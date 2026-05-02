// Device Model for ADC

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Inferred Register Functions:
// CTRL   (0x00): configuration register; bit 11 inferred as a one-shot start/trigger bit.
// STATUS (0x04): hardware-owned status; active / AFE power-up / overflow bits.
// DATA   (0x08): 16-bit ADC sample output.
// INTR   (0x0C): interrupt enable bits [4:0], interrupt flags [20:16], computed pending bit [22].

#define ADC_BASE_ADDR               0x40034000ULL

#define ADC_REG_CTRL                0x00
#define ADC_REG_STATUS              0x04
#define ADC_REG_DATA                0x08
#define ADC_REG_INTR                0x0C

// STATUS bits
#define ADC_STATUS_ACTIVE           (1u << 0)
#define ADC_STATUS_AFE_PWR_UP       (1u << 2)
#define ADC_STATUS_OVERFLOW         (1u << 3)

// INTR enable bits
#define ADC_INTR_DONE_IE            (1u << 0)
#define ADC_INTR_REF_READY_IE       (1u << 1)
#define ADC_INTR_HI_LIMIT_IE        (1u << 2)
#define ADC_INTR_LO_LIMIT_IE        (1u << 3)
#define ADC_INTR_OVERFLOW_IE        (1u << 4)
#define ADC_INTR_IE_MASK            0x0000001Fu

// INTR flag bits
#define ADC_INTR_DONE_IF            (1u << 16)
#define ADC_INTR_REF_READY_IF       (1u << 17)
#define ADC_INTR_HI_LIMIT_IF        (1u << 18)
#define ADC_INTR_LO_LIMIT_IF        (1u << 19)
#define ADC_INTR_OVERFLOW_IF        (1u << 20)
#define ADC_INTR_IF_MASK            0x001F0000u

// INTR pending bit
#define ADC_INTR_PENDING            (1u << 22)

#define ADC_CTRL_START_INFERRED     (1u << 0)

// Reset/observed defaults from trace
#define ADC_CTRL_RESET_VALUE        0x0A000000u
#define ADC_DEFAULT_SAMPLE          0x000Cu

// Keep the conversion latency short, but non-zero.
#define ADC_CONVERSION_DELAY_NS     1000ULL

// Host-side FIFO used to feed ADC samples (little-endian 16-bit preferred).
#define ADC_FIFO_PATH               "/tmp/max78000_adc_in"

typedef struct ADCState {
    uint32_t ctrl;          // readable control state; inferred start bit self-clears
    uint32_t status;        // hardware-owned status bits
    uint32_t intr_ie;       // INTR enable bits [4:0]
    uint32_t intr_if;       // INTR flag bits [20:16]
    uint16_t data;          // DATA[15:0]

    bool busy;
    uint64_t deadline_ns;
    uint64_t timer;

    int fifo_fd;
} ADCState;

static ADCState g_adc;

static uint64_t adc_extract_access_u32(uint32_t reg, hwaddr offset, unsigned size) {
    unsigned shift = (unsigned)(offset & 0x3ULL) * 8U;

    if (size >= 4) {
        return reg;
    }

    return (reg >> shift) & ((1ULL << (size * 8U)) - 1ULL);
}

static uint32_t adc_merge_access_u32(uint32_t oldv, hwaddr offset, uint64_t value, unsigned size) {
    unsigned shift = (unsigned)(offset & 0x3ULL) * 8U;
    uint32_t mask;

    if (size >= 4) {
        return (uint32_t)value;
    }

    mask = (uint32_t)(((1ULL << (size * 8U)) - 1ULL) << shift);
    return (oldv & ~mask) | ((((uint32_t)value) << shift) & mask);
}

static uint32_t adc_build_intr_value(ADCState *s) {
    if (s->ctrl & 0x8u) {
        s->intr_if |= ADC_INTR_REF_READY_IF;
    }

    uint32_t raw_ie = s->intr_ie & ADC_INTR_IE_MASK;
    uint32_t raw_if = s->intr_if & ADC_INTR_IF_MASK;
    uint32_t pending = 0;

    // pending reflects enabled-and-flagged sources.
    if (((raw_ie >> 0) & 0x1u) && (raw_if & ADC_INTR_DONE_IF)) {
        pending = ADC_INTR_PENDING;
    }
    if (((raw_ie >> 1) & 0x1u) && (raw_if & ADC_INTR_REF_READY_IF)) {
        pending = ADC_INTR_PENDING;
    }
    if (((raw_ie >> 2) & 0x1u) && (raw_if & ADC_INTR_HI_LIMIT_IF)) {
        pending = ADC_INTR_PENDING;
    }
    if (((raw_ie >> 3) & 0x1u) && (raw_if & ADC_INTR_LO_LIMIT_IF)) {
        pending = ADC_INTR_PENDING;
    }
    if (((raw_ie >> 4) & 0x1u) && (raw_if & ADC_INTR_OVERFLOW_IF)) {
        pending = ADC_INTR_PENDING;
    }

    return raw_ie | raw_if | pending;
}

static void adc_try_consume_host_sample(ADCState *s) {
    uint8_t lo = 0, hi = 0;
    int got_lo = 0, got_hi = 0;

    if (s->fifo_fd < 0) {
        return;
    }

    got_lo = api_fifo_read_nonblock(s->fifo_fd, &lo);
    if (got_lo == 1) {
        got_hi = api_fifo_read_nonblock(s->fifo_fd, &hi);
        if (got_hi == 1) {
            s->data = (uint16_t)lo | ((uint16_t)hi << 8);
        } else {
            // If only one byte is present, accept it as an 8-bit sample.
            s->data = (uint16_t)lo;
        }
    }
}

static void adc_complete_conversion(ADCState *s) {
    if (!s->busy) {
        return;
    }

    s->busy = false;
    s->status &= ~(ADC_STATUS_ACTIVE | ADC_STATUS_AFE_PWR_UP);

    // Update sample from host input if available; otherwise keep previous/default.
    adc_try_consume_host_sample(s);

    s->intr_if |= ADC_INTR_DONE_IF;
}

static void adc_update(ADCState *s) {
    int64_t now;

    if (!s->busy) {
        return;
    }

    now = qemu_plugin_get_virtual_timer();
    if (now < 0) {
        return;
    }

    if ((uint64_t)now >= s->deadline_ns) {
        adc_complete_conversion(s);
    }
}

// Timer callback must match void (*)(void *) exactly.
static void adc_timer_cb(void *opaque) {
    ADCState *s = (ADCState *)opaque;
    adc_update(s);
}

static void adc_start_conversion(ADCState *s) {
    int64_t now = qemu_plugin_get_virtual_timer();

    if (now < 0) {
        now = 0;
    }

    s->busy = true;
    s->status |= (ADC_STATUS_ACTIVE | ADC_STATUS_AFE_PWR_UP);

    // Starting a new conversion clears prior completion flags.
    s->intr_if &= ~ADC_INTR_IF_MASK;

    s->deadline_ns = (uint64_t)now + ADC_CONVERSION_DELAY_NS;
    qemu_plugin_timer_alarm(s->timer, s->deadline_ns);
}

// This function will emulate all device reads
uint64_t adc_read(void *opaque, hwaddr addr, unsigned size) {
    ADCState *s = (ADCState *)opaque;
    hwaddr offset = addr - ADC_BASE_ADDR;
    hwaddr reg_off = offset & ~0x3ULL;
    uint32_t regv = 0;

    adc_update(s);

    switch (reg_off) {
    case ADC_REG_CTRL:
        regv = s->ctrl;
        break;
    case ADC_REG_STATUS:
        regv = s->status;
        break;
    case ADC_REG_DATA:
        regv = (uint32_t)s->data;
        break;
    case ADC_REG_INTR:
        regv = adc_build_intr_value(s);
        break;
    default:
        return 0;
    }

    return adc_extract_access_u32(regv, offset, size);
}

// This function will emulate all device writes
void adc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    ADCState *s = (ADCState *)opaque;
    hwaddr offset = addr - ADC_BASE_ADDR;
    hwaddr reg_off = offset & ~0x3ULL;

    adc_update(s);

    switch (reg_off) {
    case ADC_REG_CTRL: {
        uint32_t merged = adc_merge_access_u32(s->ctrl, offset, value, size);
        bool start = (merged & ADC_CTRL_START_INFERRED) != 0;

        // Preserve visible control state but self-clear the inferred start bit.
        s->ctrl = merged & ~ADC_CTRL_START_INFERRED;

        if (start) {
            adc_start_conversion(s);
        }
        break;
    }

    case ADC_REG_STATUS:
        /*
         * Supplied field info marks the observed STATUS bits as read-only
         * hardware-owned state. Ignore writes to keep behavior consistent.
         */
        break;

    case ADC_REG_DATA:
        // DATA is read-only according to supplied field info.
        break;

    case ADC_REG_INTR: {
        uint32_t old_intr = adc_build_intr_value(s);
        uint32_t merged = adc_merge_access_u32(old_intr, offset, value, size);
        uint32_t clear_flags = merged & ADC_INTR_IF_MASK;

        // Low bits are RW enables.
        s->intr_ie = merged & ADC_INTR_IE_MASK;

        // Upper flag bits are modeled as W1C.
        s->intr_if &= ~clear_flags;
        break;
    }

    default:
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* adc_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_adc, 0, sizeof(g_adc));

    g_adc.ctrl = ADC_CTRL_RESET_VALUE;
    g_adc.status = 0;
    g_adc.intr_ie = 0;
    g_adc.intr_if = 0;
    g_adc.data = ADC_DEFAULT_SAMPLE;
    g_adc.busy = false;
    g_adc.deadline_ns = 0;
    g_adc.fifo_fd = -1;

    g_adc.timer = qemu_plugin_timer_new_ns(adc_timer_cb, &g_adc);
    g_adc.fifo_fd = api_fifo_open(ADC_FIFO_PATH);

    if (g_adc.fifo_fd < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "ADC: failed to open FIFO %s", ADC_FIFO_PATH);
        dev_debug(msg);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ADC: sample FIFO ready at %s", ADC_FIFO_PATH);
        dev_debug(msg);
    }

    return &g_adc;
}