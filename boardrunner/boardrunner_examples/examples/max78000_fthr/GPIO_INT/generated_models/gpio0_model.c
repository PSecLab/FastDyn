// Device Model for GPIO0

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define GPIO0_BASE              0x40008000ULL
#define GPIO0_IRQ_LINE          24
#define GPIO0_IRQ_VECTOR        (GPIO0_IRQ_LINE + 16)

#define GPIO0_FIFO_PATH         "/tmp/max78000_gpio0_pin2"
#define GPIO0_POLL_PERIOD_NS    1000000ULL   /* 1 ms */

#define BIT(x)                  (1u << (x))

/* Register offsets inferred from trace and standard GPIO alias layout */
#define REG_EN0                 0x00
#define REG_EN0_SET             0x04
#define REG_EN0_CLR             0x08

#define REG_OUTEN               0x0C
#define REG_OUTEN_SET           0x10
#define REG_OUTEN_CLR           0x14

#define REG_OUT                 0x18
#define REG_OUT_SET             0x1C
#define REG_OUT_CLR             0x20
#define REG_IN                  0x24

#define REG_INTMODE             0x28
#define REG_INTPOL              0x2C
#define REG_INEN                0x30

#define REG_INTEN               0x34
#define REG_INTEN_SET           0x38
#define REG_INTEN_CLR           0x3C

#define REG_INTFL               0x40
#define REG_INTFL_SET           0x44
#define REG_INTFL_CLR           0x48

#define REG_DUALEDGE            0x5C
#define REG_PADCTRL0            0x60
#define REG_PADCTRL1            0x64

#define REG_EN1                 0x68
#define REG_EN1_SET             0x6C
#define REG_EN1_CLR             0x70

#define REG_EN2                 0x74
#define REG_EN2_SET             0x78
#define REG_EN2_CLR             0x7C

#define REG_DS0                 0xB0
#define REG_DS1                 0xB4
#define REG_PS                  0xB8
#define REG_VSSEL               0xC0

typedef struct GPIO0State {
    uint32_t en0;
    uint32_t en1;
    uint32_t en2;

    uint32_t outen;
    uint32_t out;

    uint32_t intmode;
    uint32_t intpol;
    uint32_t inen;
    uint32_t inten;
    uint32_t intfl;
    uint32_t dualedge;

    uint32_t padctrl0;
    uint32_t padctrl1;
    uint32_t ds0;
    uint32_t ds1;
    uint32_t ps;
    uint32_t vssel;

    /* External pin levels as driven from host-side FIFO */
    uint32_t input_level;
    uint32_t last_sample;

    int fifo_fd;
    uint64_t poll_timer;
} GPIO0State;

static GPIO0State g_gpio0;

static void gpio0_debug(const char *fmt, uint64_t a, uint64_t b)
{
    char buf[160];
    snprintf(buf, sizeof(buf), fmt, (unsigned long long)a, (unsigned long long)b);
    dev_debug(buf);
}

static bool gpio0_pin_is_gpio(GPIO0State *s, unsigned pin)
{
    uint32_t mask = BIT(pin);

    return ((s->en0 & mask) != 0) &&
           ((s->en1 & mask) == 0) &&
           ((s->en2 & mask) == 0);
}

static uint32_t gpio0_sample_pins(GPIO0State *s)
{
    uint32_t val = 0;
    unsigned pin;

    for (pin = 0; pin < 32; pin++) {
        uint32_t mask = BIT(pin);

        if (!gpio0_pin_is_gpio(s, pin)) {
            continue;
        }

        if (s->outen & mask) {
            if (s->out & mask) {
                val |= mask;
            }
        } else if (s->input_level & mask) {
            val |= mask;
        }
    }

    return val;
}

static void gpio0_raise_irq_if_needed(GPIO0State *s,
                                      uint32_t old_pending,
                                      bool repulse_if_pending)
{
    uint32_t new_pending = s->intfl & s->inten;

    /*
     * The framework exposes IRQ assertion as a pulse-style API. For
     * level-sensitive GPIO interrupts, hardware can immediately relatch INTFL
     * after software clears it while the input level remains active. In that
     * case we must pulse the IRQ again even if the old pending state was
     * already non-zero, otherwise the guest stops re-entering the ISR.
     */
    if (new_pending != 0 && (old_pending == 0 || repulse_if_pending)) {
        qemu_plugin_raise_irq(GPIO0_IRQ_VECTOR, false);
    }
}

static void gpio0_set_intfl(GPIO0State *s, uint32_t mask)
{
    uint32_t old_pending = s->intfl & s->inten;
    s->intfl |= mask;
    gpio0_raise_irq_if_needed(s, old_pending, false);
}

static uint32_t gpio0_collect_interrupt_events(GPIO0State *s,
                                               uint32_t old_sample,
                                               uint32_t new_sample,
                                               bool include_level)
{
    uint32_t set_mask = 0;
    uint32_t changed = old_sample ^ new_sample;
    unsigned pin;

    for (pin = 0; pin < 32; pin++) {
        uint32_t mask = BIT(pin);
        bool old_level;
        bool new_level;
        bool active;

        if (!gpio0_pin_is_gpio(s, pin)) {
            continue;
        }

        if ((s->inen & mask) == 0) {
            continue;
        }

        old_level = (old_sample & mask) != 0;
        new_level = (new_sample & mask) != 0;

        if (changed & mask) {
            if (s->dualedge & mask) {
                set_mask |= mask;
                continue;
            }

            /*
             * MAX78000: INTMODE=1 is edge-sensitive, INTMODE=0 is level-sensitive.
             * Pin 2 is programmed with INTPOL=0, DUALEDGE=0, INTMODE=1 (edge),
             * INTEN=1. Falling edge triggers INTFL; clearing INTFL does NOT
             * re-latch while the pin stays low.
             */
            if (s->intmode & mask) {
                /* Edge-sensitive: fire only on the transition */
                if (s->intpol & mask) {
                    if (!old_level && new_level) {
                        set_mask |= mask;
                    }
                } else {
                    if (old_level && !new_level) {
                        set_mask |= mask;
                    }
                }
            } else {
                /* Level-sensitive: fire while pin matches active level */
                active = (s->intpol & mask) ? new_level : !new_level;
                if (active) {
                    set_mask |= mask;
                }
            }
        } else if (include_level &&
                   (s->dualedge & mask) == 0 &&
                   (s->intmode & mask) == 0) {
            /* Level-sensitive (INTMODE=0): re-latch INTFL while pin is active */
            active = (s->intpol & mask) ? new_level : !new_level;
            if (active) {
                set_mask |= mask;
            }
        }
    }

    return set_mask;
}

static void gpio0_sync_interrupt_state_from_pending(GPIO0State *s,
                                                    bool include_level,
                                                    bool repulse_if_pending,
                                                    uint32_t old_pending)
{
    uint32_t old_sample = s->last_sample;
    uint32_t new_sample = gpio0_sample_pins(s);
    uint32_t set_mask = gpio0_collect_interrupt_events(s, old_sample,
                                                       new_sample,
                                                       include_level);

    s->last_sample = new_sample;
    s->intfl |= set_mask;
    gpio0_raise_irq_if_needed(s, old_pending, repulse_if_pending);
}

static void gpio0_sync_interrupt_state(GPIO0State *s,
                                       bool include_level,
                                       bool repulse_if_pending)
{
    uint32_t old_pending = s->intfl & s->inten;
    gpio0_sync_interrupt_state_from_pending(s, include_level,
                                            repulse_if_pending,
                                            old_pending);
}

static void gpio0_eval_pin_transition(GPIO0State *s, unsigned pin, bool new_level)
{
    uint32_t mask = BIT(pin);
    bool old_level = (s->input_level & mask) != 0;

    if (old_level == new_level) {
        return;
    }

    if (new_level) {
        s->input_level |= mask;
    } else {
        s->input_level &= ~mask;
    }

    gpio0_sync_interrupt_state(s, true, false);
}

static void gpio0_fifo_poll(void *opaque)
{
    GPIO0State *s = (GPIO0State *)opaque;
    uint8_t ch;

    if (s->fifo_fd < 0) {
        return;
    }

    while (api_fifo_read_nonblock(s->fifo_fd, &ch) == 1) {
        switch (ch) {
        case '0':
            /* Drive GPIO0.2 low */
            gpio0_eval_pin_transition(s, 2, false);
            break;
        case '1':
            /* Drive GPIO0.2 high */
            gpio0_eval_pin_transition(s, 2, true);
            break;
        case 't':
        case 'T':
            /* Toggle GPIO0.2 */
            gpio0_eval_pin_transition(s, 2, (s->input_level & BIT(2)) == 0);
            break;
        default:
            gpio0_debug("GPIO0: ignoring FIFO byte 0x%llx at path selector %llx",
                        (uint64_t)ch, 2);
            break;
        }
    }
}

uint64_t gpio0_read(void *opaque, hwaddr addr, unsigned size)
{
    GPIO0State *s = (GPIO0State *)opaque;
    hwaddr offset;
    uint32_t val = 0;

    (void)size;

    if (addr < GPIO0_BASE) {
        gpio0_debug("GPIO0: read below base addr=0x%llx dummy=0x%llx", addr, 0);
        return 0;
    }

    offset = addr - GPIO0_BASE;

    switch (offset) {
    case REG_EN0:
    case REG_EN0_SET:
    case REG_EN0_CLR:
        val = s->en0;
        break;

    case REG_OUTEN:
    case REG_OUTEN_SET:
    case REG_OUTEN_CLR:
        val = s->outen;
        break;

    case REG_OUT:
    case REG_OUT_SET:
    case REG_OUT_CLR:
        val = s->out;
        break;

    case REG_IN:
        val = gpio0_sample_pins(s);
        break;

    case REG_INTMODE:
        val = s->intmode;
        break;

    case REG_INTPOL:
        val = s->intpol;
        break;

    case REG_INEN:
        val = s->inen;
        break;

    case REG_INTEN:
    case REG_INTEN_SET:
    case REG_INTEN_CLR:
        val = s->inten;
        break;

    case REG_INTFL:
    case REG_INTFL_SET:
    case REG_INTFL_CLR:
        val = s->intfl;
        break;

    case REG_DUALEDGE:
        val = s->dualedge;
        break;

    case REG_PADCTRL0:
        val = s->padctrl0;
        break;

    case REG_PADCTRL1:
        val = s->padctrl1;
        break;

    case REG_EN1:
    case REG_EN1_SET:
    case REG_EN1_CLR:
        val = s->en1;
        break;

    case REG_EN2:
    case REG_EN2_SET:
    case REG_EN2_CLR:
        val = s->en2;
        break;

    case REG_DS0:
        val = s->ds0;
        break;

    case REG_DS1:
        val = s->ds1;
        break;

    case REG_PS:
        val = s->ps;
        break;

    case REG_VSSEL:
        val = s->vssel;
        break;

    default:
        gpio0_debug("GPIO0: unhandled read addr=0x%llx off=0x%llx", addr, offset);
        val = 0;
        break;
    }

    return val;
}

void gpio0_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    GPIO0State *s = (GPIO0State *)opaque;
    hwaddr offset;
    uint32_t v = (uint32_t)value;
    uint32_t old_pending;
    bool resync = false;

    (void)size;

    if (addr < GPIO0_BASE) {
        gpio0_debug("GPIO0: write below base addr=0x%llx val=0x%llx", addr, value);
        return;
    }

    offset = addr - GPIO0_BASE;

    switch (offset) {
    case REG_EN0:
        s->en0 = v;
        resync = true;
        break;
    case REG_EN0_SET:
        s->en0 |= v;
        resync = true;
        break;
    case REG_EN0_CLR:
        s->en0 &= ~v;
        resync = true;
        break;

    case REG_OUTEN:
        s->outen = v;
        resync = true;
        break;
    case REG_OUTEN_SET:
        s->outen |= v;
        resync = true;
        break;
    case REG_OUTEN_CLR:
        s->outen &= ~v;
        resync = true;
        break;

    case REG_OUT:
        s->out = v;
        resync = true;
        break;
    case REG_OUT_SET:
        s->out |= v;
        resync = true;
        break;
    case REG_OUT_CLR:
        s->out &= ~v;
        resync = true;
        break;

    case REG_INTMODE:
        s->intmode = v;
        resync = true;
        break;

    case REG_INTPOL:
        s->intpol = v;
        resync = true;
        break;

    case REG_INEN:
        s->inen = v;
        resync = true;
        break;

    case REG_INTEN:
        old_pending = s->intfl & s->inten;
        s->inten = v;
        gpio0_sync_interrupt_state_from_pending(s, true, false, old_pending);
        break;

    case REG_INTEN_SET:
        old_pending = s->intfl & s->inten;
        s->inten |= v;
        gpio0_sync_interrupt_state_from_pending(s, true, false, old_pending);
        break;

    case REG_INTEN_CLR:
        s->inten &= ~v;
        break;

    case REG_INTFL:
        old_pending = s->intfl & s->inten;
        s->intfl = v;
        gpio0_sync_interrupt_state_from_pending(s, true, true, old_pending);
        break;

    case REG_INTFL_SET:
        gpio0_set_intfl(s, v);
        break;

    case REG_INTFL_CLR:
        old_pending = s->intfl & s->inten;
        s->intfl &= ~v;
        gpio0_sync_interrupt_state_from_pending(s, true, true, old_pending);
        break;

    case REG_DUALEDGE:
        s->dualedge = v;
        resync = true;
        break;

    case REG_PADCTRL0:
        s->padctrl0 = v;
        break;

    case REG_PADCTRL1:
        s->padctrl1 = v;
        break;

    case REG_EN1:
        s->en1 = v;
        resync = true;
        break;
    case REG_EN1_SET:
        s->en1 |= v;
        resync = true;
        break;
    case REG_EN1_CLR:
        s->en1 &= ~v;
        resync = true;
        break;

    case REG_EN2:
        s->en2 = v;
        resync = true;
        break;
    case REG_EN2_SET:
        s->en2 |= v;
        resync = true;
        break;
    case REG_EN2_CLR:
        s->en2 &= ~v;
        resync = true;
        break;

    case REG_DS0:
        s->ds0 = v;
        break;

    case REG_DS1:
        s->ds1 = v;
        break;

    case REG_PS:
        s->ps = v;
        break;

    case REG_VSSEL:
        s->vssel = v;
        break;

    default:
        gpio0_debug("GPIO0: unhandled write addr=0x%llx val=0x%llx", addr, value);
        break;
    }

    if (resync) {
        gpio0_sync_interrupt_state(s, true, false);
    }
}

/* MUST return &g_state — framework stores this and passes it as opaque to _read/_write */
void* gpio0_init(ConfigSection* model_info)
{
    GPIO0State *s = &g_gpio0;
    (void)model_info;

    memset(s, 0, sizeof(*s));

    /*
     * Observed reset-like values from trace:
     * - INEN reads back as 0xFFFFFFFF before firmware rewrites it.
     * - GPIO0.2 behaves like an already-asserted active-low input once the
     *   firmware muxes it into GPIO mode, so model it low by default.
     */
    s->inen = 0xFFFFFFFFu;
    s->input_level = 0xFFFFFFFFu;  /* All pins high (pull-up resting state) */
    s->last_sample = gpio0_sample_pins(s);

    s->fifo_fd = api_fifo_open(GPIO0_FIFO_PATH);
    s->poll_timer = qemu_plugin_timer_new_period_ns(gpio0_fifo_poll, s,
                                                    GPIO0_POLL_PERIOD_NS);

    gpio0_debug("GPIO0: host FIFO path active selector=0x%llx dummy=0x%llx",
                (uint64_t)GPIO0_FIFO_PATH, 0);

    return s;
}