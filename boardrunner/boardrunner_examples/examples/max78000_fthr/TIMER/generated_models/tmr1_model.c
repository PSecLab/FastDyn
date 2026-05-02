// Device Model for TMR1
//
// Inferred Register Functions:
//   CNT   (0x00): live counter / reload start value
//   CMP   (0x04): compare threshold
//   INTFL (0x0C): interrupt flags, bit0 set on compare match, W1C
//   CTRL0 (0x10): main timer control
//   CTRL1 (0x18): secondary control with a few hardware-reflected status bits
//
// Trace-derived behavior implemented:
//   - Absolute MMIO base: 0x40011000
//   - IRQ line: NVIC IRQn 6 => pass 22 to qemu_plugin_raise_irq()
//   - INTFL baseline reads as 0x01000100
//   - Compare event sets INTFL bit0 -> 0x01000101
//   - INTFL bit0 is cleared by write-1-to-clear
//   - CNT increases dynamically while enabled
//   - Periodic compare interrupts occur roughly every 100 ms with CMP=0x16800
//
// Notes:
//   - This is a minimal stateful model guided by the trace, not a full RM-complete timer.
//   - The effective counter rate is inferred as 921600 Hz because 92160 ticks -> 100 ms,
//     which matches the observed interrupt cadence well.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define TMR1_BASE              0x40011000ULL

#define TMR1_REG_CNT           0x00
#define TMR1_REG_CMP           0x04
#define TMR1_REG_INTFL         0x0C
#define TMR1_REG_CTRL0         0x10
#define TMR1_REG_CTRL1         0x18

#define TMR1_IRQ_EXCEPTION     22   // TMR1 uses NVIC IRQn 6 + 16

#define TMR1_INTFL_BASELINE    0x01000100u
#define TMR1_INTFL_CMPIF       0x00000001u

// Inferred CTRL0 bits from trace behavior
#define TMR1_CTRL0_ENABLE      0x00000001u
#define TMR1_CTRL0_STATUS8_SRC 0x00004000u   // makes CTRL1 read back +0x8
#define TMR1_CTRL0_IRQ_ARMED   0x00008000u   // CTRL0 bit 15: setting this makes CTRL1 bit 2 (clk-domain sync ack) read 1; firmware busy-waits on it after MXC_TMR_Start
#define TMR1_CTRL0_PERIODIC    0x00200000u   // periodic compare mode inferred from recurring IRQs

// Inferred CTRL1 read-only reflected bits
#define TMR1_CTRL1_RO_BIT2     0x00000004u
#define TMR1_CTRL1_RO_BIT3     0x00000008u

// Inferred effective timer clock from compare value vs. observed ~100 ms IRQ cadence.
#define TMR1_TICK_HZ           921600ULL

typedef struct {
    uint32_t cnt_reload;
    uint32_t cmp;
    uint32_t intfl;
    uint32_t ctrl0;
    uint32_t ctrl1;

    uint64_t timer_handle;

    // Timebase for dynamic counter
    uint64_t run_start_ns;

    // Number of compare events already accounted for
    uint64_t matches_seen;
} TMR1State;

static TMR1State g_tmr1;

static inline uint64_t tmr1_now_ns(void) {
    int64_t now = qemu_plugin_get_virtual_timer();
    return (now < 0) ? 0 : (uint64_t)now;
}

static inline bool tmr1_running(const TMR1State *s) {
    return (s->ctrl0 & TMR1_CTRL0_ENABLE) != 0;
}

static inline bool tmr1_periodic_mode(const TMR1State *s) {
    return (s->ctrl0 & TMR1_CTRL0_PERIODIC) != 0;
}

static inline bool tmr1_irq_enabled(const TMR1State *s) {
    return (s->ctrl0 & TMR1_CTRL0_IRQ_ARMED) != 0;
}

static inline uint32_t tmr1_ctrl1_ro_bits(const TMR1State *s) {
    uint32_t ro = 0;

    if (s->ctrl0 & TMR1_CTRL0_STATUS8_SRC) {
        ro |= TMR1_CTRL1_RO_BIT3;
    }
    if (s->ctrl0 & TMR1_CTRL0_IRQ_ARMED) {
        ro |= TMR1_CTRL1_RO_BIT2;
    }

    return ro;
}

static uint64_t tmr1_ns_to_ticks(uint64_t ns)
{
    unsigned __int128 prod = (unsigned __int128)ns * (unsigned __int128)TMR1_TICK_HZ;
    return (uint64_t)(prod / 1000000000ULL);
}

static uint64_t tmr1_ticks_to_ns_ceil(uint64_t ticks)
{
    unsigned __int128 num = (unsigned __int128)ticks * 1000000000ULL + (TMR1_TICK_HZ - 1);
    return (uint64_t)(num / TMR1_TICK_HZ);
}

static uint64_t tmr1_elapsed_ticks(const TMR1State *s, uint64_t now_ns)
{
    if (!tmr1_running(s)) {
        return 0;
    }
    if (now_ns <= s->run_start_ns) {
        return 0;
    }
    return tmr1_ns_to_ticks(now_ns - s->run_start_ns);
}

static bool tmr1_has_valid_compare_window(const TMR1State *s)
{
    return s->cmp != 0 && s->cmp >= s->cnt_reload;
}

static uint64_t tmr1_period_ticks(const TMR1State *s)
{
    if (!tmr1_has_valid_compare_window(s)) {
        return 0;
    }
    return (uint64_t)s->cmp - (uint64_t)s->cnt_reload + 1ULL;
}

static uint64_t tmr1_match_count_now(const TMR1State *s, uint64_t now_ns)
{
    uint64_t elapsed, period, match_tick;

    if (!tmr1_running(s) || !tmr1_has_valid_compare_window(s)) {
        return 0;
    }

    elapsed = tmr1_elapsed_ticks(s, now_ns);

    if (tmr1_periodic_mode(s)) {
        period = tmr1_period_ticks(s);
        if (period == 0) {
            return 0;
        }
        // Compare occurs when elapsed reaches period-1, then again every period.
        return (elapsed + 1ULL) / period;
    }

    match_tick = (uint64_t)s->cmp - (uint64_t)s->cnt_reload;
    return (elapsed >= match_tick) ? 1ULL : 0ULL;
}

static uint32_t tmr1_current_count(const TMR1State *s, uint64_t now_ns)
{
    uint64_t elapsed, period;
    uint64_t count64;

    if (!tmr1_running(s)) {
        return s->cnt_reload;
    }

    elapsed = tmr1_elapsed_ticks(s, now_ns);

    if (tmr1_periodic_mode(s) && tmr1_has_valid_compare_window(s)) {
        period = tmr1_period_ticks(s);
        if (period != 0) {
            count64 = (uint64_t)s->cnt_reload + (elapsed % period);
            return (uint32_t)count64;
        }
    }

    count64 = (uint64_t)s->cnt_reload + elapsed;
    return (uint32_t)count64;
}

static void tmr1_schedule_next_alarm(TMR1State *s, uint64_t now_ns)
{
    uint64_t next_match_index;
    uint64_t event_elapsed_ticks;
    uint64_t abs_ns;
    uint64_t period;

    if (!tmr1_running(s) || !tmr1_irq_enabled(s) || !tmr1_has_valid_compare_window(s)) {
        return;
    }

    if (tmr1_periodic_mode(s)) {
        period = tmr1_period_ticks(s);
        if (period == 0) {
            return;
        }
        next_match_index = s->matches_seen + 1ULL;
        event_elapsed_ticks = next_match_index * period - 1ULL;
    } else {
        if (s->matches_seen != 0) {
            return;
        }
        event_elapsed_ticks = (uint64_t)s->cmp - (uint64_t)s->cnt_reload;
    }

    abs_ns = s->run_start_ns + tmr1_ticks_to_ns_ceil(event_elapsed_ticks);

    if (abs_ns <= now_ns) {
        abs_ns = now_ns + 1ULL;
    }

    qemu_plugin_timer_alarm(s->timer_handle, abs_ns);
}

static void tmr1_sync(TMR1State *s, uint64_t now_ns)
{
    uint64_t matches_now;

    matches_now = tmr1_match_count_now(s, now_ns);
    if (matches_now > s->matches_seen) {
        s->matches_seen = matches_now;
        s->intfl |= TMR1_INTFL_CMPIF;

        if (tmr1_irq_enabled(s)) {
            qemu_plugin_raise_irq(TMR1_IRQ_EXCEPTION, false);
        }
    }

    tmr1_schedule_next_alarm(s, now_ns);
}

static void tmr1_timer_cb(void *opaque)
{
    TMR1State *s = (TMR1State *)opaque;
    uint64_t now_ns = tmr1_now_ns();

    tmr1_sync(s, now_ns);
}

static uint64_t tmr1_read_reg32(TMR1State *s, uint32_t offset)
{
    uint64_t now_ns = tmr1_now_ns();

    // Important: synchronize compare logic on reads too, because virtual time
    // only advances at TB boundaries and timer callbacks may not fire during
    // tight MMIO polling loops.
    tmr1_sync(s, now_ns);

    switch (offset) {
    case TMR1_REG_CNT:
        return tmr1_current_count(s, now_ns);

    case TMR1_REG_CMP:
        return s->cmp;

    case TMR1_REG_INTFL:
        return s->intfl;

    case TMR1_REG_CTRL0:
        return s->ctrl0;

    case TMR1_REG_CTRL1:
        return s->ctrl1 | tmr1_ctrl1_ro_bits(s);

    default:
        return 0;
    }
}

static void tmr1_freeze_counter(TMR1State *s, uint64_t now_ns)
{
    s->cnt_reload = tmr1_current_count(s, now_ns);
    s->run_start_ns = now_ns;
}

static void tmr1_rebase_counter(TMR1State *s, uint64_t now_ns, uint32_t new_cnt)
{
    s->cnt_reload = new_cnt;
    s->run_start_ns = now_ns;
    s->matches_seen = 0;
}

static void tmr1_write_reg32(TMR1State *s, uint32_t offset, uint32_t value)
{
    uint64_t now_ns = tmr1_now_ns();
    bool was_running;

    // First bring state current before applying the write.
    tmr1_sync(s, now_ns);

    switch (offset) {
    case TMR1_REG_CNT:
        tmr1_rebase_counter(s, now_ns, value);
        break;

    case TMR1_REG_CMP:
        if (tmr1_running(s)) {
            /*
             * Preserve the live counter value across CMP updates.
             * Resetting only run_start_ns while leaving cnt_reload unchanged
             * makes CNT jump backwards and distorts the next compare event.
             */
            tmr1_freeze_counter(s, now_ns);
        }
        s->cmp = value;
        s->matches_seen = 0;
        break;

    case TMR1_REG_INTFL:
        // Bit0 behaves as W1C; baseline bits remain hardware-owned.
        if (value & TMR1_INTFL_CMPIF) {
            s->intfl &= ~TMR1_INTFL_CMPIF;
        }
        s->intfl |= TMR1_INTFL_BASELINE;
        break;

    case TMR1_REG_CTRL0:
        was_running = tmr1_running(s);

        if (was_running) {
            tmr1_freeze_counter(s, now_ns);
        }

        s->ctrl0 = value;

        if (!was_running && tmr1_running(s)) {
            // Transition into running state from the preserved cnt_reload.
            s->run_start_ns = now_ns;
            s->matches_seen = 0;
        }

        break;

    case TMR1_REG_CTRL1:
        // Store software-visible bits; read-only reflected bits are overlaid on read.
        s->ctrl1 = value;
        break;

    default:
        break;
    }

    tmr1_schedule_next_alarm(s, now_ns);
}

static uint64_t tmr1_read_sized(TMR1State *s, uint32_t offset, unsigned size)
{
    uint32_t val = (uint32_t)tmr1_read_reg32(s, offset);

    switch (size) {
    case 1:
        return val & 0xFFu;
    case 2:
        return val & 0xFFFFu;
    case 4:
    default:
        return val;
    }
}

static uint32_t tmr1_merge_write(uint32_t oldv, uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        return (oldv & ~0xFFu) | ((uint32_t)value & 0xFFu);
    case 2:
        return (oldv & ~0xFFFFu) | ((uint32_t)value & 0xFFFFu);
    case 4:
    default:
        return (uint32_t)value;
    }
}

// This function will emulate all device reads
uint64_t tmr1_read(void *opaque, hwaddr addr, unsigned size)
{
    TMR1State *s = (TMR1State *)opaque;
    hwaddr offset = addr - TMR1_BASE;

    return tmr1_read_sized(s, (uint32_t)offset, size);
}

// This function will emulate all device writes
void tmr1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    TMR1State *s = (TMR1State *)opaque;
    hwaddr offset = addr - TMR1_BASE;
    uint32_t oldv = (uint32_t)tmr1_read_reg32(s, (uint32_t)offset);
    uint32_t newv = tmr1_merge_write(oldv, value, size);

    switch ((uint32_t)offset) {
    case TMR1_REG_CNT:
    case TMR1_REG_CMP:
    case TMR1_REG_INTFL:
    case TMR1_REG_CTRL0:
    case TMR1_REG_CTRL1:
        tmr1_write_reg32(s, (uint32_t)offset, newv);
        break;
    default:
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* tmr1_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_tmr1, 0, sizeof(g_tmr1));

    g_tmr1.intfl = TMR1_INTFL_BASELINE;
    g_tmr1.timer_handle = qemu_plugin_timer_new_ns(tmr1_timer_cb, &g_tmr1);

    return &g_tmr1;
}