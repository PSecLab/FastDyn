// Device Model for TMR2

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Trace/Manual-derived register layout:
//   0x00 CNT    : counter register, software readable/writable
//   0x04 CMP    : compare register, software readable/writable
//   0x08 PWM    : PWM register, software readable/writable
//   0x0C INTFL  : interrupt/status flags, W1C from software but re-asserted by live timer state
//   0x10 CTRL0  : timer control/configuration
//   0x14 NOLCMP : auxiliary compare register
//   0x18 CTRL1  : timer control/configuration; bit31 is sticky, not self-clearing

#define TMR2_BASE           0x40012000ULL

#define TMR2_REG_CNT        0x00
#define TMR2_REG_CMP        0x04
#define TMR2_REG_PWM        0x08
#define TMR2_REG_INTFL      0x0C
#define TMR2_REG_CTRL0      0x10
#define TMR2_REG_NOLCMP     0x14
#define TMR2_REG_CTRL1      0x18

// Trace-derived live status:
//   When firmware places the timer into the active state via CTRL1[31], INTFL
//   reads back as 0x01000100 in this execution path.
#define TMR2_CTRL1_ACTIVE       0x80000000u
#define TMR2_INTFL_FLAG_LO      0x00000100u
#define TMR2_INTFL_FLAG_HI      0x01000000u
#define TMR2_INTFL_SYNTH_MASK   (TMR2_INTFL_FLAG_LO | TMR2_INTFL_FLAG_HI)

typedef struct {
    uint32_t cnt;
    uint32_t cmp;
    uint32_t pwm;
    uint32_t intfl;
    uint32_t ctrl0;
    uint32_t nolcmp;
    uint32_t ctrl1;
} TMR2State;

static TMR2State g_tmr2;

static uint64_t tmr2_mask_by_size(uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        return val & 0xFFu;
    case 2:
        return val & 0xFFFFu;
    case 4:
    default:
        return val & 0xFFFFFFFFu;
    }
}

static void tmr2_debug_bad_access(const char *kind, hwaddr addr, unsigned size, uint64_t value)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "TMR2: %s unhandled addr=0x%08llx size=%u value=0x%08llx",
             kind,
             (unsigned long long)addr,
             size,
             (unsigned long long)value);
    dev_debug(buf);
}

static bool tmr2_is_valid_reg(hwaddr reg)
{
    switch (reg) {
    case TMR2_REG_CNT:
    case TMR2_REG_CMP:
    case TMR2_REG_PWM:
    case TMR2_REG_INTFL:
    case TMR2_REG_CTRL0:
    case TMR2_REG_NOLCMP:
    case TMR2_REG_CTRL1:
        return true;
    default:
        return false;
    }
}

static void tmr2_refresh_status(TMR2State *s)
{
    /*
     * Hardware keeps CTRL1[31] set after software writes it, and INTFL remains
     * asserted in the active state. The old model incorrectly treated bit31 as
     * a self-clearing strobe plus delayed one-shot event.
     */
    if (s->ctrl1 & TMR2_CTRL1_ACTIVE) {
        s->intfl |= TMR2_INTFL_SYNTH_MASK;
    } else {
        s->intfl &= ~TMR2_INTFL_SYNTH_MASK;
    }
}

static uint32_t tmr2_read_reg32(TMR2State *s, hwaddr reg)
{
    switch (reg) {
    case TMR2_REG_CNT:
        return s->cnt;

    case TMR2_REG_CMP:
        return s->cmp;

    case TMR2_REG_PWM:
        return s->pwm;

    case TMR2_REG_INTFL:
        tmr2_refresh_status(s);
        return s->intfl;

    case TMR2_REG_CTRL0:
        return s->ctrl0;

    case TMR2_REG_NOLCMP:
        return s->nolcmp;

    case TMR2_REG_CTRL1: {
        uint32_t ro = 0;
        if (s->ctrl0 & 0x4000u) ro |= 0x8u; /* CTRL0 bit 14 → CTRL1 bit 3 */
        if (s->ctrl0 & 0x8000u) ro |= 0x4u; /* CTRL0 bit 15 → CTRL1 bit 2 (clk-domain sync ack) */
        return s->ctrl1 | ro;
    }

    default:
        return 0;
    }
}

static void tmr2_write_reg32(TMR2State *s, hwaddr reg, uint32_t value, uint32_t wmask)
{
    switch (reg) {
    case TMR2_REG_CNT:
        s->cnt = (s->cnt & ~wmask) | (value & wmask);
        break;

    case TMR2_REG_CMP:
        s->cmp = (s->cmp & ~wmask) | (value & wmask);
        break;

    case TMR2_REG_PWM:
        s->pwm = (s->pwm & ~wmask) | (value & wmask);
        break;

    case TMR2_REG_INTFL:
        s->intfl &= ~((uint32_t)(value & wmask));
        tmr2_refresh_status(s);
        break;

    case TMR2_REG_CTRL0:
        s->ctrl0 = (s->ctrl0 & ~wmask) | (value & wmask);
        break;

    case TMR2_REG_NOLCMP:
        s->nolcmp = (s->nolcmp & ~wmask) | (value & wmask);
        break;

    case TMR2_REG_CTRL1:
        s->ctrl1 = (s->ctrl1 & ~wmask) | (value & wmask);
        tmr2_refresh_status(s);
        break;

    default:
        break;
    }
}

// This function will emulate all device reads
uint64_t tmr2_read(void *opaque, hwaddr addr, unsigned size)
{
    TMR2State *s = (TMR2State *)opaque;
    hwaddr offset = addr - TMR2_BASE;
    hwaddr reg = offset & ~0x3ULL;
    unsigned shift = (unsigned)((offset & 0x3ULL) * 8);

    uint32_t regval = tmr2_read_reg32(s, reg);
    uint64_t rval = ((uint64_t)regval >> shift);

    // Debug unexpected non-register-aligned accesses, but still serve them.
    if (!tmr2_is_valid_reg(reg)) {
        tmr2_debug_bad_access("read", addr, size, 0);
    }

    return tmr2_mask_by_size(rval, size);
}

// This function will emulate all device writes
void tmr2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    TMR2State *s = (TMR2State *)opaque;
    hwaddr offset = addr - TMR2_BASE;
    hwaddr reg = offset & ~0x3ULL;
    unsigned shift = (unsigned)((offset & 0x3ULL) * 8);

    uint32_t wmask;
    uint32_t wval;

    switch (size) {
    case 1:
        wmask = 0xFFu << shift;
        wval = ((uint32_t)(value & 0xFFu)) << shift;
        break;
    case 2:
        wmask = 0xFFFFu << shift;
        wval = ((uint32_t)(value & 0xFFFFu)) << shift;
        break;
    case 4:
    default:
        wmask = 0xFFFFFFFFu;
        wval = (uint32_t)value;
        break;
    }

    if (!tmr2_is_valid_reg(reg)) {
        tmr2_debug_bad_access("write", addr, size, value);
        return;
    }

    tmr2_write_reg32(s, reg, wval, wmask);
}

void* tmr2_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_tmr2, 0, sizeof(g_tmr2));
    tmr2_refresh_status(&g_tmr2);

    return &g_tmr2;
}
