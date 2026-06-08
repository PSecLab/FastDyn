#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>

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
    bool irq_latched;
    uint64_t compare_timer;
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

static void tim5_maybe_raise_irq(TIM5State *s) {
    bool pending = false;

    if ((s->dier & TIM_DIER_UIE) && (s->sr & TIM_SR_UIF)) {
        pending = true;
    }
    if ((s->dier & TIM_DIER_CC1IE) && (s->sr & TIM_SR_CC1IF)) {
        pending = true;
    }

    if (pending && !s->irq_latched) {
        qemu_plugin_raise_irq(TIM5_IRQ_LINE, false);
        s->irq_latched = true;
    }

    if (!pending) {
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

static void tim5_schedule_compare(TIM5State *s) {
    uint64_t now;
    uint64_t hz;
    uint64_t period;
    uint64_t delta_ticks;
    uint64_t delay_ns;
    uint32_t curr;
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
    if (hz == 0) {
        return;
    }

    period = tim5_get_period(s);
    curr = s->cnt;
    target = s->ccr1;

    if (curr == target) {
        s->sr |= TIM_SR_CC1IF;
        tim5_maybe_raise_irq(s);
        return;
    }

    if (target > curr) {
        delta_ticks = (uint64_t)target - (uint64_t)curr;
    } else {
        delta_ticks = (period - (uint64_t)curr) + (uint64_t)target;
    }

    delay_ns = (delta_ticks * 1000000000ULL + hz - 1ULL) / hz;
    if (delay_ns == 0) {
        delay_ns = 1;
    }

    now = (uint64_t)qemu_plugin_get_virtual_timer();
    qemu_plugin_timer_alarm(s->compare_timer, now + delay_ns);
}

static void tim5_sync_counter(TIM5State *s) {
    int64_t now = qemu_plugin_get_virtual_timer();
    uint64_t hz;
    uint64_t period;
    uint64_t delta_ns;
    uint64_t numer;
    uint64_t ticks;
    uint64_t total;
    uint32_t old_cnt;
    uint32_t new_cnt;

    if (!(s->cr1 & TIM_CR1_CEN)) {
        s->last_ns = now;
        s->frac_ns = 0;
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
        tim5_maybe_raise_irq(s);
        return;
    }

    delta_ns = (uint64_t)(now - s->last_ns);
    numer = s->frac_ns + (delta_ns * hz);
    ticks = numer / 1000000000ULL;
    s->frac_ns = numer % 1000000000ULL;
    s->last_ns = now;

    if (ticks == 0) {
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
        tim5_sync_counter(s);
        /* TIMx_SR bits are cleared by writing 0, preserved by writing 1. */
        s->sr &= v;
        tim5_maybe_raise_irq(s);
        tim5_schedule_compare(s);
        break;

    case TIM5_EGR:
        if (v & TIM_EGR_UG) {
            s->cnt = 0;
            s->sr |= TIM_SR_UIF;
            if (s->ccr1 == 0 && s->ccr1 <= s->arr) {
                s->sr |= TIM_SR_CC1IF;
            }
            s->last_ns = qemu_plugin_get_virtual_timer();
            s->frac_ns = 0;
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
        tim5_schedule_compare(s);
        break;

    case TIM5_PSC:
        tim5_sync_counter(s);
        s->psc = v & 0xFFFFU;
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        tim5_schedule_compare(s);
        break;

    case TIM5_ARR:
        tim5_sync_counter(s);
        s->arr = v;
        s->cnt = tim5_normalize_cnt(s, s->cnt);
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac_ns = 0;
        tim5_schedule_compare(s);
        break;

    case TIM5_CCR1:
        tim5_sync_counter(s);
        s->ccr1 = v;
        tim5_schedule_compare(s);
        break;

    default:
        break;
    }
}