// Device Model for TIM3 (STM32F103xx) - trace-driven + backward-pass correction
//
// Primary fixes to match the provided mismatch constraints:
//  1) Interrupt must be raised as (IRQn + 16, false). TIM3 IRQn observed = 29 => raise(45, false).
//  2) EGR.UG when CR1.URS=1 must NOT set UIF / cause IRQ (matches your init: CR1=0x4 then EGR=1).
//  3) SR must alternate: baseline 0x1E, and 0x1F only when UIF is pending, then return to 0x1E after clear.
//
// Implemented (based only on provided data):
//  - CR1/DIER/SR RMW-safe storage
//  - CNT progresses with qemu_plugin_get_virtual_timer()
//  - Update event sets UIF and raises IRQ if UIE enabled
//  - SR write-0-to-clear via value ANDing (old_sr &= value)
//
// Build: include <device.h> and <boardrunner/vio.h> (provided by your environment)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef hwaddr
typedef uint64_t hwaddr;
#endif

// ---- Base + offsets (from trace addresses) ----
#define TIM3_BASE_ADDR   0x40000400ULL

#define TIM3_CR1_OFF     0x00
#define TIM3_SMCR_OFF    0x08
#define TIM3_DIER_OFF    0x0C
#define TIM3_SR_OFF      0x10
#define TIM3_EGR_OFF     0x14
#define TIM3_CNT_OFF     0x24
#define TIM3_PSC_OFF     0x28
#define TIM3_ARR_OFF     0x2C

// ---- Bits used (only what traces imply) ----
#define TIM_CR1_CEN      (1u << 0)
#define TIM_CR1_URS      (1u << 2)

#define TIM_DIER_UIE     (1u << 0)

#define TIM_SR_UIF       (1u << 0)

// Trace: SR reads are dominated by 0x1E and 0x1F.
#define TIM_SR_BASELINE  (0x1Eu)                 // bits 1..4 appear set in reads
#define TIM_SR_IMPL_MASK (TIM_SR_UIF | 0x1Eu)    // we only care about UIF + bits 1..4 group

// Interrupt details from trace
#define TIM3_IRQn              29
#define NVIC_EXCEPTION(irqn)   ((irqn) + 16)

// Heuristic clock to match PSC/ARR to ~1 second: (0x18FF+1)*(0x270F+1)/64MHz = 1s
#define TIM3_BASE_CLK_HZ       (64000000ULL)

typedef struct tim3_state {
    uint32_t CR1;
    uint32_t SMCR;
    uint32_t DIER;
    uint32_t SR;
    uint32_t EGR;
    uint32_t CNT;
    uint32_t PSC;
    uint32_t ARR;

    uint64_t base_clk_hz;

    uint64_t oneshot_timer;
    bool     enabled;

    uint64_t last_vtime_ns;   // last virtual time used to update CNT
} tim3_state_t;

static tim3_state_t g_tim3;

static void tim3_debugf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static inline hwaddr tim3_norm_off(hwaddr addr)
{
    // Allow either absolute MMIO address or offset
    if (addr >= TIM3_BASE_ADDR && addr < (TIM3_BASE_ADDR + 0x400ULL)) {
        return (hwaddr)(addr - TIM3_BASE_ADDR);
    }
    return addr;
}

static inline uint32_t mask_from_size(hwaddr addr, unsigned size, uint32_t *shift_out)
{
    uint32_t shift = 0;
    uint32_t mask  = 0xFFFFFFFFu;

    if (size == 4) {
        shift = 0;
        mask  = 0xFFFFFFFFu;
    } else if (size == 2) {
        shift = (uint32_t)((addr & 0x2u) * 8u); // 0 or 16
        mask  = (uint32_t)(0xFFFFu << shift);
    } else if (size == 1) {
        shift = (uint32_t)((addr & 0x3u) * 8u); // 0,8,16,24
        mask  = (uint32_t)(0xFFu << shift);
    } else {
        shift = 0;
        mask  = 0xFFFFFFFFu;
    }

    if (shift_out) *shift_out = shift;
    return mask;
}

static inline uint32_t rmw_apply(uint32_t oldv, hwaddr addr, uint64_t value, unsigned size)
{
    uint32_t shift;
    uint32_t mask = mask_from_size(addr, size, &shift);
    uint32_t v32  = (uint32_t)value;
    uint32_t ins  = (uint32_t)((v32 << shift) & mask);
    return (oldv & ~mask) | ins;
}

static inline uint64_t read_sized(uint32_t reg, hwaddr addr, unsigned size)
{
    if (size == 4) return (uint64_t)reg;
    if (size == 2) {
        uint32_t shift = (uint32_t)((addr & 0x2u) * 8u);
        return (uint64_t)((reg >> shift) & 0xFFFFu);
    }
    if (size == 1) {
        uint32_t shift = (uint32_t)((addr & 0x3u) * 8u);
        return (uint64_t)((reg >> shift) & 0xFFu);
    }
    return (uint64_t)reg;
}

static inline uint32_t tim3_arr_mod(const tim3_state_t *s)
{
    uint32_t arr = (uint32_t)(s->ARR & 0xFFFFu);
    uint32_t mod = arr + 1u;
    if (mod == 0) mod = 0x10000u; // arr=0xFFFF
    return mod;
}

static void tim3_update_cnt_now(tim3_state_t *s, uint64_t now_ns)
{
    if (!s->enabled) {
        s->last_vtime_ns = now_ns;
        return;
    }
    if (s->last_vtime_ns == 0) {
        s->last_vtime_ns = now_ns;
        return;
    }

    uint64_t elapsed_ns = now_ns - s->last_vtime_ns;
    if (elapsed_ns == 0) return;

    uint64_t psc_div = ((uint64_t)(s->PSC & 0xFFFFu)) + 1ULL;
    if (psc_div == 0) psc_div = 1;

    // ticks = elapsed_ns * base_clk / 1e9 / (PSC+1)
    __int128 num = (__int128)elapsed_ns * (__int128)s->base_clk_hz;
    uint64_t raw_ticks = (uint64_t)(num / (__int128)1000000000LL);
    uint64_t ticks = raw_ticks / psc_div;

    if (ticks == 0) return;

    uint32_t mod = tim3_arr_mod(s);
    uint32_t cnt = (uint32_t)(s->CNT & 0xFFFFu);
    cnt = (uint32_t)((cnt + (uint32_t)(ticks % mod)) % mod);
    s->CNT = (s->CNT & 0xFFFF0000u) | (cnt & 0xFFFFu);

    // Snap; this keeps CNT moving and bounded like hardware (< ARR)
    s->last_vtime_ns = now_ns;
}

static uint64_t tim3_tick_ns(const tim3_state_t *s)
{
    // tick_ns = 1e9 * (PSC+1) / base_clk
    uint64_t psc = (uint64_t)(s->PSC & 0xFFFFu) + 1ULL;
    if (s->base_clk_hz == 0) return 1;

    __int128 num = (__int128)psc * (__int128)1000000000LL;
    uint64_t ns = (uint64_t)(num / (__int128)s->base_clk_hz);
    if (ns == 0) ns = 1;
    return ns;
}

static void tim3_arm_next_update(tim3_state_t *s)
{
    if (!s->enabled) return;

    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    tim3_update_cnt_now(s, now);

    uint32_t mod = tim3_arr_mod(s);
    uint32_t cnt = (uint32_t)(s->CNT & 0xFFFFu);

    // Remaining ticks until overflow/update:
    // If CNT in [0..ARR], update occurs when it wraps from ARR -> 0, i.e., after (ARR - CNT + 1) ticks.
    uint32_t remaining = (uint32_t)((mod - cnt) % mod);
    if (remaining == 0) remaining = mod; // if cnt==0, schedule full period

    uint64_t ns_per_tick = tim3_tick_ns(s);
    uint64_t delay = ns_per_tick * (uint64_t)remaining;

    qemu_plugin_timer_alarm(s->oneshot_timer, now + delay);
}

static void tim3_raise_irq_if_enabled(const tim3_state_t *s)
{
    if ((s->DIER & TIM_DIER_UIE) != 0) {
        // REQUIRED calling convention: (IRQn + 16, false)
        qemu_plugin_raise_irq(NVIC_EXCEPTION(TIM3_IRQn), false);
    }
}

static void tim3_timer_cb(void *opaque)
{
    tim3_state_t *s = (tim3_state_t *)opaque;

    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    tim3_update_cnt_now(s, now);

    if (!s->enabled) return;

    // Overflow/update event: set UIF, CNT wraps to 0.
    s->SR |= TIM_SR_UIF;
    s->CNT = (s->CNT & 0xFFFF0000u) | 0u;

    tim3_raise_irq_if_enabled(s);

    // Reschedule next update
    s->last_vtime_ns = now;
    tim3_arm_next_update(s);
}

// This function will emulate all device reads
uint64_t tim3_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    tim3_state_t *s = &g_tim3;

    hwaddr off = tim3_norm_off(addr);

    if (off == TIM3_CNT_OFF) {
        uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
        tim3_update_cnt_now(s, now);
    }

    switch (off) {
    case TIM3_CR1_OFF:
        return read_sized(s->CR1, off, size);

    case TIM3_SMCR_OFF:
        return read_sized(s->SMCR, off, size);

    case TIM3_DIER_OFF:
        return read_sized(s->DIER, off, size);

    case TIM3_SR_OFF: {
        // Match hardware: baseline 0x1E, add UIF => 0x1F when pending.
        uint32_t sr = (s->SR & TIM_SR_IMPL_MASK) | TIM_SR_BASELINE;
        return read_sized(sr, off, size);
    }

    case TIM3_EGR_OFF:
        return read_sized(s->EGR, off, size);

    case TIM3_CNT_OFF:
        return read_sized(s->CNT & 0xFFFFu, off, size);

    case TIM3_PSC_OFF:
        return read_sized(s->PSC & 0xFFFFu, off, size);

    case TIM3_ARR_OFF:
        return read_sized(s->ARR & 0xFFFFu, off, size);

    default:
        return 0;
    }
}

// This function will emulate all device writes
void tim3_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    (void)opaque;
    tim3_state_t *s = &g_tim3;

    hwaddr off = tim3_norm_off(addr);

    switch (off) {
    case TIM3_CR1_OFF: {
        uint32_t new_cr1 = rmw_apply(s->CR1, off, value, size);
        bool old_en = (s->CR1 & TIM_CR1_CEN) != 0;
        bool new_en = (new_cr1 & TIM_CR1_CEN) != 0;

        s->CR1 = new_cr1;

        if (!old_en && new_en) {
            s->enabled = true;
            s->last_vtime_ns = (uint64_t)qemu_plugin_get_virtual_timer();
            tim3_arm_next_update(s);
        } else if (old_en && !new_en) {
            s->enabled = false;
        }
        break;
    }

    case TIM3_SMCR_OFF:
        s->SMCR = rmw_apply(s->SMCR, off, value, size);
        break;

    case TIM3_DIER_OFF:
        s->DIER = rmw_apply(s->DIER, off, value, size);
        break;

    case TIM3_SR_OFF: {
        // STM32 W0C semantics: writing 0 clears, writing 1 keeps.
        // Example: old 0x1F, write 0xFFFFFFFE => old &= write => 0x1E (clears UIF)
        uint32_t v = (uint32_t)value;
        s->SR &= v;

        // Keep SR state bounded to what we implement
        s->SR &= TIM_SR_IMPL_MASK;
        break;
    }

    case TIM3_EGR_OFF: {
        s->EGR = rmw_apply(s->EGR, off, value, size);

        // UG = bit0
        if (((uint32_t)value) & 0x1u) {
            // Important trace-driven behavior:
            // In init, CR1 was 0x4 (URS=1) before EGR=1.
            // With URS=1, UG should NOT behave like an update request source (no UIF / no IRQ).
            bool urs = (s->CR1 & TIM_CR1_URS) != 0;

            // Still safe to reset CNT to 0 (helps "reload" semantics) without creating UIF noise.
            s->CNT = (s->CNT & 0xFFFF0000u) | 0u;

            if (!urs) {
                s->SR |= TIM_SR_UIF;
                tim3_raise_irq_if_enabled(s);
            }

            // If already running, re-arm from now (treat UG as re-sync)
            if (s->enabled) {
                s->last_vtime_ns = (uint64_t)qemu_plugin_get_virtual_timer();
                tim3_arm_next_update(s);
            }
        }
        break;
    }

    case TIM3_CNT_OFF:
        s->CNT = rmw_apply(s->CNT, off, value, size) & 0xFFFFu;
        if (s->enabled) {
            s->last_vtime_ns = (uint64_t)qemu_plugin_get_virtual_timer();
            tim3_arm_next_update(s);
        }
        break;

    case TIM3_PSC_OFF:
        s->PSC = rmw_apply(s->PSC, off, value, size) & 0xFFFFu;
        if (s->enabled) {
            s->last_vtime_ns = (uint64_t)qemu_plugin_get_virtual_timer();
            tim3_arm_next_update(s);
        }
        break;

    case TIM3_ARR_OFF:
        s->ARR = rmw_apply(s->ARR, off, value, size) & 0xFFFFu;
        if (s->enabled) {
            s->last_vtime_ns = (uint64_t)qemu_plugin_get_virtual_timer();
            tim3_arm_next_update(s);
        }
        break;

    default:
        break;
    }
}

void tim3_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_tim3, 0, sizeof(g_tim3));

    g_tim3.base_clk_hz = TIM3_BASE_CLK_HZ;

    // Defaults consistent with trace expectations
    g_tim3.CR1  = 0x00000000u;
    g_tim3.SMCR = 0x00000000u;
    g_tim3.DIER = 0x00000000u;
    g_tim3.SR   = 0x00000000u;   // reads OR in baseline 0x1E
    g_tim3.EGR  = 0x00000000u;

    g_tim3.CNT  = 0x00000000u;
    g_tim3.PSC  = 0x00000000u;
    g_tim3.ARR  = 0x0000FFFFu;

    g_tim3.enabled = false;
    g_tim3.last_vtime_ns = 0;

    g_tim3.oneshot_timer = qemu_plugin_timer_new_ns(tim3_timer_cb, &g_tim3);

    tim3_debugf("[tim3] init done. base_clk=%llu Hz, IRQn=%d -> raise(%d,false)\n",
                (unsigned long long)g_tim3.base_clk_hz,
                TIM3_IRQn, NVIC_EXCEPTION(TIM3_IRQn));
}
