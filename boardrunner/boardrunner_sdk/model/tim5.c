#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define TIM5_BASE           0x40000C00ULL

#define TIM5_CR1            0x00
#define TIM5_CR2            0x04
#define TIM5_DIER           0x0C
#define TIM5_SR             0x10
#define TIM5_EGR            0x14
#define TIM5_CCMR1          0x18
#define TIM5_CNT            0x24
#define TIM5_PSC            0x28
#define TIM5_ARR            0x2C
#define TIM5_CCR1           0x34

#define TIM_CR1_CEN         (1U << 0)
#define TIM_DIER_UIE        (1U << 0)
#define TIM_DIER_CC1IE      (1U << 1)
#define TIM_SR_UIF          (1U << 0)
#define TIM_SR_CC1IF        (1U << 1)
#define TIM_EGR_UG          (1U << 0)

/*
 * STM32F427 TIM5 runs from the APB1 timer clock.
 * For this firmware ST_CLOCK_SRC is 84 MHz and PSC is programmed to 83,
 * yielding a 1 MHz free-running system timer.
 */
#define TIM5_INPUT_CLK_HZ   84000000ULL

/* TIM5 global IRQ is 50 on STM32F427, API requires IRQn + 16. */
#define TIM5_IRQ_LINE       (50 + 16)

typedef struct TIM5State {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t dier;
    uint32_t sr;
    uint32_t ccmr1;
    uint32_t ccr1;
    uint32_t psc;
    uint32_t arr;
    uint32_t cnt;
    int64_t last_ns;
    uint64_t frac_ns;
    bool irq_latched;   /* suppress repeat pulses while the masked IRQ request stays high */
    uint64_t compare_timer;
    uint64_t synthetic_tick_debt;
    int64_t last_cnt_read_ns;
} TIM5State;

static TIM5State g_tim5;

static uint64_t tim5_mask_by_size(uint64_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xFFULL;
    case 2:
        return value & 0xFFFFULL;
    case 4:
    default:
        return value & 0xFFFFFFFFULL;
    }
}

static uint64_t tim5_get_tick_hz(TIM5State *s) {
    return TIM5_INPUT_CLK_HZ / ((uint64_t)s->psc + 1ULL);
}

static uint64_t tim5_get_period(TIM5State *s) {
    return (uint64_t)s->arr + 1ULL;
}

static uint32_t tim5_normalize_cnt(TIM5State *s, uint32_t value) {
    uint64_t period = tim5_get_period(s);

    if (period == 0) {
        return value;
    }

    return (uint32_t)((uint64_t)value % period);
}

static uint32_t tim5_irq_request_mask(TIM5State *s) {
    return s->sr & s->dier & (TIM_SR_UIF | TIM_SR_CC1IF);
}

static void tim5_maybe_raise_irq(TIM5State *s) {
    uint32_t request = tim5_irq_request_mask(s);

    /*
     * The STM32 timer IRQ request is governed by the live masked condition
     * (SR & DIER). The framework only exposes an edge-style raise API, so we
     * emit one pulse when that masked request transitions from idle to active
     * and re-arm as soon as either firmware clears the SR flag or masks the
     * source in DIER. This prevents stale compare interrupts from surviving
     * DIER changes and re-entering st_lld_serve_interrupt with incoherent
     * SR/DIER state.
     */
    if (request != 0U) {
        if (!s->irq_latched) {
            qemu_plugin_raise_irq(TIM5_IRQ_LINE, false);
            s->irq_latched = true;
        }
    } else {
        s->irq_latched = false;
    }
}

static bool tim5_compare_crossed(uint32_t old_cnt,
                                 uint32_t new_cnt,
                                 uint32_t target,
                                 uint64_t period,
                                 uint64_t ticks) {
    if (ticks == 0) {
        return false;
    }

    if (ticks >= period) {
        return true;
    }

    if (new_cnt >= old_cnt) {
        return (old_cnt < target) && (target <= new_cnt);
    }

    return (target > old_cnt) || (target <= new_cnt);
}

static void tim5_advance_ticks(TIM5State *s, uint64_t ticks) {
    uint64_t period;
    uint64_t total;
    uint32_t old_cnt;
    uint32_t new_cnt;

    if (ticks == 0U) {
        tim5_maybe_raise_irq(s);
        return;
    }

    period = tim5_get_period(s);
    old_cnt = s->cnt;
    total = (uint64_t)old_cnt + ticks;
    new_cnt = (uint32_t)(total % period);

    if (total >= period) {
        s->sr |= TIM_SR_UIF;
    }

    if ((s->ccr1 <= s->arr) &&
        tim5_compare_crossed(old_cnt, new_cnt, s->ccr1, period, ticks)) {
        s->sr |= TIM_SR_CC1IF;
    }

    s->cnt = new_cnt;
    tim5_maybe_raise_irq(s);
}

static void tim5_maybe_step_on_cnt_read(TIM5State *s) {
    int64_t now;

    if (!(s->cr1 & TIM_CR1_CEN)) {
        s->last_cnt_read_ns = -1;
        return;
    }

    now = qemu_plugin_get_virtual_timer();
    if (now == s->last_cnt_read_ns) {
        /*
         * Virtual time only advances at TB boundaries. If firmware spins on
         * CNT inside one translated block, repeated reads can otherwise return
         * a frozen value forever. Advance one synthetic timer tick per same-TB
         * CNT reread, then pay that tick back from future real elapsed time.
         */
        s->synthetic_tick_debt++;
        tim5_advance_ticks(s, 1U);
    }

    s->last_cnt_read_ns = now;
}

static void tim5_peek_counter(TIM5State *s,
                              int64_t *now_ns_out,
                              uint32_t *cnt_out,
                              uint64_t *ticks_out,
                              uint64_t *debt_out) {
    int64_t now = qemu_plugin_get_virtual_timer();
    uint32_t cnt = s->cnt;
    uint64_t visible_ticks = 0;
    uint64_t remaining_debt = s->synthetic_tick_debt;

    if ((s->cr1 & TIM_CR1_CEN) && (now > s->last_ns)) {
        uint64_t hz = tim5_get_tick_hz(s);

        if (hz != 0U) {
            uint64_t period = tim5_get_period(s);
            uint64_t delta_ns = (uint64_t)(now - s->last_ns);
            uint64_t numer = s->frac_ns + (delta_ns * hz);
            uint64_t real_ticks = numer / 1000000000ULL;

            if (real_ticks != 0U) {
                if (remaining_debt != 0U) {
                    if (real_ticks >= remaining_debt) {
                        visible_ticks = real_ticks - remaining_debt;
                        remaining_debt = 0U;
                    } else {
                        remaining_debt -= real_ticks;
                    }
                } else {
                    visible_ticks = real_ticks;
                }

                if (visible_ticks != 0U) {
                    cnt = (uint32_t)(((uint64_t)s->cnt + visible_ticks) % period);
                }
            }
        }
    }

    if (now_ns_out != NULL) {
        *now_ns_out = now;
    }
    if (cnt_out != NULL) {
        *cnt_out = cnt;
    }
    if (ticks_out != NULL) {
        *ticks_out = visible_ticks;
    }
    if (debt_out != NULL) {
        *debt_out = remaining_debt;
    }
}

static void tim5_schedule_compare(TIM5State *s) {
    int64_t now_ns;
    uint64_t now;
    uint64_t hz;
    uint64_t period;
    uint64_t elapsed_ticks;
    uint64_t remaining_debt;
    uint64_t delta_ticks;
    uint64_t delay_ticks;
    uint64_t delay_ns;
    uint32_t curr_now;
    uint32_t target;

    if (!(s->cr1 & TIM_CR1_CEN)) {
        return;
    }

    if (s->ccr1 > s->arr) {
        return;
    }

    if (s->sr & TIM_SR_CC1IF) {
        tim5_maybe_raise_irq(s);
        return;
    }

    hz = tim5_get_tick_hz(s);
    if (hz == 0U) {
        return;
    }

    period = tim5_get_period(s);
    target = s->ccr1;

    /*
     * Arm the host timer against a live estimate of CNT rather than the last
     * committed guest-visible snapshot. ChibiOS often programs CCR1 only a
     * handful of microseconds ahead of a CNT value it has just read; virtual
     * time can advance between that read and the later CCR1/DIER write, so
     * using s->cnt directly schedules the compare late.
     *
     * Also account for any synthetic CNT progress injected during same-TB
     * polling: guest-visible CNT may already be ahead of the virtual clock, so
     * some future real ticks must first "pay back" that lead before CNT moves
     * again.
     */
    tim5_peek_counter(s, &now_ns, &curr_now, &elapsed_ticks, &remaining_debt);

    /*
     * If the deadline has already been crossed since the last committed
     * snapshot, reflect the hardware state immediately. Also treat alarms
     * that are already numerically behind the live counter as stale unless
     * they are clearly the normal wrap-around future case (delta <= half a
     * timer period).
     */
    if (tim5_compare_crossed(s->cnt, curr_now, target, period, elapsed_ticks)) {
        s->sr |= TIM_SR_CC1IF;
        tim5_maybe_raise_irq(s);
        return;
    }

    delta_ticks = (((uint64_t)target + period) - (uint64_t)curr_now) % period;
    if (delta_ticks == 0U) {
        /*
         * Boot-time exception: st_lld_init leaves CCR1=0 while enabling the
         * free-running timer. Avoid synthesizing a spurious compare there.
         */
        if ((curr_now == 0U) && (target == 0U) && (elapsed_ticks == 0U)) {
            delta_ticks = period;
        } else {
            s->sr |= TIM_SR_CC1IF;
            tim5_maybe_raise_irq(s);
            return;
        }
    } else if (delta_ticks > (period / 2ULL)) {
        s->sr |= TIM_SR_CC1IF;
        tim5_maybe_raise_irq(s);
        return;
    }

    delay_ticks = delta_ticks + remaining_debt;
    delay_ns = (delay_ticks * 1000000000ULL + hz - 1ULL) / hz;
    if (delay_ns == 0U) {
        delay_ns = 1U;
    }

    now = (now_ns > 0) ? (uint64_t)now_ns : 0ULL;
    qemu_plugin_timer_alarm(s->compare_timer, now + delay_ns);
}

static void tim5_sync_counter(TIM5State *s) {
    int64_t now = qemu_plugin_get_virtual_timer();
    uint64_t hz;
    uint64_t delta_ns;
    uint64_t numer;
    uint64_t ticks;

    if (!(s->cr1 & TIM_CR1_CEN)) {
        s->last_ns = now;
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_maybe_raise_irq(s);
        return;
    }

    if (now <= s->last_ns) {
        tim5_maybe_raise_irq(s);
        return;
    }

    hz = tim5_get_tick_hz(s);
    if (hz == 0) {
        s->last_ns = now;
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_maybe_raise_irq(s);
        return;
    }

    delta_ns = (uint64_t)(now - s->last_ns);
    numer = s->frac_ns + (delta_ns * hz);
    ticks = numer / 1000000000ULL;
    s->frac_ns = numer % 1000000000ULL;
    s->last_ns = now;

    if ((ticks != 0U) && (s->synthetic_tick_debt != 0U)) {
        if (ticks >= s->synthetic_tick_debt) {
            ticks -= s->synthetic_tick_debt;
            s->synthetic_tick_debt = 0U;
        } else {
            s->synthetic_tick_debt -= ticks;
            ticks = 0U;
        }
    }

    if (ticks == 0U) {
        tim5_maybe_raise_irq(s);
        return;
    }

    tim5_advance_ticks(s, ticks);
}

static void tim5_compare_cb(void *opaque) {
    TIM5State *s = (TIM5State *)opaque;

    if (s == NULL) {
        return;
    }

    tim5_sync_counter(s);
    tim5_schedule_compare(s);
}

void* tim5_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_tim5, 0, sizeof(g_tim5));
    g_tim5.arr = 0xFFFFFFFFU;
    g_tim5.last_ns = qemu_plugin_get_virtual_timer();
    g_tim5.synthetic_tick_debt = 0;
    g_tim5.last_cnt_read_ns = -1;
    g_tim5.compare_timer = qemu_plugin_timer_new_ns(tim5_compare_cb, &g_tim5);

    return &g_tim5;
}

uint64_t tim5_read(void *opaque, uint64_t addr, unsigned size) {
    TIM5State *s = (TIM5State *)opaque;
    uint64_t offset = addr - TIM5_BASE;
    uint32_t value = 0;

    if (s == NULL) {
        s = &g_tim5;
    }

    s->last_cnt_read_ns = -1;

    switch (offset) {
    case TIM5_CR1:
        value = s->cr1;
        break;
    case TIM5_CR2:
        value = s->cr2;
        break;
    case TIM5_DIER:
        value = s->dier;
        break;
    case TIM5_SR:
        tim5_sync_counter(s);
        value = s->sr;
        break;
    case TIM5_EGR:
        value = 0;
        break;
    case TIM5_CCMR1:
        value = s->ccmr1;
        break;
    case TIM5_CNT:
        tim5_sync_counter(s);
        tim5_maybe_step_on_cnt_read(s);
        value = s->cnt;
        break;
    case TIM5_PSC:
        value = s->psc;
        break;
    case TIM5_ARR:
        value = s->arr;
        break;
    case TIM5_CCR1:
        value = s->ccr1;
        break;
    default:
        value = 0;
        break;
    }

    return tim5_mask_by_size(value, size);
}

void tim5_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    TIM5State *s = (TIM5State *)opaque;
    uint64_t offset = addr - TIM5_BASE;
    uint32_t v = (uint32_t)tim5_mask_by_size(value, size);

    if (s == NULL) {
        s = &g_tim5;
    }

    switch (offset) {
    case TIM5_CR1:
        tim5_sync_counter(s);
        s->cr1 = v;
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_maybe_raise_irq(s);
        tim5_schedule_compare(s);
        break;

    case TIM5_CR2:
        s->cr2 = v;
        break;

    case TIM5_DIER:
        tim5_sync_counter(s);
        s->dier = v;
        tim5_maybe_raise_irq(s);
        tim5_schedule_compare(s);
        break;

    case TIM5_SR:
        /*
         * TIMx_SR bits are cleared by writing 0, preserved by writing 1.
         *
         * Do not re-schedule against the old CCR1 value here. ChibiOS clears
         * CC1IF first and then immediately reads CNT / writes a fresh CCR1.
         * Re-arming from the acknowledge path can synthesize a ghost compare
         * from the already-serviced deadline, which later arrives as an IRQ
         * entry with SR==0 or DIER==0 after firmware has moved the alarm.
         */
        s->sr &= v;
        tim5_maybe_raise_irq(s);
        break;

    case TIM5_EGR:
        if (v & TIM_EGR_UG) {
            /*
             * ChibiOS uses UG during init to force the prescaler/counter state
             * to reload before starting the free-running timer. In the trace
             * this does not leave SR pending bits set, so model the effect as
             * a counter reload only.
             */
            s->cnt = 0;
            s->last_ns = qemu_plugin_get_virtual_timer();
            s->frac_ns = 0;
            s->synthetic_tick_debt = 0;
            tim5_maybe_raise_irq(s);
            tim5_schedule_compare(s);
        }
        break;

    case TIM5_CCMR1:
        s->ccmr1 = v;
        break;

    case TIM5_CNT:
        tim5_sync_counter(s);
        s->cnt = tim5_normalize_cnt(s, v);
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_schedule_compare(s);
        break;

    case TIM5_PSC:
        tim5_sync_counter(s);
        s->psc = v & 0xFFFFU;
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_schedule_compare(s);
        break;

    case TIM5_ARR:
        tim5_sync_counter(s);
        s->arr = v;
        s->cnt = tim5_normalize_cnt(s, s->cnt);
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        s->synthetic_tick_debt = 0;
        tim5_schedule_compare(s);
        break;

    case TIM5_CCR1:
        /*
         * Do not commit a fresh CNT snapshot here.
         *
         * Firmware often derives CCR1 from a CNT value it read only a few
         * instructions earlier, especially inside the TIM5 IRQ handler.
         * Re-advancing the architected counter at the write point perturbs
         * that guest-visible sequence and can fabricate exact-match races.
         *
         * tim5_schedule_compare() still peeks the live timer so host-side
         * compare scheduling tracks real current time and already-missed
         * deadlines, it just does so without changing the CNT value the guest
         * most recently observed.
         */
        s->ccr1 = v;
        /*
         * ChibiOS reprograms CCR1 immediately after acknowledging the previous
         * compare. Drop any stale CC1IF from that old deadline so the new
         * target starts from a clean compare state; if the new target is
         * already in the past, tim5_schedule_compare() will re-assert CC1IF
         * synchronously.
         */
        s->sr &= ~TIM_SR_CC1IF;
        tim5_maybe_raise_irq(s);
        tim5_schedule_compare(s);
        break;

    default:
        break;
    }
}