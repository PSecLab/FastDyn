// Device Model for TIM3
//
// Inferred Register Functions:
//   CR1   : control register, includes CEN enable bit
//   EGR   : update generation; UG resets/reinitializes counter
/*
 *   CCMR1 : channel mode configuration; trace shows output-view reads return 0
 *           for this workload, so it is modeled as write-only/read-as-zero
 *   CCER  : channel enable configuration, read/write stateful
 */
//   CNT   : free-running up-counter while CEN=1
//   PSC   : prescaler
//   ARR   : auto-reload / wrap value
//
// Notes:
// - Absolute addresses are provided by the framework; we subtract TIM3_BASE.
// - No IRQ behavior is implemented because no interrupt evidence was provided.
// - No DMA/signal/host-I/O is needed from the supplied trace.
// - TIM3 is modeled as a 16-bit timer.
// - CNT progression is derived from the virtual timer using a nominal 200 MHz
//   timer input clock, which matches common STM32H7 timer configurations and
//   gives sensible behavior for the observed PSC/ARR programming.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define TIM3_BASE               0x40000400ULL

#define TIM3_CR1_OFFSET         0x00
#define TIM3_CR2_OFFSET         0x04
#define TIM3_SMCR_OFFSET        0x08
#define TIM3_DIER_OFFSET        0x0C
#define TIM3_SR_OFFSET          0x10
#define TIM3_EGR_OFFSET         0x14
#define TIM3_CCMR1_OFFSET       0x18
#define TIM3_CCER_OFFSET        0x20
#define TIM3_CNT_OFFSET         0x24
#define TIM3_PSC_OFFSET         0x28
#define TIM3_ARR_OFFSET         0x2C

#define TIM3_CR1_CEN            (1U << 0)
#define TIM3_EGR_UG             (1U << 0)

/*
 * Nominal timer kernel clock used for CNT progression.
 * 200 MHz => 5 ns base timer tick before prescaler.
 */
#define TIM3_INPUT_CLOCK_HZ     200000000ULL
#define TIM3_BASE_TICK_NS       (1000000000ULL / TIM3_INPUT_CLOCK_HZ) /* 5 ns */

typedef struct TIM3State {
    uint16_t cr1;
    uint16_t cr2;
    uint16_t smcr;
    uint16_t dier;
    uint16_t sr;
    uint16_t egr;
    uint16_t ccmr1;
    uint16_t ccer;
    uint16_t cnt;
    uint16_t psc;
    uint16_t arr;

    uint64_t last_sync_ns;
} TIM3State;

static TIM3State g_tim3;

static void tim3_debug(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static uint32_t tim3_mask_for_size(unsigned size)
{
    switch (size) {
    case 1: return 0xFFU;
    case 2: return 0xFFFFU;
    default: return 0xFFFFFFFFU;
    }
}

static void tim3_sync_counter(TIM3State *s)
{
    if ((s->cr1 & TIM3_CR1_CEN) == 0) {
        return;
    }

    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    if (now <= s->last_sync_ns) {
        return;
    }

    uint64_t tick_period_ns = (uint64_t)(s->psc + 1U) * TIM3_BASE_TICK_NS;
    if (tick_period_ns == 0) {
        tick_period_ns = 1;
    }

    uint64_t elapsed_ns = now - s->last_sync_ns;
    uint64_t ticks = elapsed_ns / tick_period_ns;
    if (ticks == 0) {
        return;
    }

    uint32_t modulo = (uint32_t)s->arr + 1U; /* 1..65536 */
    if (modulo == 0) {
        modulo = 0x10000U;
    }

    s->cnt = (uint16_t)(((uint32_t)s->cnt + (uint32_t)(ticks % modulo)) % modulo);
    s->last_sync_ns += ticks * tick_period_ns;
}

static void tim3_apply_update_event(TIM3State *s)
{
    /*
     * Minimal useful UG behavior for this trace:
     * - reinitialize the counter
     * - real hardware also reloads prescaler/shadowed regs and may set flags,
     *   but that was not observed in the provided data.
     */
    s->cnt = 0;
    s->last_sync_ns = (uint64_t)qemu_plugin_get_virtual_timer();
}

static uint64_t tim3_read_reg(TIM3State *s, hwaddr offset)
{
    switch (offset) {
    case TIM3_CR1_OFFSET:
        return s->cr1;
    case TIM3_CR2_OFFSET:
        return s->cr2;
    case TIM3_SMCR_OFFSET:
        return s->smcr;
    case TIM3_DIER_OFFSET:
        return s->dier;
    case TIM3_SR_OFFSET:
        return s->sr;
    case TIM3_EGR_OFFSET:
        /* EGR is effectively write-only; reads return 0 in this model. */
        return 0;
    case TIM3_CCMR1_OFFSET:
        /*
         * Trace evidence shows CCMR1_Output always reads back as 0 on the
         * exercised path, even after writes. Returning the latched value breaks
         * the firmware's RMW sequence and causes spurious accumulation of mode
         * bits (e.g. 0x60 -> 0x68). Keep internal state for completeness, but
         * expose read-as-zero to the guest.
         */
        return 0;
    case TIM3_CCER_OFFSET:
        return s->ccer;
    case TIM3_CNT_OFFSET:
        tim3_sync_counter(s);
        return s->cnt;
    case TIM3_PSC_OFFSET:
        return s->psc;
    case TIM3_ARR_OFFSET:
        return s->arr;
    default:
        return 0;
    }
}

// This function will emulate all device reads
uint64_t tim3_read(void *opaque, hwaddr addr, unsigned size)
{
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint64_t val = tim3_read_reg(s, offset);

    val &= tim3_mask_for_size(size);

    return val;
}

// This function will emulate all device writes
void tim3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint32_t v = (uint32_t)(value & tim3_mask_for_size(size));
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();

    switch (offset) {
    case TIM3_CR1_OFFSET: {
        bool was_enabled = (s->cr1 & TIM3_CR1_CEN) != 0;
        if (was_enabled) {
            tim3_sync_counter(s);
        }

        s->cr1 = (uint16_t)(v & 0xFFFFU);

        bool now_enabled = (s->cr1 & TIM3_CR1_CEN) != 0;
        if (!was_enabled && now_enabled) {
            s->last_sync_ns = now;
        } else if (was_enabled && !now_enabled) {
            s->last_sync_ns = now;
        }
        break;
    }

    case TIM3_CR2_OFFSET:
        s->cr2 = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_SMCR_OFFSET:
        s->smcr = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_DIER_OFFSET:
        s->dier = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_SR_OFFSET:
        /*
         * Minimal RW behavior. Real TIM status registers have write-to-clear
         * semantics for many bits, but no such accesses were observed.
         */
        s->sr = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_EGR_OFFSET:
        s->egr = (uint16_t)(v & 0xFFFFU);
        if (v & TIM3_EGR_UG) {
            tim3_apply_update_event(s);
        }
        break;

    case TIM3_CCMR1_OFFSET:
        /*
         * Preserve the last programmed value internally, but guest-visible
         * reads use read-as-zero behavior to match the observed hardware.
         */
        s->ccmr1 = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_CCER_OFFSET:
        s->ccer = (uint16_t)(v & 0xFFFFU);
        break;

    case TIM3_CNT_OFFSET:
        tim3_sync_counter(s);
        s->cnt = (uint16_t)(v & 0xFFFFU);
        s->last_sync_ns = now;
        break;

    case TIM3_PSC_OFFSET:
        tim3_sync_counter(s);
        s->psc = (uint16_t)(v & 0xFFFFU);
        s->last_sync_ns = now;
        break;

    case TIM3_ARR_OFFSET:
        tim3_sync_counter(s);
        s->arr = (uint16_t)(v & 0xFFFFU);
        if (s->cnt > s->arr) {
            s->cnt = 0;
        }
        s->last_sync_ns = now;
        break;

    default:
        tim3_debug("TIM3: unhandled write addr=0x%llx offset=0x%llx value=0x%llx size=%u",
                   (unsigned long long)addr,
                   (unsigned long long)offset,
                   (unsigned long long)value,
                   size);
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* tim3_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_tim3, 0, sizeof(g_tim3));

    /*
     * Reset-like defaults for a basic STM32 general-purpose timer.
     * ARR reset value is typically all-ones for a 16-bit timer.
     */
    g_tim3.cr1 = 0x0000;
    g_tim3.cr2 = 0x0000;
    g_tim3.smcr = 0x0000;
    g_tim3.dier = 0x0000;
    g_tim3.sr = 0x0000;
    g_tim3.egr = 0x0000;
    g_tim3.ccmr1 = 0x0000;
    g_tim3.ccer = 0x0000;
    g_tim3.cnt = 0x0000;
    g_tim3.psc = 0x0000;
    g_tim3.arr = 0xFFFF;
    g_tim3.last_sync_ns = (uint64_t)qemu_plugin_get_virtual_timer();

    return &g_tim3;
}