#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define SYSTEMCONTROL_BASE          0xE000E000ULL
#define SYSTEMCONTROL_SIZE          0x1000U

#define SYSTICK_CTRL_OFF            0x010U
#define SYSTICK_LOAD_OFF            0x014U
#define SYSTICK_VAL_OFF             0x018U
#define SYSTICK_CALIB_OFF           0x01CU

#define SCB_CPUID_OFF               0xD00U
#define SCB_CCR_OFF                 0xD14U
#define SCB_CPACR_OFF               0xD88U

#define SYSTICK_CTRL_ENABLE         (1U << 0)
#define SYSTICK_CTRL_TICKINT        (1U << 1)
#define SYSTICK_CTRL_CLKSOURCE      (1U << 2)
#define SYSTICK_CTRL_COUNTFLAG      (1U << 16)
#define SYSTICK_CTRL_RW_MASK        (SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_CLKSOURCE)

#define SYSTICK_RELOAD_MASK         0x00FFFFFFU
#define SYSTICK_IRQN                (-1) /* CMSIS SysTick_IRQn */
#define SYSTICK_IRQ_LINE            (SYSTICK_IRQN + 16)
#define DEFAULT_CORE_CLOCK_HZ       528000000ULL

typedef struct {
    uint8_t regs[SYSTEMCONTROL_SIZE];
    bool systick_countflag;
    uint64_t systick_last_ns;
    uint64_t systick_timer;
} SystemcontrolState;

static SystemcontrolState g_systemcontrol;

static uint32_t systemcontrol_reg_read32(SystemcontrolState *s, uint32_t off) {
    return ((uint32_t)s->regs[off + 0]) |
           ((uint32_t)s->regs[off + 1] << 8) |
           ((uint32_t)s->regs[off + 2] << 16) |
           ((uint32_t)s->regs[off + 3] << 24);
}

static void systemcontrol_reg_write32(SystemcontrolState *s, uint32_t off, uint32_t value) {
    s->regs[off + 0] = (uint8_t)(value & 0xFFU);
    s->regs[off + 1] = (uint8_t)((value >> 8) & 0xFFU);
    s->regs[off + 2] = (uint8_t)((value >> 16) & 0xFFU);
    s->regs[off + 3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint64_t systemcontrol_buf_read(SystemcontrolState *s, uint32_t off, unsigned size) {
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size && (off + i) < SYSTEMCONTROL_SIZE; i++) {
        value |= ((uint64_t)s->regs[off + i]) << (8U * i);
    }

    return value;
}

static void systemcontrol_buf_write(SystemcontrolState *s, uint32_t off, uint64_t value, unsigned size) {
    unsigned i;

    for (i = 0; i < size && (off + i) < SYSTEMCONTROL_SIZE; i++) {
        s->regs[off + i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
}

static uint64_t systemcontrol_muldiv_floor_u64(uint64_t a, uint64_t b, uint64_t c) {
    return (uint64_t)(((__uint128_t)a * (__uint128_t)b) / (__uint128_t)c);
}

static uint64_t systemcontrol_muldiv_ceil_u64(uint64_t a, uint64_t b, uint64_t c) {
    return (uint64_t)((((__uint128_t)a * (__uint128_t)b) + ((__uint128_t)c - 1U)) / (__uint128_t)c);
}

static uint64_t systemcontrol_extract_u32(uint32_t value, uint32_t off, unsigned size) {
    unsigned shift = (off & 3U) * 8U;
    uint64_t mask = ((uint64_t)1U << (size * 8U)) - 1ULL;
    return (value >> shift) & mask;
}

static uint32_t systemcontrol_merge_u32(uint32_t old_value, uint32_t off, uint64_t value, unsigned size) {
    unsigned shift = (off & 3U) * 8U;
    uint32_t lane_mask = (uint32_t)((((uint64_t)1U << (size * 8U)) - 1ULL) << shift);
    uint32_t merged = old_value & ~lane_mask;
    merged |= ((uint32_t)value << shift) & lane_mask;
    return merged;
}

static bool systemcontrol_is_word_local(uint32_t off, unsigned size, uint32_t word_off) {
    if (size == 0 || size > 4) {
        return false;
    }

    if ((off & ~3U) != word_off) {
        return false;
    }

    return ((off & 3U) + size) <= 4U;
}

static uint32_t systemcontrol_systick_hz(SystemcontrolState *s) {
    uint32_t ctrl = systemcontrol_reg_read32(s, SYSTICK_CTRL_OFF) & SYSTICK_CTRL_RW_MASK;

    if ((ctrl & SYSTICK_CTRL_CLKSOURCE) != 0U) {
        return (uint32_t)DEFAULT_CORE_CLOCK_HZ;
    }

    return (uint32_t)(DEFAULT_CORE_CLOCK_HZ / 8ULL);
}

static void systemcontrol_systick_rearm(SystemcontrolState *s);

static void systemcontrol_systick_advance(SystemcontrolState *s, bool raise_irq) {
    uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
    uint32_t ctrl = systemcontrol_reg_read32(s, SYSTICK_CTRL_OFF) & SYSTICK_CTRL_RW_MASK;
    uint32_t load = systemcontrol_reg_read32(s, SYSTICK_LOAD_OFF) & SYSTICK_RELOAD_MASK;
    uint32_t cur = systemcontrol_reg_read32(s, SYSTICK_VAL_OFF) & SYSTICK_RELOAD_MASK;
    uint64_t elapsed_ns;
    uint64_t ticks;
    uint64_t consumed_ns;
    uint64_t period;
    uint64_t wraps = 0;
    uint32_t new_cur = cur;

    if ((ctrl & SYSTICK_CTRL_ENABLE) == 0U) {
        s->systick_last_ns = now;
        return;
    }

    if (now <= s->systick_last_ns) {
        return;
    }

    elapsed_ns = now - s->systick_last_ns;
    ticks = systemcontrol_muldiv_floor_u64(elapsed_ns, (uint64_t)systemcontrol_systick_hz(s), 1000000000ULL);
    if (ticks == 0U) {
        return;
    }

    consumed_ns = systemcontrol_muldiv_floor_u64(ticks, 1000000000ULL, (uint64_t)systemcontrol_systick_hz(s));
    s->systick_last_ns += consumed_ns;

    period = (uint64_t)load + 1ULL;

    if (cur == 0U) {
        wraps = ticks / period;
        if ((ticks % period) == 0U) {
            new_cur = 0U;
        } else {
            new_cur = load - (uint32_t)((ticks % period) - 1ULL);
        }
    } else if (ticks < (uint64_t)cur) {
        new_cur = cur - (uint32_t)ticks;
    } else {
        uint64_t after_zero = ticks - (uint64_t)cur;
        wraps = 1ULL + (after_zero / period);
        if ((after_zero % period) == 0U) {
            new_cur = 0U;
        } else {
            new_cur = load - (uint32_t)((after_zero % period) - 1ULL);
        }
    }

    systemcontrol_reg_write32(s, SYSTICK_VAL_OFF, new_cur & SYSTICK_RELOAD_MASK);

    if (wraps != 0U) {
        s->systick_countflag = true;
        if (raise_irq && ((ctrl & SYSTICK_CTRL_TICKINT) != 0U)) {
            qemu_plugin_raise_irq(SYSTICK_IRQ_LINE, false);
        }
    }
}

static void systemcontrol_systick_timer_cb(void *opaque) {
    SystemcontrolState *s = (SystemcontrolState *)opaque;

    systemcontrol_systick_advance(s, true);
    systemcontrol_systick_rearm(s);
}

static void systemcontrol_systick_rearm(SystemcontrolState *s) {
    uint32_t ctrl = systemcontrol_reg_read32(s, SYSTICK_CTRL_OFF) & SYSTICK_CTRL_RW_MASK;
    uint32_t load;
    uint32_t cur;
    uint64_t ticks_to_wrap;
    uint64_t delay_ns;
    uint64_t now;

    if (s->systick_timer == 0U) {
        return;
    }

    if ((ctrl & SYSTICK_CTRL_ENABLE) == 0U || (ctrl & SYSTICK_CTRL_TICKINT) == 0U) {
        return;
    }

    load = systemcontrol_reg_read32(s, SYSTICK_LOAD_OFF) & SYSTICK_RELOAD_MASK;
    cur = systemcontrol_reg_read32(s, SYSTICK_VAL_OFF) & SYSTICK_RELOAD_MASK;
    ticks_to_wrap = (cur == 0U) ? ((uint64_t)load + 1ULL) : (uint64_t)cur;
    delay_ns = systemcontrol_muldiv_ceil_u64(ticks_to_wrap, 1000000000ULL, (uint64_t)systemcontrol_systick_hz(s));
    if (delay_ns == 0U) {
        delay_ns = 1U;
    }

    now = (uint64_t)qemu_plugin_get_virtual_timer();
    qemu_plugin_timer_alarm(s->systick_timer, now + delay_ns);
}

void* systemcontrol_init(ConfigSection* model_info) {
    SystemcontrolState *s = &g_systemcontrol;

    (void)model_info;

    memset(s, 0, sizeof(*s));

    /* Plausible ARMv7-M SCB reset state for common CMSIS helpers. */
    systemcontrol_reg_write32(s, SCB_CPUID_OFF, 0x411FC272U);
    systemcontrol_reg_write32(s, SCB_CCR_OFF, 0x00000200U);
    systemcontrol_reg_write32(s, SCB_CPACR_OFF, 0x00000000U);
    systemcontrol_reg_write32(s, SYSTICK_CALIB_OFF, 0x00000000U);

    s->systick_last_ns = (uint64_t)qemu_plugin_get_virtual_timer();
    s->systick_timer = qemu_plugin_timer_new_ns(systemcontrol_systick_timer_cb, s);

    return s;
}

uint64_t systemcontrol_read(void *opaque, uint64_t addr, unsigned size) {
    SystemcontrolState *s = (SystemcontrolState *)opaque;
    uint32_t off;

    if (s == NULL) {
        s = &g_systemcontrol;
    }

    if (size == 0U) {
        return 0;
    }

    if (addr < SYSTEMCONTROL_BASE || addr >= (SYSTEMCONTROL_BASE + SYSTEMCONTROL_SIZE)) {
        return 0;
    }

    off = (uint32_t)(addr - SYSTEMCONTROL_BASE);

    if (systemcontrol_is_word_local(off, size, SYSTICK_CTRL_OFF)) {
        uint32_t ctrl;

        systemcontrol_systick_advance(s, true);
        ctrl = systemcontrol_reg_read32(s, SYSTICK_CTRL_OFF) & SYSTICK_CTRL_RW_MASK;
        if (s->systick_countflag) {
            ctrl |= SYSTICK_CTRL_COUNTFLAG;
        }

        s->systick_countflag = false;
        return systemcontrol_extract_u32(ctrl, off, size);
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_LOAD_OFF)) {
        systemcontrol_systick_advance(s, true);
        return systemcontrol_extract_u32(systemcontrol_reg_read32(s, SYSTICK_LOAD_OFF) & SYSTICK_RELOAD_MASK, off, size);
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_VAL_OFF)) {
        systemcontrol_systick_advance(s, true);
        return systemcontrol_extract_u32(systemcontrol_reg_read32(s, SYSTICK_VAL_OFF) & SYSTICK_RELOAD_MASK, off, size);
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_CALIB_OFF)) {
        return systemcontrol_extract_u32(systemcontrol_reg_read32(s, SYSTICK_CALIB_OFF), off, size);
    }

    return systemcontrol_buf_read(s, off, size);
}

void systemcontrol_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SystemcontrolState *s = (SystemcontrolState *)opaque;
    uint32_t off;

    if (s == NULL) {
        s = &g_systemcontrol;
    }

    if (size == 0U) {
        return;
    }

    if (addr < SYSTEMCONTROL_BASE || addr >= (SYSTEMCONTROL_BASE + SYSTEMCONTROL_SIZE)) {
        return;
    }

    off = (uint32_t)(addr - SYSTEMCONTROL_BASE);

    if (systemcontrol_is_word_local(off, size, SYSTICK_CTRL_OFF)) {
        uint32_t old_ctrl;
        uint32_t new_ctrl;

        systemcontrol_systick_advance(s, true);
        old_ctrl = systemcontrol_reg_read32(s, SYSTICK_CTRL_OFF) & SYSTICK_CTRL_RW_MASK;
        new_ctrl = systemcontrol_merge_u32(old_ctrl, off, value, size) & SYSTICK_CTRL_RW_MASK;
        systemcontrol_reg_write32(s, SYSTICK_CTRL_OFF, new_ctrl);
        s->systick_last_ns = (uint64_t)qemu_plugin_get_virtual_timer();
        systemcontrol_systick_rearm(s);
        return;
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_LOAD_OFF)) {
        uint32_t old_load;
        uint32_t new_load;
        uint32_t cur;

        systemcontrol_systick_advance(s, true);
        old_load = systemcontrol_reg_read32(s, SYSTICK_LOAD_OFF) & SYSTICK_RELOAD_MASK;
        new_load = systemcontrol_merge_u32(old_load, off, value, size) & SYSTICK_RELOAD_MASK;
        systemcontrol_reg_write32(s, SYSTICK_LOAD_OFF, new_load);

        cur = systemcontrol_reg_read32(s, SYSTICK_VAL_OFF) & SYSTICK_RELOAD_MASK;
        if (cur > new_load) {
            systemcontrol_reg_write32(s, SYSTICK_VAL_OFF, new_load);
        }

        s->systick_last_ns = (uint64_t)qemu_plugin_get_virtual_timer();
        systemcontrol_systick_rearm(s);
        return;
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_VAL_OFF)) {
        systemcontrol_systick_advance(s, true);
        systemcontrol_reg_write32(s, SYSTICK_VAL_OFF, 0U);
        s->systick_countflag = false;
        s->systick_last_ns = (uint64_t)qemu_plugin_get_virtual_timer();
        systemcontrol_systick_rearm(s);
        return;
    }

    if (systemcontrol_is_word_local(off, size, SYSTICK_CALIB_OFF) ||
        systemcontrol_is_word_local(off, size, SCB_CPUID_OFF)) {
        return;
    }

    systemcontrol_buf_write(s, off, value, size);
}