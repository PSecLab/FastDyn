// Device Model for TIM3

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// CR1  - control register 1 (CEN/UDIS/URS relevant)
// DIER - DMA/interrupt enable register (UIE relevant)
// SR   - status register (UIF clear-on-write-zero semantics inferred)
// EGR  - event generation register (UG relevant)
// CNT  - live counter
// PSC  - prescaler
// ARR  - auto-reload

#define TIM3_BASE              0x40000400ULL

#define TIM3_CR1_OFFSET        0x00
#define TIM3_DIER_OFFSET       0x0C
#define TIM3_SR_OFFSET         0x10
#define TIM3_EGR_OFFSET        0x14
#define TIM3_CNT_OFFSET        0x24
#define TIM3_PSC_OFFSET        0x28
#define TIM3_ARR_OFFSET        0x2C

/* STM32 TIM bit definitions used by this model */
#define TIM_CR1_CEN            (1U << 0)
#define TIM_CR1_UDIS           (1U << 1)
#define TIM_CR1_URS            (1U << 2)

#define TIM_DIER_UIE           (1U << 0)

#define TIM_SR_UIF             (1U << 0)

#define TIM_EGR_UG             (1U << 0)

/*
 * Observed runtime reads show SR=0x1F before clearing UIF and 0x1E after.
 * To preserve that behavior, keep bits [4:1] as observed-sticky read-only ones.
 */
#define TIM3_SR_OBSERVED_STICKY 0x1EU

/*
 * TIM3 on STM32H753x is a 16-bit general-purpose timer clocked from the APB1
 * timer domain. The observed PSC=19999 and ARR=9999 programming strongly
 * suggests a 1 Hz update period with a 200 MHz timer kernel clock:
 *
 *   200 MHz / (19999 + 1) / (9999 + 1) = 1 Hz
 *
 * Using 240 MHz makes CNT run ahead of hardware and causes UIF to be
 * reasserted too early after firmware clears SR, collapsing the observed
 * 0x1F/0x1E alternation into mostly 0x1F reads.
 */
#define TIM3_INPUT_CLOCK_HZ    200000000ULL

#define TIM3_16BIT_MASK        0xFFFFU

/*
 * TIM3_IRQn on STM32H7 is 29. Framework requires interrupt number + 16.
 */
#define TIM3_IRQN              29
#define TIM3_RAISE_VECTOR      (TIM3_IRQN + 16)

/* When disabled, park the one-shot timer far in the future. */
#define TIM3_PARK_DELAY_NS     (24ULL * 60ULL * 60ULL * 1000000000ULL)

typedef struct {
    uint32_t cr1;
    uint32_t dier;
    uint32_t sr;          // mutable status bits; observed sticky bits are added on read
    uint32_t psc;
    uint32_t arr;

    uint32_t cnt_latched; // counter value at base_time_ns
    uint64_t base_time_ns;

    uint64_t timer_handle;
    uint64_t next_deadline_ns;
    bool irq_level;       // internal shadow of (DIER.UIE && SR.UIF)
} TIM3State;

static TIM3State g_tim3;

static uint32_t tim3_mask_for_size(unsigned size)
{
    switch (size) {
    case 1: return 0xFFU;
    case 2: return 0xFFFFU;
    default: return 0xFFFFFFFFU;
    }
}

static uint64_t tim3_now_ns(void)
{
    int64_t now = qemu_plugin_get_virtual_timer();
    return (now < 0) ? 0 : (uint64_t)now;
}

static uint64_t tim3_div_ceil_u128(__uint128_t num, uint64_t den)
{
    return (uint64_t)((num + den - 1) / den);
}

static uint64_t tim3_modulus(const TIM3State *s)
{
    return ((uint64_t)(s->arr & TIM3_16BIT_MASK)) + 1ULL;
}

static uint32_t tim3_compute_cnt(TIM3State *s, uint64_t now_ns)
{
    uint64_t mod = tim3_modulus(s);

    if ((s->cr1 & TIM_CR1_CEN) == 0) {
        return (uint32_t)((s->cnt_latched & TIM3_16BIT_MASK) % mod);
    }

    __uint128_t elapsed_ns = (__uint128_t)(now_ns - s->base_time_ns);
    __uint128_t timer_ticks = (elapsed_ns * TIM3_INPUT_CLOCK_HZ) / 1000000000ULL;
    uint64_t counter_steps =
        (uint64_t)(timer_ticks / (((uint64_t)(s->psc & TIM3_16BIT_MASK)) + 1ULL));

    return (uint32_t)(((s->cnt_latched & TIM3_16BIT_MASK) + counter_steps) % mod);
}

static void tim3_sync_counter(TIM3State *s, uint64_t now_ns)
{
    s->cnt_latched = tim3_compute_cnt(s, now_ns);
    s->base_time_ns = now_ns;
}

static uint64_t tim3_ns_until_update(TIM3State *s, uint64_t now_ns)
{
    uint64_t mod = tim3_modulus(s);
    uint32_t cnt = tim3_compute_cnt(s, now_ns);
    uint64_t remaining_counts;

    if (mod == 0) {
        return 1;
    }

    remaining_counts = mod - (uint64_t)cnt;
    if (remaining_counts == 0) {
        remaining_counts = mod;
    }

    __uint128_t timer_ticks = (__uint128_t)remaining_counts * ((uint64_t)s->psc + 1ULL);
    uint64_t delay = tim3_div_ceil_u128(timer_ticks * 1000000000ULL, TIM3_INPUT_CLOCK_HZ);

    return (delay == 0) ? 1 : delay;
}

static void tim3_rearm(TIM3State *s)
{
    uint64_t now_ns = tim3_now_ns();

    if ((s->cr1 & TIM_CR1_CEN) == 0) {
        s->next_deadline_ns = now_ns + TIM3_PARK_DELAY_NS;
        qemu_plugin_timer_alarm(s->timer_handle, s->next_deadline_ns);
        return;
    }

    /* Latch current counter so future timing is relative to "now". */
    tim3_sync_counter(s, now_ns);

    s->next_deadline_ns = now_ns + tim3_ns_until_update(s, now_ns);
    qemu_plugin_timer_alarm(s->timer_handle, s->next_deadline_ns);
}

static bool tim3_irq_pending(const TIM3State *s)
{
    return ((s->dier & TIM_DIER_UIE) != 0) && ((s->sr & TIM_SR_UIF) != 0);
}

static void tim3_refresh_irq(TIM3State *s)
{
    bool pending = tim3_irq_pending(s);

    /*
     * The hardware interrupt source is level-like: enabling UIE while UIF is
     * already set must immediately make the interrupt pending. The framework
     * only offers an IRQ raise primitive, so we track the logical level and
     * emit a pulse on false->true transitions.
     */
    if (pending && !s->irq_level) {
        qemu_plugin_raise_irq(TIM3_RAISE_VECTOR, false);
    }

    s->irq_level = pending;
}

static void tim3_do_update_event(TIM3State *s, bool request_irq)
{
    uint64_t now_ns = tim3_now_ns();

    s->cnt_latched = 0;
    s->base_time_ns = now_ns;

    if ((s->cr1 & TIM_CR1_UDIS) == 0) {
        s->sr |= TIM_SR_UIF;
        if (request_irq) {
            tim3_refresh_irq(s);
        } else {
            s->irq_level = tim3_irq_pending(s);
        }
    }

    tim3_rearm(s);
}

static void tim3_timer_cb(void *opaque)
{
    TIM3State *s = (TIM3State *)opaque;
    uint64_t now_ns = tim3_now_ns();

    if ((s->cr1 & TIM_CR1_CEN) == 0) {
        return;
    }

    /*
     * Ignore stale callbacks if the timer was re-armed after configuration changes.
     */
    if (now_ns + 1000ULL < s->next_deadline_ns) {
        return;
    }

    tim3_do_update_event(s, true);
}

static uint64_t tim3_read_reg32(uint32_t value, unsigned size)
{
    if (size >= 4) {
        return value;
    }
    return value & tim3_mask_for_size(size);
}

static void tim3_write_reg32(uint32_t *reg, uint64_t value, unsigned size)
{
    uint32_t mask = tim3_mask_for_size(size);
    *reg = (*reg & ~mask) | ((uint32_t)value & mask);
}

// This function will emulate all device reads
uint64_t tim3_read(void *opaque, hwaddr addr, unsigned size) {
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint64_t now_ns = tim3_now_ns();

    switch (offset) {
    case TIM3_CR1_OFFSET:
        return tim3_read_reg32(s->cr1, size);

    case TIM3_DIER_OFFSET:
        return tim3_read_reg32(s->dier, size);

    case TIM3_SR_OFFSET:
        return tim3_read_reg32((s->sr & TIM_SR_UIF) | TIM3_SR_OBSERVED_STICKY, size);

    case TIM3_CNT_OFFSET:
        return tim3_read_reg32(tim3_compute_cnt(s, now_ns) & TIM3_16BIT_MASK, size);

    case TIM3_PSC_OFFSET:
        return tim3_read_reg32(s->psc & TIM3_16BIT_MASK, size);

    case TIM3_ARR_OFFSET:
        return tim3_read_reg32(s->arr & TIM3_16BIT_MASK, size);

    case TIM3_EGR_OFFSET:
        return 0;

    default:
        return 0;
    }
}

// This function will emulate all device writes
void tim3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint64_t now_ns = tim3_now_ns();

    switch (offset) {
    case TIM3_CR1_OFFSET: {
        uint32_t old_cr1 = s->cr1;
        bool old_cen = (old_cr1 & TIM_CR1_CEN) != 0;

        if (old_cen) {
            tim3_sync_counter(s, now_ns);
        }

        tim3_write_reg32(&s->cr1, value, size);

        if (((old_cr1 ^ s->cr1) & (TIM_CR1_CEN | TIM_CR1_UDIS | TIM_CR1_URS)) != 0) {
            if ((s->cr1 & TIM_CR1_CEN) != 0) {
                s->base_time_ns = now_ns;
            }
            tim3_rearm(s);
        }
        break;
    }

    case TIM3_DIER_OFFSET:
        tim3_write_reg32(&s->dier, value, size);
        /*
         * TIM interrupt request state depends on both DIER and SR.
         * If UIF was already pending when UIE becomes enabled, hardware makes
         * the IRQ pending immediately.
         */
        tim3_refresh_irq(s);
        break;

    case TIM3_SR_OFFSET: {
        /*
         * TIMx_SR uses rc_w0 semantics. Only UIF is modeled as mutable here;
         * the observed bits [4:1] remain read-only/high on readback.
         */
        uint32_t mask = tim3_mask_for_size(size);
        uint32_t clear_bits = (~(uint32_t)value) & mask & TIM_SR_UIF;
        s->sr &= ~clear_bits;
        tim3_refresh_irq(s);
        break;
    }

    case TIM3_EGR_OFFSET:
        if (((uint32_t)value & TIM_EGR_UG) != 0) {
            /*
             * UG resets/reloads the counter immediately.
             * With URS=1, UG should not generate update interrupt/flag.
             */
            s->cnt_latched = 0;
            s->base_time_ns = now_ns;

            if ((s->cr1 & TIM_CR1_URS) == 0) {
                if ((s->cr1 & TIM_CR1_UDIS) == 0) {
                    s->sr |= TIM_SR_UIF;
                    tim3_refresh_irq(s);
                }
            }

            tim3_rearm(s);
        }
        break;

    case TIM3_PSC_OFFSET:
        tim3_sync_counter(s, now_ns);
        tim3_write_reg32(&s->psc, value, size);
        s->psc &= TIM3_16BIT_MASK;
        tim3_rearm(s);
        break;

    case TIM3_ARR_OFFSET:
        tim3_sync_counter(s, now_ns);
        tim3_write_reg32(&s->arr, value, size);
        s->arr &= TIM3_16BIT_MASK;
        if ((s->cnt_latched & TIM3_16BIT_MASK) > s->arr) {
            s->cnt_latched = 0;
        }
        tim3_rearm(s);
        break;

    case TIM3_CNT_OFFSET:
        tim3_write_reg32(&s->cnt_latched, value, size);
        s->cnt_latched &= TIM3_16BIT_MASK;
        s->base_time_ns = now_ns;
        tim3_rearm(s);
        break;

    default:
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* tim3_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_tim3, 0, sizeof(g_tim3));

    /*
     * Reset-like defaults plus trace-informed behavior.
     * TIM3 is a 16-bit general-purpose timer; ARR reset is typically 0xFFFF.
     */
    g_tim3.psc = 0x0000;
    g_tim3.arr = 0xFFFF;
    g_tim3.cr1 = 0x0000;
    g_tim3.dier = 0x0000;
    g_tim3.sr = 0x0000;
    g_tim3.cnt_latched = 0;
    g_tim3.base_time_ns = tim3_now_ns();

    g_tim3.irq_level = false;

    g_tim3.timer_handle = qemu_plugin_timer_new_ns(tim3_timer_cb, &g_tim3);
    tim3_rearm(&g_tim3);

    return &g_tim3;
}