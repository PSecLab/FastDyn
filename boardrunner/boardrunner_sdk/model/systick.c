#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define SYSTICK_BASE         0xE000E010ULL

#define SYST_CSR_OFF         0x00
#define SYST_RVR_OFF         0x04
#define SYST_CVR_OFF         0x08
#define SYST_CALIB_OFF       0x0C

#define SYST_CSR_ENABLE      (1U << 0)
#define SYST_CSR_TICKINT     (1U << 1)
#define SYST_CSR_CLKSOURCE   (1U << 2)
#define SYST_CSR_COUNTFLAG   (1U << 16)

#define SYST_24BIT_MASK      0x00FFFFFFU

/*
 * Approximate CubeBlack core clock for SysTick timebase.
 * SysTick is not the primary scheduler source in this target, but a live
 * counter model avoids pathological polling behaviour if firmware touches it.
 */
#define SYSTICK_CLK_HZ       168000000ULL

typedef struct SysTickState {
    uint32_t csr;
    uint32_t rvr;
    uint32_t cvr;
    uint32_t calib;
    int64_t last_ns;
    uint64_t frac;
    bool countflag;
} SysTickState;

static SysTickState g_systick;

static uint32_t systick_mask_by_size(uint32_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xFFU;
    case 2:
        return value & 0xFFFFU;
    default:
        return value;
    }
}

static void systick_sync(SysTickState *s) {
    int64_t now;
    uint64_t delta_ns;
    uint64_t numer;
    uint64_t ticks;
    uint32_t period;
    uint32_t cur;
    uint32_t rem;

    if (s == NULL) {
        return;
    }

    now = qemu_plugin_get_virtual_timer();

    if ((s->csr & SYST_CSR_ENABLE) == 0U || s->rvr == 0U) {
        s->last_ns = now;
        s->frac = 0;
        return;
    }

    if (now <= s->last_ns) {
        return;
    }

    delta_ns = (uint64_t)(now - s->last_ns);
    numer = s->frac + delta_ns * SYSTICK_CLK_HZ;
    ticks = numer / 1000000000ULL;
    s->frac = numer % 1000000000ULL;
    s->last_ns = now;

    if (ticks == 0U) {
        return;
    }

    period = (s->rvr & SYST_24BIT_MASK) + 1U;
    cur = s->cvr & SYST_24BIT_MASK;

    if (period == 0U) {
        s->cvr = 0;
        return;
    }

    /*
     * Minimal modulo down-counter model:
     * current value decrements modulo (LOAD+1), and COUNTFLAG latches if one
     * or more wrap events occurred while advancing.
     */
    rem = (uint32_t)(ticks % period);
    if (ticks >= (uint64_t)(cur + 1U)) {
        s->countflag = true;
    }
    if (rem > cur) {
        s->countflag = true;
        s->cvr = (period - (rem - cur)) % period;
    } else {
        s->cvr = cur - rem;
    }

    s->cvr &= SYST_24BIT_MASK;
}

void* systick_init(ConfigSection *model_info) {
    (void)model_info;

    memset(&g_systick, 0, sizeof(g_systick));
    g_systick.last_ns = qemu_plugin_get_virtual_timer();
    g_systick.calib = 0;
    return &g_systick;
}

uint64_t systick_read(void *opaque, uint64_t addr, unsigned size) {
    SysTickState *s = (SysTickState *)opaque;
    uint64_t offset = addr - SYSTICK_BASE;
    uint32_t value = 0;

    if (s == NULL) {
        s = &g_systick;
    }

    switch (offset) {
    case SYST_CSR_OFF:
        systick_sync(s);
        value = s->csr & (SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE);
        if (s->countflag) {
            value |= SYST_CSR_COUNTFLAG;
            s->countflag = false;
        }
        break;
    case SYST_RVR_OFF:
        value = s->rvr & SYST_24BIT_MASK;
        break;
    case SYST_CVR_OFF:
        systick_sync(s);
        value = s->cvr & SYST_24BIT_MASK;
        break;
    case SYST_CALIB_OFF:
        value = s->calib;
        break;
    default:
        value = 0;
        break;
    }

    return systick_mask_by_size(value, size);
}

void systick_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {
    SysTickState *s = (SysTickState *)opaque;
    uint64_t offset = addr - SYSTICK_BASE;
    uint32_t v = systick_mask_by_size((uint32_t)value, size);

    if (s == NULL) {
        s = &g_systick;
    }

    switch (offset) {
    case SYST_CSR_OFF:
        systick_sync(s);
        s->csr = v & (SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE);
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac = 0;
        break;
    case SYST_RVR_OFF:
        systick_sync(s);
        s->rvr = v & SYST_24BIT_MASK;
        break;
    case SYST_CVR_OFF:
        /*
         * ARM SysTick clears the current value and COUNTFLAG on write.
         * After this, the next decrement cycle restarts from LOAD.
         */
        s->cvr = 0;
        s->countflag = false;
        s->last_ns = qemu_plugin_get_virtual_timer();
        s->frac = 0;
        break;
    case SYST_CALIB_OFF:
        /* read-only */
        break;
    default:
        break;
    }
}
