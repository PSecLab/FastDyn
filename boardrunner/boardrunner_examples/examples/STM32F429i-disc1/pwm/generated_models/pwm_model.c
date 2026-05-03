// Device Model for TIM3
//
// Inferred Register Functions:
//   0x00 CR1   - Counter enable, ARPE, basic timer control
//   0x0C DIER  - Interrupt enable (UIE handled)
//   0x10 SR    - Status register (UIF handled)
//   0x14 EGR   - Update generation (UG handled)
//   0x18 CCMR1 - Channel 1 PWM mode / preload
//   0x20 CCER  - Channel enable/polarity
//   0x24 CNT   - Counter
//   0x28 PSC   - Prescaler preload
//   0x2C ARR   - Auto-reload
//   0x34 CCR1  - Compare register for channel 1
//
// Notes:
// - Absolute MMIO addresses are used by the framework, so this model subtracts
//   the TIM3 base before decoding registers.
// - This model implements a minimal but stateful timer.
// - Because no RCC/clock-query API is available, the timer runs from a fixed
//   emulation clock chosen for functional behavior rather than cycle accuracy.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define TIM3_BASE               0x40000400ULL
#define TIM3_SIZE               0x400

// STM32F429: TIM3 global interrupt is IRQn 29.
// Framework requires raising interrupt number + 16.
#define TIM3_IRQ_NUM            29

// Functional emulation clock for TIM3 input.
// No clock-tree API is available, so keep this simple and deterministic.
#define TIM3_INPUT_CLK_HZ       1000000ULL

// Register offsets
#define TIM_CR1_OFFSET          0x00
#define TIM_CR2_OFFSET          0x04
#define TIM_SMCR_OFFSET         0x08
#define TIM_DIER_OFFSET         0x0C
#define TIM_SR_OFFSET           0x10
#define TIM_EGR_OFFSET          0x14
#define TIM_CCMR1_OFFSET        0x18
#define TIM_CCMR2_OFFSET        0x1C
#define TIM_CCER_OFFSET         0x20
#define TIM_CNT_OFFSET          0x24
#define TIM_PSC_OFFSET          0x28
#define TIM_ARR_OFFSET          0x2C
#define TIM_CCR1_OFFSET         0x34
#define TIM_CCR2_OFFSET         0x38
#define TIM_CCR3_OFFSET         0x3C
#define TIM_CCR4_OFFSET         0x40
#define TIM_DCR_OFFSET          0x48
#define TIM_DMAR_OFFSET         0x4C

// CR1 bits
#define TIM_CR1_CEN             (1u << 0)
#define TIM_CR1_UDIS            (1u << 1)
#define TIM_CR1_URS             (1u << 2)
#define TIM_CR1_OPM             (1u << 3)
#define TIM_CR1_DIR             (1u << 4)
#define TIM_CR1_ARPE            (1u << 7)
#define TIM_CR1_VALID_MASK      0x03FFu

// DIER bits
#define TIM_DIER_UIE            (1u << 0)

// SR bits
#define TIM_SR_UIF              (1u << 0)

// EGR bits
#define TIM_EGR_UG              (1u << 0)

// CCMR1 bits for channel 1 output
#define TIM_CCMR1_CC1S_MASK     (0x3u << 0)
#define TIM_CCMR1_OC1PE         (1u << 3)
#define TIM_CCMR1_OC1M_MASK     (0x7u << 4)

// CCER bits
#define TIM_CCER_CC1E           (1u << 0)

typedef struct TIM3State {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t smcr;
    uint32_t dier;
    uint32_t sr;
    uint32_t ccmr1;
    uint32_t ccmr2;
    uint32_t ccer;
    uint32_t cnt;

    uint32_t psc_preload;
    uint32_t psc_active;

    uint32_t arr_preload;
    uint32_t arr_active;

    uint32_t ccr1_preload;
    uint32_t ccr1_active;

    uint32_t ccr2;
    uint32_t ccr3;
    uint32_t ccr4;
    uint32_t dcr;
    uint32_t dmar;

    uint64_t last_sync_ns;
    uint64_t frac_accum;   // remainder in "Hz*ns" units modulo 1e9
    uint64_t irq_timer;
} TIM3State;

static TIM3State g_tim3;

static uint64_t tim3_now_ns(void) {
    int64_t now = qemu_plugin_get_virtual_timer();
    return (now < 0) ? 0 : (uint64_t)now;
}

static uint32_t tim3_mask_by_size(uint32_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xFFu;
    case 2:
        return value & 0xFFFFu;
    default:
        return value;
    }
}

static uint32_t tim3_counter_hz(TIM3State *s) {
    uint32_t psc_div = (s->psc_active & 0xFFFFu) + 1u;
    uint32_t hz = (uint32_t)(TIM3_INPUT_CLK_HZ / psc_div);
    return hz ? hz : 1u;
}

static uint32_t tim3_period_counts(TIM3State *s) {
    return (s->arr_active & 0xFFFFu) + 1u;
}

static void tim3_raise_update_irq_if_enabled(TIM3State *s) {
    if ((s->dier & TIM_DIER_UIE) != 0) {
        qemu_plugin_raise_irq(TIM3_IRQ_NUM + 16, false);
    }
}

static void tim3_latch_preloads(TIM3State *s) {
    // PSC is always preloaded in STM32 timers and latched on update.
    s->psc_active = s->psc_preload & 0xFFFFu;

    // ARR only truly buffers when ARPE=1, but keeping active==preload when ARPE=0
    // is also correct for reads and later updates.
    s->arr_active = s->arr_preload & 0xFFFFu;

    // CCR1 preload depends on OC1PE; when disabled, writes already update active.
    if ((s->ccmr1 & TIM_CCMR1_OC1PE) != 0) {
        s->ccr1_active = s->ccr1_preload & 0xFFFFu;
    }
}

static void tim3_generate_update_event(TIM3State *s, bool reset_counter) {
    tim3_latch_preloads(s);

    if (reset_counter) {
        s->cnt = 0;
        s->frac_accum = 0;
    }

    if ((s->cr1 & TIM_CR1_UDIS) == 0) {
        s->sr |= TIM_SR_UIF;
        tim3_raise_update_irq_if_enabled(s);
    }

    if ((s->cr1 & TIM_CR1_OPM) != 0) {
        s->cr1 &= ~TIM_CR1_CEN;
    }
}

static void tim3_sync(TIM3State *s) {
    uint64_t now = tim3_now_ns();

    if (s->last_sync_ns == 0) {
        s->last_sync_ns = now;
        return;
    }

    if ((s->cr1 & TIM_CR1_CEN) == 0) {
        s->last_sync_ns = now;
        return;
    }

    if (now <= s->last_sync_ns) {
        return;
    }

    uint64_t delta_ns = now - s->last_sync_ns;
    uint32_t hz = tim3_counter_hz(s);
    uint32_t period = tim3_period_counts(s);

    unsigned __int128 accum = (unsigned __int128)delta_ns * (unsigned __int128)hz;
    accum += s->frac_accum;

    uint64_t ticks = (uint64_t)(accum / 1000000000ULL);
    s->frac_accum = (uint64_t)(accum % 1000000000ULL);
    s->last_sync_ns = now;

    if (ticks == 0) {
        return;
    }

    if ((s->cr1 & TIM_CR1_DIR) != 0) {
        // Minimal model: downcounting not evidenced in trace.
        // Keep behavior functional by treating it as upcounting.
    }

    if ((s->cr1 & TIM_CR1_OPM) != 0) {
        uint32_t remaining = period - (s->cnt % period);
        if (ticks >= remaining) {
            s->cnt = 0;
            tim3_generate_update_event(s, false);
            s->cr1 &= ~TIM_CR1_CEN;
            s->frac_accum = 0;
            return;
        }
    }

    uint64_t total = (uint64_t)(s->cnt & 0xFFFFu) + ticks;
    uint64_t wraps = total / period;
    s->cnt = (uint32_t)(total % period);

    if (wraps > 0) {
        tim3_latch_preloads(s);

        if ((s->cr1 & TIM_CR1_UDIS) == 0) {
            s->sr |= TIM_SR_UIF;
            tim3_raise_update_irq_if_enabled(s);
        }
    }
}

static void tim3_reschedule_irq(TIM3State *s) {
    if ((s->cr1 & TIM_CR1_CEN) == 0) {
        return;
    }
    if ((s->cr1 & TIM_CR1_UDIS) != 0) {
        return;
    }
    if ((s->dier & TIM_DIER_UIE) == 0) {
        return;
    }

    tim3_sync(s);

    uint32_t hz = tim3_counter_hz(s);
    uint32_t period = tim3_period_counts(s);
    uint32_t cnt = s->cnt % period;
    uint32_t ticks_left = period - cnt;

    unsigned __int128 target = (unsigned __int128)ticks_left * 1000000000ULL;
    unsigned __int128 need = (target > s->frac_accum) ? (target - s->frac_accum) : 0;
    uint64_t ns_until = (uint64_t)((need + hz - 1u) / hz);

    qemu_plugin_timer_alarm(s->irq_timer, tim3_now_ns() + ns_until);
}

static void tim3_irq_timer_cb(void *opaque) {
    TIM3State *s = (TIM3State *)opaque;
    tim3_sync(s);
    tim3_reschedule_irq(s);
}

// This function will emulate all device reads
uint64_t tim3_read(void *opaque, hwaddr addr, unsigned size) {
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint32_t value = 0;

    tim3_sync(s);

    switch (offset) {
    case TIM_CR1_OFFSET:
        value = s->cr1;
        break;
    case TIM_CR2_OFFSET:
        value = s->cr2;
        break;
    case TIM_SMCR_OFFSET:
        value = s->smcr;
        break;
    case TIM_DIER_OFFSET:
        value = s->dier;
        break;
    case TIM_SR_OFFSET:
        value = s->sr;
        break;
    case TIM_EGR_OFFSET:
        value = 0; // write-only
        break;
    case TIM_CCMR1_OFFSET:
        value = s->ccmr1;
        break;
    case TIM_CCMR2_OFFSET:
        value = s->ccmr2;
        break;
    case TIM_CCER_OFFSET:
        value = s->ccer;
        break;
    case TIM_CNT_OFFSET:
        value = s->cnt & 0xFFFFu;
        break;
    case TIM_PSC_OFFSET:
        value = s->psc_preload & 0xFFFFu;
        break;
    case TIM_ARR_OFFSET:
        value = s->arr_preload & 0xFFFFu;
        break;
    case TIM_CCR1_OFFSET:
        value = s->ccr1_preload & 0xFFFFu;
        break;
    case TIM_CCR2_OFFSET:
        value = s->ccr2 & 0xFFFFu;
        break;
    case TIM_CCR3_OFFSET:
        value = s->ccr3 & 0xFFFFu;
        break;
    case TIM_CCR4_OFFSET:
        value = s->ccr4 & 0xFFFFu;
        break;
    case TIM_DCR_OFFSET:
        value = s->dcr;
        break;
    case TIM_DMAR_OFFSET:
        value = s->dmar;
        break;
    default:
        value = 0;
        break;
    }

    return tim3_mask_by_size(value, size);
}

// This function will emulate all device writes
void tim3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    TIM3State *s = (TIM3State *)opaque;
    hwaddr offset = addr - TIM3_BASE;
    uint32_t v = tim3_mask_by_size((uint32_t)value, size);

    tim3_sync(s);

    switch (offset) {
    case TIM_CR1_OFFSET: {
        uint32_t old_cr1 = s->cr1;
        uint32_t new_cr1 = v & TIM_CR1_VALID_MASK;
        uint64_t now = tim3_now_ns();

        s->cr1 = new_cr1;

        if ((old_cr1 & TIM_CR1_CEN) == 0 && (new_cr1 & TIM_CR1_CEN) != 0) {
            s->last_sync_ns = now;
        } else if ((old_cr1 & TIM_CR1_CEN) != 0 && (new_cr1 & TIM_CR1_CEN) == 0) {
            s->last_sync_ns = now;
            s->frac_accum = 0;
        }
        break;
    }

    case TIM_CR2_OFFSET:
        s->cr2 = v & 0xFFFFu;
        break;

    case TIM_SMCR_OFFSET:
        s->smcr = v & 0xFFFFu;
        break;

    case TIM_DIER_OFFSET:
        s->dier = v & 0xFFFFu;
        break;

    case TIM_SR_OFFSET:
        // STM32 timer flags are generally cleared by writing 0 to the bit.
        if ((v & TIM_SR_UIF) == 0) {
            s->sr &= ~TIM_SR_UIF;
        }
        break;

    case TIM_EGR_OFFSET:
        if ((v & TIM_EGR_UG) != 0) {
            tim3_generate_update_event(s, true);
        }
        break;

    case TIM_CCMR1_OFFSET:
        s->ccmr1 = v & 0xFFFFu;
        if ((s->ccmr1 & TIM_CCMR1_OC1PE) == 0) {
            s->ccr1_active = s->ccr1_preload & 0xFFFFu;
        }
        break;

    case TIM_CCMR2_OFFSET:
        s->ccmr2 = v & 0xFFFFu;
        break;

    case TIM_CCER_OFFSET:
        s->ccer = v & 0xFFFFu;
        break;

    case TIM_CNT_OFFSET: {
        uint32_t period = tim3_period_counts(s);
        s->cnt = (period == 0) ? 0 : ((v & 0xFFFFu) % period);
        s->frac_accum = 0;
        s->last_sync_ns = tim3_now_ns();
        break;
    }

    case TIM_PSC_OFFSET:
        s->psc_preload = v & 0xFFFFu;
        break;

    case TIM_ARR_OFFSET:
        s->arr_preload = v & 0xFFFFu;
        if ((s->cr1 & TIM_CR1_ARPE) == 0) {
            s->arr_active = s->arr_preload;
            uint32_t period = tim3_period_counts(s);
            s->cnt %= period;
        }
        break;

    case TIM_CCR1_OFFSET:
        s->ccr1_preload = v & 0xFFFFu;
        if ((s->ccmr1 & TIM_CCMR1_OC1PE) == 0) {
            s->ccr1_active = s->ccr1_preload;
        }
        break;

    case TIM_CCR2_OFFSET:
        s->ccr2 = v & 0xFFFFu;
        break;

    case TIM_CCR3_OFFSET:
        s->ccr3 = v & 0xFFFFu;
        break;

    case TIM_CCR4_OFFSET:
        s->ccr4 = v & 0xFFFFu;
        break;

    case TIM_DCR_OFFSET:
        s->dcr = v & 0xFFFFu;
        break;

    case TIM_DMAR_OFFSET:
        s->dmar = v & 0xFFFFu;
        break;

    default:
        break;
    }

    tim3_reschedule_irq(s);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* tim3_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_tim3, 0, sizeof(g_tim3));

    // Reset values consistent with STM32 general-purpose timers.
    g_tim3.arr_preload = 0xFFFFu;
    g_tim3.arr_active  = 0xFFFFu;
    g_tim3.psc_preload = 0x0000u;
    g_tim3.psc_active  = 0x0000u;
    g_tim3.ccr1_preload = 0x0000u;
    g_tim3.ccr1_active  = 0x0000u;
    g_tim3.last_sync_ns = tim3_now_ns();
    g_tim3.frac_accum = 0;

    g_tim3.irq_timer = qemu_plugin_timer_new_ns(tim3_irq_timer_cb, &g_tim3);

    return &g_tim3;
}