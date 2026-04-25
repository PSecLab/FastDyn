// Device Model for ADC

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ADC1_BASE           0x40022000ULL
#define ADC2_BASE           0x40022100ULL
#define ADC12_COMMON_BASE   0x40022300ULL

#define ADC_REGION_SIZE     0x100
#define ADC_COMMON_SIZE     0x100
#define ADC_REG_WORDS       64
#define ADC_COMMON_WORDS    16

/* Per-instance ADC register offsets */
#define ADC_ISR_OFS         0x00
#define ADC_IER_OFS         0x04
#define ADC_CR_OFS          0x08
#define ADC_CFGR_OFS        0x0C
#define ADC_DR_OFS          0x40

/* ADC12 common register offsets */
#define ADC_CCR_OFS         0x08

/* Simplified CR bits used by this model */
#define ADC_CR_ADEN         (1u << 0)
#define ADC_CR_ADDIS        (1u << 1)
#define ADC_CR_ADSTART      (1u << 2)
#define ADC_CR_ADSTP        (1u << 4)
#define ADC_CR_ADCAL        (1u << 31)

/*
 * In this trace, ADCx_CR reads mainly expose the high control field
 * (0x10000000 / 0x20000000), while command bits behave as self-clearing actions.
 */
#define ADC_CR_READBACK_MASK    0x30000000U

/*
 * Hardware first exposes a ready baseline (0x1001), then later conversion
 * state is observed predominantly as 0x100B, with rare 0x100F reads.
 */
#define ADC_ISR_READY_FLAGS         0x00001001U
#define ADC_ISR_EOC                 0x00000004U
#define ADC_ISR_CONVERSION_FLAGS    0x0000000EU

/* STM32H7 ADC CFGR low bits hold DMA management mode. */
#define ADC_CFGR_DMNGT_MASK         0x3u
#define ADC_CFGR_CONT               (1u << 13)

/*
 * Bit 21 is written by firmware during init in this trace but does not read back
 * on hardware; mask it out to avoid diverging CFGR entropy/flow.
 */
#define ADC_CFGR_UNREADABLE_MASK    0x00200000U

/*
 * CR readback in this trace shows a delayed hardware-owned transition to
 * 0x20000000 while firmware polls the register.
 */
#define ADC_CR_SETTLED_READBACK     0x20000000U
#define ADC_CR_SETTLE_DELAY_NS      1000ULL

#define ADC_SAMPLE_PERIOD_NS    10000000ULL

typedef struct {
    uint32_t regs[ADC_REG_WORDS];

    bool enabled;
    bool running_continuous;
    bool conversion_active;
    bool cr_settle_pending;
    bool cr_settled_once;
    bool synthetic_cr_enabled;
    bool first_conversion_pending;
    bool startup_skip_once;

    uint8_t cr_reads_until_settle;

    uint16_t last_sample;
    uint16_t ramp_seed;

    uint64_t last_conv_time_ns;
    uint64_t period_ns;

    int dma_stream_id;

    int fifo_fd;
    uint8_t fifo_partial[2];
    int fifo_partial_count;
} ADCInstanceState;

typedef struct {
    ADCInstanceState adc1;
    ADCInstanceState adc2;
    uint32_t common_regs[ADC_COMMON_WORDS];
    uint64_t timer_handle;
} ADCState;

static ADCState g_adc;

/* Host FIFOs are created automatically by api_fifo_open(). */
static const char *adc1_fifo_path = "/tmp/stm32h7_adc1_in";
static const char *adc2_fifo_path = "/tmp/stm32h7_adc2_in";

static uint64_t adc_extract_subword(uint32_t word, hwaddr offset, unsigned size)
{
    unsigned shift = (offset & 0x3) * 8;
    uint64_t mask;

    switch (size) {
    case 1:
        mask = 0xFFu;
        break;
    case 2:
        mask = 0xFFFFu;
        break;
    default:
        mask = 0xFFFFFFFFu;
        break;
    }

    return (word >> shift) & mask;
}

static uint32_t adc_merge_subword(uint32_t old_word, hwaddr offset, uint64_t value, unsigned size)
{
    unsigned shift = (offset & 0x3) * 8;
    uint32_t mask;

    switch (size) {
    case 1:
        mask = 0xFFu << shift;
        break;
    case 2:
        mask = 0xFFFFu << shift;
        break;
    default:
        mask = 0xFFFFFFFFu;
        break;
    }

    return (old_word & ~mask) | ((((uint32_t)value) << shift) & mask);
}

static uint64_t adc_now_ns(void)
{
    int64_t now = qemu_plugin_get_virtual_timer();
    return (now < 0) ? 0 : (uint64_t)now;
}

static void adc_schedule_cr_settle(ADCInstanceState *adc)
{
    /*
     * Only ADC2 shows the synthetic one-shot 0x0 -> 0x20000000 CR transition
     * in the trace. ADC1's CR entropy is driven by explicit firmware-visible
     * writes and should not be advanced by unrelated register writes.
     *
     * Keep the synthetic transition read-driven (not timer-driven) so the
     * first observed poll read can still return 0 before the second returns
     * 0x20000000.
     */
    if (!adc->synthetic_cr_enabled) {
        return;
    }

    if (adc->cr_settled_once || adc->cr_settle_pending) {
        return;
    }

    if ((adc->regs[ADC_CR_OFS >> 2] & ADC_CR_READBACK_MASK) != 0) {
        return;
    }

    adc->cr_settle_pending = true;
    adc->cr_reads_until_settle = 2;
}

static void adc_maybe_settle_cr_readback(ADCInstanceState *adc, bool from_cr_read)
{
    if (!adc->cr_settle_pending) {
        return;
    }

    /*
     * Drive the synthetic transition only from CR polling reads. Using virtual
     * time here makes the transition fire too early because timer callbacks
     * occur only at TB boundaries and can run before the firmware's first
     * observed read in the poll loop.
     */
    if (!from_cr_read) {
        return;
    }

    if (adc->cr_reads_until_settle > 0) {
        adc->cr_reads_until_settle--;
    }
    if (adc->cr_reads_until_settle != 0) {
        return;
    }

    adc->regs[ADC_CR_OFS >> 2] =
        (adc->regs[ADC_CR_OFS >> 2] & ~ADC_CR_READBACK_MASK) |
        ADC_CR_SETTLED_READBACK;
    adc->cr_settle_pending = false;
    adc->cr_settled_once = true;
    adc->cr_reads_until_settle = 0;
}

static bool adc_dma_enabled(ADCInstanceState *adc)
{
    return (adc->regs[ADC_CFGR_OFS >> 2] & ADC_CFGR_DMNGT_MASK) != 0;
}

static uint16_t adc_next_fallback_sample(ADCInstanceState *adc)
{
    adc->ramp_seed = (uint16_t)(adc->ramp_seed + 0x31u);
    return adc->ramp_seed;
}

static uint16_t adc_read_host_sample(ADCInstanceState *adc)
{
    uint8_t b;

    while (adc->fifo_fd >= 0 && adc->fifo_partial_count < 2) {
        int rc = api_fifo_read_nonblock(adc->fifo_fd, &b);
        if (rc != 1) {
            break;
        }
        adc->fifo_partial[adc->fifo_partial_count++] = b;
    }

    if (adc->fifo_partial_count >= 2) {
        uint16_t v = (uint16_t)adc->fifo_partial[0] |
                     ((uint16_t)adc->fifo_partial[1] << 8);
        adc->fifo_partial_count = 0;
        return v;
    }

    if (adc->fifo_partial_count == 1) {
        uint16_t v = (uint16_t)adc->fifo_partial[0] << 4;
        adc->fifo_partial_count = 0;
        return v;
    }

    return adc_next_fallback_sample(adc);
}

static bool adc_issue_dma_if_needed(ADCInstanceState *adc, uint16_t sample)
{
    int rc;

    if (adc->dma_stream_id < 0) {
        return false;
    }

    if (!adc_dma_enabled(adc)) {
        return false;
    }

    {
        uint8_t payload[2];
        payload[0] = (uint8_t)(sample & 0xFF);
        payload[1] = (uint8_t)((sample >> 8) & 0xFF);
        rc = api_dma_request_data(adc->dma_stream_id, payload, 2);
    }

    return rc == 0;
}

static void adc_complete_conversion(ADCInstanceState *adc)
{
    uint16_t sample;

    if (!adc->enabled || !adc->conversion_active) {
        return;
    }

    sample = adc_read_host_sample(adc);
    adc->last_sample = sample;
    adc->regs[ADC_DR_OFS >> 2] = sample;
    adc->regs[ADC_ISR_OFS >> 2] |=
        (ADC_ISR_READY_FLAGS | ADC_ISR_CONVERSION_FLAGS);

    if (adc_issue_dma_if_needed(adc, sample)) {
        /*
         * In this trace, DMA-backed conversions settle at 0x100B rather than
         * leaving EOC sticky. Model that by clearing EOC when the DMA request
         * is successfully consumed.
         */
        adc->regs[ADC_ISR_OFS >> 2] &= ~ADC_ISR_EOC;
    }

    if (!adc->running_continuous) {
        adc->conversion_active = false;
    }
}

static void adc_service_conversion(ADCInstanceState *adc, bool force)
{
    uint64_t now;

    if (!adc->enabled || !adc->conversion_active) {
        return;
    }

    if (!force && adc->startup_skip_once) {
        adc->startup_skip_once = false;
        return;
    }

    now = adc_now_ns();

    if (adc->first_conversion_pending) {
        adc->first_conversion_pending = false;
        adc->last_conv_time_ns = now;
        adc_complete_conversion(adc);
        return;
    }

    if (force || (now - adc->last_conv_time_ns) >= adc->period_ns) {
        adc->last_conv_time_ns = now;
        adc_complete_conversion(adc);
    }
}

static void adc_periodic_cb(void *opaque)
{
    ADCState *s = (ADCState *)opaque;

    adc_maybe_settle_cr_readback(&s->adc1, false);
    adc_maybe_settle_cr_readback(&s->adc2, false);

    adc_service_conversion(&s->adc1, false);
    adc_service_conversion(&s->adc2, false);
}

static ADCInstanceState *adc_instance_from_addr(hwaddr addr, hwaddr *base_out)
{
    if (addr >= ADC1_BASE && addr < (ADC1_BASE + ADC_REGION_SIZE)) {
        if (base_out) {
            *base_out = ADC1_BASE;
        }
        return &g_adc.adc1;
    }

    if (addr >= ADC2_BASE && addr < (ADC2_BASE + ADC_REGION_SIZE)) {
        if (base_out) {
            *base_out = ADC2_BASE;
        }
        return &g_adc.adc2;
    }

    return NULL;
}

static bool adc_is_common_addr(hwaddr addr, hwaddr *base_out)
{
    if (addr >= ADC12_COMMON_BASE && addr < (ADC12_COMMON_BASE + ADC_COMMON_SIZE)) {
        if (base_out) {
            *base_out = ADC12_COMMON_BASE;
        }
        return true;
    }
    return false;
}

static uint64_t adc_read_instance(ADCInstanceState *adc, hwaddr offset, unsigned size)
{
    uint32_t word;

    if ((offset >> 2) >= ADC_REG_WORDS) {
        return 0;
    }

    switch (offset & ~0x3ULL) {
    case ADC_ISR_OFS:
        adc_service_conversion(adc, false);
        word = adc->regs[ADC_ISR_OFS >> 2];
        break;

    case ADC_CR_OFS:
        adc_maybe_settle_cr_readback(adc, true);
        word = adc->regs[ADC_CR_OFS >> 2];
        break;

    case ADC_CFGR_OFS:
        word = adc->regs[ADC_CFGR_OFS >> 2];
        break;

    case ADC_DR_OFS:
        adc_service_conversion(adc, false);
        word = adc->regs[ADC_DR_OFS >> 2];
        break;

    default:
        word = adc->regs[offset >> 2];
        break;
    }

    return adc_extract_subword(word, offset, size);
}

static void adc_write_cr(ADCInstanceState *adc, hwaddr offset, uint64_t value, unsigned size)
{
    uint32_t old_word = adc->regs[ADC_CR_OFS >> 2];
    uint32_t merged = adc_merge_subword(old_word, offset, value, size);
    uint32_t old_readback = old_word & ADC_CR_READBACK_MASK;
    uint32_t new_readback = merged & ADC_CR_READBACK_MASK;
    uint64_t now;

    /*
     * Only the persistent high control field is visible in CR readback for this
     * trace. Low command bits are write-triggered actions and self-clear.
     *
     * Crucially, low-bit command writes must not cancel the pending hardware-
     * owned 0x0 -> 0x20000000 transition. Only an explicit change to the visible
     * high field should stop that pending transition.
     */
    adc->regs[ADC_CR_OFS >> 2] = new_readback;
    if (new_readback != old_readback) {
        adc->cr_settle_pending = false;
        adc->cr_reads_until_settle = 0;
        if (new_readback != 0) {
            adc->cr_settled_once = true;
        }
    }

    if (merged & ADC_CR_ADCAL) {
        /* Complete calibration immediately; firmware should observe ADC ready, not a stuck ADCAL bit. */
        adc->regs[ADC_ISR_OFS >> 2] |= ADC_ISR_READY_FLAGS;
    }

    if (merged & ADC_CR_ADEN) {
        adc->enabled = true;
        adc->regs[ADC_ISR_OFS >> 2] |= ADC_ISR_READY_FLAGS;
    }

    if (merged & ADC_CR_ADDIS) {
        adc->enabled = false;
        adc->running_continuous = false;
        adc->conversion_active = false;
        adc->first_conversion_pending = false;
        adc->startup_skip_once = false;
        adc->regs[ADC_ISR_OFS >> 2] &= ~ADC_ISR_READY_FLAGS;
    }

    if ((merged & ADC_CR_ADSTART) && adc->enabled) {
        now = adc_now_ns();
        adc->conversion_active = true;
        adc->running_continuous =
            (adc->regs[ADC_CFGR_OFS >> 2] & ADC_CFGR_CONT) != 0;
        adc->first_conversion_pending = true;
        adc->startup_skip_once = true;
        adc->last_conv_time_ns = now;
    }

    if (merged & ADC_CR_ADSTP) {
        adc->running_continuous = false;
        adc->conversion_active = false;
        adc->first_conversion_pending = false;
        adc->startup_skip_once = false;
    }
}

static void adc_write_instance(ADCInstanceState *adc, hwaddr offset, uint64_t value, unsigned size)
{
    uint32_t old_word, merged;

    if ((offset >> 2) >= ADC_REG_WORDS) {
        return;
    }

    switch (offset & ~0x3ULL) {
    case ADC_ISR_OFS:
        old_word = adc->regs[ADC_ISR_OFS >> 2];
        merged = adc_merge_subword(old_word, offset, value, size);
        /* W1C behavior */
        adc->regs[ADC_ISR_OFS >> 2] &= ~merged;
        break;

    case ADC_CR_OFS:
        adc_write_cr(adc, offset, value, size);
        break;

    case ADC_CFGR_OFS:
        old_word = adc->regs[ADC_CFGR_OFS >> 2];
        merged = adc_merge_subword(old_word, offset, value, size);
        merged &= ~ADC_CFGR_UNREADABLE_MASK;
        adc->regs[ADC_CFGR_OFS >> 2] = merged;
        if (adc->conversion_active) {
            adc->running_continuous = (merged & ADC_CFGR_CONT) != 0;
        }
        adc_schedule_cr_settle(adc);
        break;

    case ADC_DR_OFS:
        /* DR is effectively read-only from firmware perspective; ignore writes. */
        break;

    default:
        old_word = adc->regs[offset >> 2];
        merged = adc_merge_subword(old_word, offset, value, size);
        adc->regs[offset >> 2] = merged;
        adc_schedule_cr_settle(adc);
        break;
    }
}

static uint64_t adc_read_common(hwaddr offset, unsigned size)
{
    uint32_t word;

    if ((offset >> 2) >= ADC_COMMON_WORDS) {
        return 0;
    }

    word = g_adc.common_regs[offset >> 2];
    return adc_extract_subword(word, offset, size);
}

static void adc_write_common(hwaddr offset, uint64_t value, unsigned size)
{
    uint32_t old_word, merged;

    if ((offset >> 2) >= ADC_COMMON_WORDS) {
        return;
    }

    old_word = g_adc.common_regs[offset >> 2];
    merged = adc_merge_subword(old_word, offset, value, size);
    g_adc.common_regs[offset >> 2] = merged;
}

uint64_t adc_read(void *opaque, hwaddr addr, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    ADCInstanceState *adc;
    hwaddr base;

    (void)s;

    if (adc_is_common_addr(addr, &base)) {
        hwaddr offset = addr - base;
        return adc_read_common(offset, size);
    }

    adc = adc_instance_from_addr(addr, &base);
    if (adc) {
        hwaddr offset = addr - base;
        return adc_read_instance(adc, offset, size);
    }

    return 0;
}

void adc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    ADCState *s = (ADCState *)opaque;
    ADCInstanceState *adc;
    hwaddr base;

    (void)s;

    if (adc_is_common_addr(addr, &base)) {
        hwaddr offset = addr - base;
        adc_write_common(offset, value, size);
        return;
    }

    adc = adc_instance_from_addr(addr, &base);
    if (adc) {
        hwaddr offset = addr - base;
        adc_write_instance(adc, offset, value, size);
        return;
    }
}

/* MUST return &g_adc — framework stores this and passes it as opaque to _read/_write */
void* adc_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_adc, 0, sizeof(g_adc));

    g_adc.adc1.period_ns = ADC_SAMPLE_PERIOD_NS;
    g_adc.adc2.period_ns = ADC_SAMPLE_PERIOD_NS;

    g_adc.adc1.dma_stream_id = 1;   /* Inferred from DMA1->S1CR and DMAMUX1 C1CR */
    g_adc.adc2.dma_stream_id = -1;  /* No ADC2 DMA path evidenced in the trace */

    g_adc.adc1.ramp_seed = 0x0100;
    g_adc.adc2.ramp_seed = 0x0200;

    g_adc.adc1.synthetic_cr_enabled = false;
    g_adc.adc2.synthetic_cr_enabled = true;
    g_adc.adc1.cr_settle_pending = false;
    g_adc.adc2.cr_settle_pending = true;
    g_adc.adc1.cr_settled_once = false;
    g_adc.adc2.cr_settled_once = false;
    g_adc.adc1.cr_reads_until_settle = 0;
    g_adc.adc2.cr_reads_until_settle = 2;

    g_adc.adc1.fifo_fd = api_fifo_open(adc1_fifo_path);
    g_adc.adc2.fifo_fd = api_fifo_open(adc2_fifo_path);

    g_adc.common_regs[ADC_CCR_OFS >> 2] = 0x00000000;

    g_adc.timer_handle = qemu_plugin_timer_new_period_ns(adc_periodic_cb, &g_adc,
                                                         ADC_SAMPLE_PERIOD_NS);

    return &g_adc;
}