// Device Model for TIM3 (STM32F7x9)
// Based ONLY on provided MMIO traces: CR1, PSC, ARR, EGR, CCMR1, CCER.
// Includes a lightweight optional counter/overflow path if firmware later enables CEN/UIE.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <device.h>
#include <boardrunner/vio.h>

// -------------------------
// TIM3 base + register map
// -------------------------
#define TIM3_BASE          0x40000400ULL

#define TIM3_CR1_OFS       0x00
#define TIM3_DIER_OFS      0x0C
#define TIM3_SR_OFS        0x10
#define TIM3_EGR_OFS       0x14
#define TIM3_CCMR1_OFS     0x18
#define TIM3_CCER_OFS      0x20
#define TIM3_CNT_OFS       0x24
#define TIM3_PSC_OFS       0x28
#define TIM3_ARR_OFS       0x2C

// -------------------------
// Bit definitions (subset)
// -------------------------
// CR1
#define CR1_CEN            (1u << 0)
#define CR1_UDIS           (1u << 1)
#define CR1_URS            (1u << 2)
#define CR1_OPM            (1u << 3)
#define CR1_DIR            (1u << 4)
#define CR1_CMS_MASK       (3u << 5)
#define CR1_ARPE           (1u << 7)

// DIER/SR
#define DIER_UIE           (1u << 0)
#define SR_UIF             (1u << 0)

// EGR
#define EGR_UG             (1u << 0)

// TIM3 interrupt number (STM32F769xx CMSIS header shows TIM3_IRQn = 29). :contentReference[oaicite:0]{index=0}
#define TIM3_IRQn          29

// NVIC external interrupt lines use IRQn + 16 per your rules.
#define TIM3_IRQ_LINE      (TIM3_IRQn + 16)

// -------------------------
// Timing model knob
// -------------------------
// We must not assume your exact RCC tree from the trace alone.
// This default is chosen because the trace values (PSC=0x2A2F, ARR=0x270F)
// are consistent with ~1s period if the TIM clock is ~108MHz.
#ifndef TIM3_INPUT_HZ
#define TIM3_INPUT_HZ      108000000ULL
#endif

// -------------------------
// Debug helper (dev_debug only)
// -------------------------
static void dbg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

typedef struct tim3_state {
    // Visible/control registers (as seen by MMIO)
    uint32_t cr1;
    uint32_t dier;
    uint32_t sr;

    uint32_t ccmr1;
    uint32_t ccer;

    // Counter and buffering
    uint32_t cnt;

    uint32_t psc_shadow;
    uint32_t arr_shadow;

    uint32_t psc_active;
    uint32_t arr_active;

    // Virtual time bookkeeping
    int64_t  last_ns;

    // One-shot overflow timer (optional)
    uint64_t overflow_timer;
    uint64_t next_overflow_ns; // for diagnostics
} tim3_state_t;

static tim3_state_t g_tim3;

// Compute tick period in ns using active prescaler and assumed input clock.
static uint64_t tim3_tick_ns(const tim3_state_t *s) {
    // tick = (PSC+1) / TIMCLK seconds
    // ns = 1e9 * (PSC+1) / TIMCLK
    uint64_t psc = (uint64_t)s->psc_active + 1ULL;
    if (TIM3_INPUT_HZ == 0) return 0;
    return (1000000000ULL * psc) / TIM3_INPUT_HZ;
}

static uint64_t tim3_period_ns(const tim3_state_t *s) {
    uint64_t tick = tim3_tick_ns(s);
    uint64_t arrp1 = (uint64_t)s->arr_active + 1ULL;
    if (tick == 0 || arrp1 == 0) return 0;
    // Guard overflow (very large values just saturate)
    if (arrp1 > (UINT64_MAX / tick)) return UINT64_MAX;
    return tick * arrp1;
}

static void tim3_arm_overflow(tim3_state_t *s) {
    // No cancel API: callback will self-check CEN and do nothing if disabled.
    if ((s->cr1 & CR1_CEN) == 0) {
        s->next_overflow_ns = 0;
        return;
    }

    uint64_t per = tim3_period_ns(s);
    if (per == 0 || per == UINT64_MAX) {
        s->next_overflow_ns = 0;
        return;
    }

    int64_t now = qemu_plugin_get_virtual_timer();
    if (now < 0) now = 0;

    uint64_t fire = (uint64_t)now + per;
    s->next_overflow_ns = fire;
    qemu_plugin_timer_alarm(s->overflow_timer, fire);

    dbg("[TIM3] arm overflow: now=%lld ns, period=%llu ns -> fire@%llu\n",
        (long long)now, (unsigned long long)per, (unsigned long long)fire);
}

static void tim3_set_uif_and_maybe_irq(tim3_state_t *s, bool from_ug) {
    // Respect UDIS (update disable): update events are not generated if UDIS=1.
    if (s->cr1 & CR1_UDIS) return;

    // Set UIF in SR regardless; firmware may poll SR later (even though not in trace).
    s->sr |= SR_UIF;

    // If URS=1, "update interrupt/DMA request" is typically restricted;
    // we conservatively suppress IRQ generation for software UG when URS=1.
    if (from_ug && (s->cr1 & CR1_URS)) {
        return;
    }

    if (s->dier & DIER_UIE) {
        // MUST use IRQn + 16 and pass false (non-secure).
        qemu_plugin_raise_irq(TIM3_IRQ_LINE, false);
    }
}

static void tim3_apply_update_event(tim3_state_t *s, bool from_ug) {
    // Apply buffered PSC always on update.
    s->psc_active = s->psc_shadow;

    // Apply buffered ARR depending on ARPE; if ARPE=0, ARR is "immediate" anyway.
    if (s->cr1 & CR1_ARPE) {
        s->arr_active = s->arr_shadow;
    } else {
        // If ARPE is off, we keep them identical.
        s->arr_active = s->arr_shadow;
    }

    // Reset counter on UG (common expectation).
    s->cnt = 0;

    // Mark update flag / maybe interrupt.
    tim3_set_uif_and_maybe_irq(s, from_ug);

    // Re-arm overflow if running.
    tim3_arm_overflow(s);
}

static void tim3_update_cnt_on_demand(tim3_state_t *s) {
    if ((s->cr1 & CR1_CEN) == 0) {
        s->last_ns = qemu_plugin_get_virtual_timer();
        return;
    }

    int64_t now = qemu_plugin_get_virtual_timer();
    if (now < 0) now = 0;

    if (s->last_ns <= 0) {
        s->last_ns = now;
        return;
    }

    uint64_t tick = tim3_tick_ns(s);
    if (tick == 0) {
        s->last_ns = now;
        return;
    }

    uint64_t dt = (now >= s->last_ns) ? (uint64_t)(now - s->last_ns) : 0ULL;
    uint64_t ticks = dt / tick;
    if (ticks == 0) return;

    uint64_t arrp1 = (uint64_t)s->arr_active + 1ULL;
    if (arrp1 == 0) return;

    uint64_t old = (uint64_t)s->cnt;
    uint64_t newv = old + ticks;

    // Wrap and detect overflow(s)
    uint64_t overflows = newv / arrp1;
    uint32_t new_cnt = (uint32_t)(newv % arrp1);

    s->cnt = new_cnt;
    s->last_ns += (int64_t)(ticks * tick);

    if (overflows > 0) {
        tim3_set_uif_and_maybe_irq(s, /*from_ug=*/false);
        // Keep periodic behavior consistent: arm next overflow from "now".
        tim3_arm_overflow(s);
    }
}

// Overflow callback (fires only if we armed it; self-checks CEN)
static void tim3_overflow_cb(void *opaque) {
    tim3_state_t *s = (tim3_state_t *)opaque;
    if (!s) return;

    if ((s->cr1 & CR1_CEN) == 0) return;

    // At overflow time: set UIF, reset CNT, and re-arm.
    s->cnt = 0;
    tim3_set_uif_and_maybe_irq(s, /*from_ug=*/false);

    tim3_arm_overflow(s);
}

// -------------------------
// MMIO read/write callbacks
// -------------------------
uint64_t tim3_read(void *opaque, hwaddr addr, unsigned size) {
    tim3_state_t *s = (tim3_state_t *)opaque;
    if (!s) s = &g_tim3;

    // Only 32-bit accesses observed; tolerate others by trunc/extend.
    uint32_t off = (uint32_t)(addr - (hwaddr)TIM3_BASE);
    uint32_t val = 0;

    switch (off) {
        case TIM3_CR1_OFS:
            val = s->cr1;
            break;
        case TIM3_EGR_OFS:
            // EGR is effectively write-only; return 0.
            val = 0;
            break;
        case TIM3_CCMR1_OFS:
            val = s->ccmr1;
            break;
        case TIM3_CCER_OFS:
            val = s->ccer;
            break;
        case TIM3_PSC_OFS:
            val = s->psc_shadow;
            break;
        case TIM3_ARR_OFS:
            val = s->arr_shadow;
            break;

        // Not in your trace, but safe for firmware that later reads them:
        case TIM3_DIER_OFS:
            val = s->dier;
            break;
        case TIM3_SR_OFS:
            val = s->sr;
            break;
        case TIM3_CNT_OFS:
            tim3_update_cnt_on_demand(s);
            val = s->cnt;
            break;

        default:
            val = 0;
            break;
    }

    if (size == 1) return (uint8_t)val;
    if (size == 2) return (uint16_t)val;
    return (uint64_t)val;
}

void tim3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    tim3_state_t *s = (tim3_state_t *)opaque;
    if (!s) s = &g_tim3;

    uint32_t off = (uint32_t)(addr - (hwaddr)TIM3_BASE);

    // Normalize to 32-bit for this peripheral.
    uint32_t v32 = (uint32_t)value;
    if (size == 1) v32 = (uint8_t)value;
    else if (size == 2) v32 = (uint16_t)value;

    switch (off) {
        case TIM3_CR1_OFS: {
            uint32_t old = s->cr1;

            // Keep only meaningful low bits (timer CR1 is small). Preserve any reserved as 0.
            uint32_t newv = v32 & 0x00FFu; // enough for bits we care about (CEN..ARPE)
            // But also keep DIR/CMS/OPM/UDIS/URS if present:
            newv = v32 & (CR1_CEN | CR1_UDIS | CR1_URS | CR1_OPM | CR1_DIR | CR1_CMS_MASK | CR1_ARPE);

            s->cr1 = newv;

            dbg("[TIM3] CR1 write: 0x%08x -> 0x%08x\n", old, s->cr1);

            // If CEN toggles, arm overflow timer.
            bool old_en = (old & CR1_CEN) != 0;
            bool new_en = (s->cr1 & CR1_CEN) != 0;
            if (!old_en && new_en) {
                s->last_ns = qemu_plugin_get_virtual_timer();
                tim3_arm_overflow(s);
            }
            break;
        }

        case TIM3_PSC_OFS:
            s->psc_shadow = v32;
            dbg("[TIM3] PSC write: shadow=0x%08x\n", s->psc_shadow);

            // If timer is running, re-arm (PSC takes effect on update in real HW,
            // but firmware in your trace triggers UG explicitly; we follow that).
            break;

        case TIM3_ARR_OFS:
            s->arr_shadow = v32;
            dbg("[TIM3] ARR write: shadow=0x%08x\n", s->arr_shadow);

            // If ARPE=0, apply immediately.
            if ((s->cr1 & CR1_ARPE) == 0) {
                s->arr_active = s->arr_shadow;
                tim3_arm_overflow(s);
            }
            break;

        case TIM3_EGR_OFS:
            dbg("[TIM3] EGR write: 0x%08x\n", v32);
            if (v32 & EGR_UG) {
                tim3_apply_update_event(s, /*from_ug=*/true);
            }
            break;

        case TIM3_CCMR1_OFS:
            s->ccmr1 = v32;
            dbg("[TIM3] CCMR1 write: 0x%08x\n", s->ccmr1);
            break;

        case TIM3_CCER_OFS:
            s->ccer = v32;
            dbg("[TIM3] CCER write: 0x%08x\n", s->ccer);
            break;

        // Optional/safe: DIER/SR/CNT behavior if firmware later touches them.
        case TIM3_DIER_OFS:
            s->dier = v32 & 0x0000FFFFu;
            dbg("[TIM3] DIER write: 0x%08x\n", s->dier);
            break;

        case TIM3_SR_OFS:
            // Writing 0 clears flags in many STM32 timers; implement common pattern:
            // clear bits that are written as 0 in v32 (firmware often writes ~flagmask or 0).
            // Simplest: if they write 0, clear all.
            if (v32 == 0) {
                s->sr = 0;
            } else {
                // Clear bits that are 0 in v32 (W0C-ish approximation)
                s->sr &= v32;
            }
            dbg("[TIM3] SR write: now 0x%08x\n", s->sr);
            break;

        case TIM3_CNT_OFS:
            s->cnt = v32;
            dbg("[TIM3] CNT write: 0x%08x\n", s->cnt);
            // CNT write changes phase; re-arm.
            tim3_arm_overflow(s);
            break;

        default:
            // Ignore unknown writes (trace-driven minimalism)
            break;
    }
}

// -------------------------
// Initialization
// -------------------------
void tim3_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_tim3, 0, sizeof(g_tim3));

    // Defaults: PSC/ARR reset values are 0 in many cases; keep 0.
    g_tim3.psc_shadow = 0;
    g_tim3.arr_shadow = 0;
    g_tim3.psc_active = 0;
    g_tim3.arr_active = 0xFFFF; // a benign non-zero default period
    g_tim3.last_ns = qemu_plugin_get_virtual_timer();

    // Create a one-shot timer for overflow events (only armed if CEN becomes 1).
    g_tim3.overflow_timer = qemu_plugin_timer_new_ns(tim3_overflow_cb, &g_tim3);

    dbg("[TIM3] init: created overflow timer=%llu, TIM3_INPUT_HZ=%llu\n",
        (unsigned long long)g_tim3.overflow_timer,
        (unsigned long long)TIM3_INPUT_HZ);
}
