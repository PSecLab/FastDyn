// Device Model for EXTI

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Inferred Register Functions:
// RTSR1   - Rising trigger selection for EXTI lines
// FTSR1   - Falling trigger selection for EXTI lines
// CPUIMR1 - CPU interrupt mask register
// CPUEMR1 - CPU event mask register
// CPUPR1  - CPU pending register (write-1-to-clear)

#define EXTI_BASE_ADDR          0x58000000ULL

#define EXTI_RTSR1_OFFSET       0x00
#define EXTI_FTSR1_OFFSET       0x04
#define EXTI_CPUIMR1_OFFSET     0x80
#define EXTI_CPUEMR1_OFFSET     0x84
#define EXTI_CPUPR1_OFFSET      0x88

// Observed reset/readback behavior from the trace.
#define EXTI_CPUIMR1_RESET_VAL  0xFFC00000U
#define EXTI_CPUIMR1_FIXED_ONES 0xFFC00000U

// STM32H753x IRQ numbers from the reference manual.
// qemu_plugin_raise_irq() requires IRQn + 16.
#define EXTI0_IRQn              6
#define EXTI1_IRQn              7
#define EXTI2_IRQn              8
#define EXTI3_IRQn              9
#define EXTI4_IRQn              10
#define EXTI9_5_IRQn            23
#define EXTI15_10_IRQn          40

typedef struct {
    uint32_t rtsr1;
    uint32_t ftsr1;
    uint32_t cpuimr1;
    uint32_t cpuemr1;
    uint32_t cpupr1;
    bool line_level[32];

    /*
     * After software clears a pending bit while the source line is still held
     * at its current level, hardware should not immediately re-pend until the
     * line first transitions away and a fresh configured edge occurs again.
     */
    uint32_t rise_blocked;
    uint32_t fall_blocked;
} exti_state_t;

static exti_state_t exti_state;

static exti_state_t *exti_get_state(void *opaque) {
    if (opaque) {
        return (exti_state_t *)opaque;
    }
    return &exti_state;
}

static uint32_t exti_mask_for_size(unsigned size)
{
    switch (size) {
    case 1:
        return 0xFFu;
    case 2:
        return 0xFFFFu;
    case 4:
    default:
        return 0xFFFFFFFFu;
    }
}

static uint32_t exti_extract_reg(uint32_t reg, hwaddr offset, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    uint32_t mask = exti_mask_for_size(size);
    return (reg >> shift) & mask;
}

static uint32_t exti_deposit_reg(uint32_t oldval, hwaddr offset, uint64_t value, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    uint32_t mask = exti_mask_for_size(size) << shift;
    uint32_t v = ((uint32_t)value << shift) & mask;
    return (oldval & ~mask) | v;
}

static uint32_t exti_expand_write(hwaddr offset, uint64_t value, unsigned size)
{
    unsigned shift = (unsigned)(offset & 0x3u) * 8u;
    return ((uint32_t)value) << shift;
}

static void exti_log_unknown(const char *op, uint64_t addr, uint64_t value) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "EXTI: unknown %s addr=0x%08llx value=0x%08llx",
             op,
             (unsigned long long)addr,
             (unsigned long long)value);
    dev_debug(buf);
}

static int exti_line_to_irqn(unsigned line) {
    switch (line) {
    case 0: return EXTI0_IRQn;
    case 1: return EXTI1_IRQn;
    case 2: return EXTI2_IRQn;
    case 3: return EXTI3_IRQn;
    case 4: return EXTI4_IRQn;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        return EXTI9_5_IRQn;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        return EXTI15_10_IRQn;
    default:
        return -1;
    }
}

static void exti_raise_for_line(exti_state_t *s, unsigned line) {
    uint32_t bit;
    int irqn;

    if (line >= 32) {
        return;
    }

    bit = (1U << line);
    if ((s->cpupr1 & bit) == 0) {
        return;
    }
    if ((s->cpuimr1 & bit) == 0) {
        return;
    }

    irqn = exti_line_to_irqn(line);
    if (irqn >= 0) {
        qemu_plugin_raise_irq(irqn + 16, false);
    }
}

static void exti_raise_all_pending(exti_state_t *s) {
    unsigned line;
    for (line = 0; line < 32; line++) {
        exti_raise_for_line(s, line);
    }
}

static void exti_pulse_irqs_for_cleared_mask(exti_state_t *s, uint32_t cleared_mask)
{
    unsigned line;
    bool raised_exti9_5 = false;
    bool raised_exti15_10 = false;

    for (line = 0; line < 32; line++) {
        uint32_t bit = (1U << line);
        int irqn;

        if ((cleared_mask & bit) == 0) {
            continue;
        }
        if ((s->cpuimr1 & bit) == 0) {
            continue;
        }

        irqn = exti_line_to_irqn(line);
        switch (irqn) {
        case EXTI9_5_IRQn:
            if (raised_exti9_5) {
                continue;
            }
            raised_exti9_5 = true;
            break;
        case EXTI15_10_IRQn:
            if (raised_exti15_10) {
                continue;
            }
            raised_exti15_10 = true;
            break;
        default:
            break;
        }

        if (irqn >= 0) {
            qemu_plugin_raise_irq(irqn + 16, false);
        }
    }
}

static void exti_block_retrigger_after_clear(exti_state_t *s, uint32_t cleared_mask)
{
    unsigned line;

    for (line = 0; line < 32; line++) {
        uint32_t bit = (1U << line);

        if ((cleared_mask & bit) == 0) {
            continue;
        }

        /*
         * If software clears a pending bit while the input remains high,
         * suppress another rising-edge latch until the line goes low again.
         * Likewise, if the line remains low, suppress another falling-edge
         * latch until the line goes high again.
         */
        if (s->line_level[line]) {
            s->rise_blocked |= bit;
        } else {
            s->fall_blocked |= bit;
        }
    }
}

static void exti_signal_cb(void *opaque, int signal_id, bool level) {
    exti_state_t *s = (exti_state_t *)opaque;
    unsigned line;
    uint32_t bit;
    bool old_level;
    bool rising;
    bool falling;
    bool trigger;
    bool already_pending;

    if (signal_id < 0 || signal_id >= 32) {
        return;
    }

    line = (unsigned)signal_id;
    bit = (1U << line);

    old_level = s->line_level[line];
    s->line_level[line] = level;

    /*
     * Opposite level re-arms edge detection after a software clear.
     * High level re-arms falling detection; low level re-arms rising detection.
     */
    if (level) {
        s->fall_blocked &= ~bit;
    } else {
        s->rise_blocked &= ~bit;
    }

    rising = (!old_level && level);
    falling = (old_level && !level);

    trigger = false;
    if (rising && (s->rtsr1 & bit) && ((s->rise_blocked & bit) == 0)) {
        trigger = true;
        s->rise_blocked |= bit;
    }
    if (falling && (s->ftsr1 & bit) && ((s->fall_blocked & bit) == 0)) {
        trigger = true;
        s->fall_blocked |= bit;
    }

    if (!trigger) {
        return;
    }

    already_pending = ((s->cpupr1 & bit) != 0);
    s->cpupr1 |= bit;

    if (!already_pending) {
        exti_raise_for_line(s, line);
    }
}

// This function will emulation all device reads
uint64_t exti_read(void *opaque, hwaddr addr, unsigned size) {
    exti_state_t *s = exti_get_state(opaque);
    uint64_t offset;
    uint64_t regoff;
    uint32_t ret = 0;

    if (addr < EXTI_BASE_ADDR) {
        exti_log_unknown("read", (uint64_t)addr, 0);
        return 0;
    }

    offset = (uint64_t)addr - EXTI_BASE_ADDR;
    regoff = offset & ~0x3ULL;

    switch (regoff) {
    case EXTI_RTSR1_OFFSET:
        ret = exti_extract_reg(s->rtsr1, offset, size);
        break;
    case EXTI_FTSR1_OFFSET:
        ret = exti_extract_reg(s->ftsr1, offset, size);
        break;
    case EXTI_CPUIMR1_OFFSET:
        ret = exti_extract_reg(s->cpuimr1, offset, size);
        break;
    case EXTI_CPUEMR1_OFFSET:
        ret = exti_extract_reg(s->cpuemr1, offset, size);
        break;
    case EXTI_CPUPR1_OFFSET:
        ret = exti_extract_reg(s->cpupr1, offset, size);
        break;
    default:
        exti_log_unknown("read", (uint64_t)addr, 0);
        ret = 0;
        break;
    }

    return ret;
}

// This function will emulate all device writes
void exti_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    exti_state_t *s = exti_get_state(opaque);
    uint64_t offset;
    uint64_t regoff;
    uint32_t v = (uint32_t)value;
    uint32_t expanded;

    if (addr < EXTI_BASE_ADDR) {
        exti_log_unknown("write", (uint64_t)addr, value);
        return;
    }

    offset = (uint64_t)addr - EXTI_BASE_ADDR;
    regoff = offset & ~0x3ULL;
    expanded = exti_expand_write(offset, value, size);

    switch (regoff) {
    case EXTI_RTSR1_OFFSET:
        s->rtsr1 = exti_deposit_reg(s->rtsr1, offset, v, size);
        break;

    case EXTI_FTSR1_OFFSET:
        s->ftsr1 = exti_deposit_reg(s->ftsr1, offset, v, size);
        break;

    case EXTI_CPUIMR1_OFFSET:
        s->cpuimr1 = exti_deposit_reg(s->cpuimr1, offset, v, size);
        s->cpuimr1 = (s->cpuimr1 & ~EXTI_CPUIMR1_FIXED_ONES) | EXTI_CPUIMR1_FIXED_ONES;
        exti_raise_all_pending(s);
        break;

    case EXTI_CPUEMR1_OFFSET:
        s->cpuemr1 = exti_deposit_reg(s->cpuemr1, offset, v, size);
        break;

    case EXTI_CPUPR1_OFFSET:
    {
        uint32_t cleared;

        /* Write-1-to-clear. */
        cleared = s->cpupr1 & expanded;
        s->cpupr1 &= ~expanded;

        /*
         * Prevent immediate re-latching on the same steady level after a
         * software clear; a fresh opposite transition must occur first.
         */
        exti_block_retrigger_after_clear(s, cleared);

        /*
         * Trace evidence shows the shared EXTI interrupt can be observed once
         * more after software clears the pending bit, with CPUPR1 already back
         * to 0 on the subsequent handler entry. Re-pulse the corresponding
         * IRQ group for cleared, unmasked lines so firmware performs the same
         * post-clear readback path.
         */
        exti_pulse_irqs_for_cleared_mask(s, cleared);
        break;
    }

    default:
        exti_log_unknown("write", (uint64_t)addr, value);
        break;
    }
}

void exti_init(ConfigSection* model_info) {
    unsigned i;
    (void)model_info;

    memset(&exti_state, 0, sizeof(exti_state));
    exti_state.cpuimr1 = EXTI_CPUIMR1_RESET_VAL;

    // EXTI is a sink for logical signal lines. Register line callbacks so that
    // GPIO or other source peripherals can drive EXTI0..EXTI31 levels.
    for (i = 0; i < 32; i++) {
        api_signal_register((int)i, exti_signal_cb, &exti_state);
    }
}