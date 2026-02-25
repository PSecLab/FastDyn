// Device Model for GPIOG (STM32F429)
// Focus: MODER + ODR statefulness and correct address normalization.
//
// Inferred Register Functions:
//  - MODER (0x00): mode select (2 bits per pin). Trace writes 0x55000000.
//  - ODR   (0x14): output latch. Trace writes 0xF000 and expects reads to return 0xF000.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <device.h>
#include <boardrunner/vio.h>

#define GPIOG_BASE   0x40021800ULL
#define GPIOG_SIZE   0x400ULL   // enough to cover GPIO register block

enum {
    GPIO_MODER = 0x00,
    GPIO_ODR   = 0x14,
};

typedef struct {
    uint32_t moder;
    uint32_t odr;
    int log_level; // 0=quiet, 1=key, 2=verbose
} gpiog_state_t;

static gpiog_state_t g_gpiog;

static void dbg(gpiog_state_t *s, int lvl, const char *fmt, ...) {
    if (!s || s->log_level < lvl) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

/*
 * Normalize MMIO address:
 * - Some frameworks pass addr as an OFFSET within the device region (0x0..)
 * - Others pass ABSOLUTE system address (e.g., 0x40021814)
 *
 * The mismatch you showed (ODR read returning 0) commonly happens when the model
 * assumes offset but receives absolute (or vice versa). We handle both.
 */
static inline uint32_t norm_off(hwaddr addr) {
    uint64_t a = (uint64_t)addr;
    if (a >= GPIOG_BASE && a < (GPIOG_BASE + GPIOG_SIZE)) {
        return (uint32_t)(a - GPIOG_BASE);
    }
    // Treat as already-an-offset
    return (uint32_t)a;
}

// Sub-word read helper (supports byte/halfword reads safely)
static uint64_t read_u32_masked(uint32_t v, uint32_t off, unsigned size) {
    if (size >= 4) return (uint64_t)v;
    unsigned shift = (off & 3u) * 8u;
    uint32_t mask = (size == 1) ? 0xFFu : 0xFFFFu;
    return (uint64_t)((v >> shift) & mask);
}

// Sub-word write helper (supports byte/halfword writes safely)
static void write_u32_masked(uint32_t *reg, uint32_t off, uint64_t value, unsigned size) {
    if (!reg) return;
    if (size >= 4) {
        *reg = (uint32_t)value;
        return;
    }
    unsigned shift = (off & 3u) * 8u;
    uint32_t mask = (size == 1) ? 0xFFu : 0xFFFFu;
    uint32_t v = *reg;
    v &= ~(mask << shift);
    v |= (((uint32_t)value & mask) << shift);
    *reg = v;
}

// This function will emulate all device reads
uint64_t gpiog_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    gpiog_state_t *s = &g_gpiog;

    uint32_t off = norm_off(addr);
    uint32_t reg_off = off & ~0x3u;

    uint64_t ret = 0;
    switch (reg_off) {
        case GPIO_MODER:
            ret = read_u32_masked(s->moder, off, size);
            break;
        case GPIO_ODR:
            // Critical for your discrepancy: must return the latched ODR value.
            ret = read_u32_masked(s->odr, off, size);
            break;
        default:
            ret = 0;
            break;
    }

    dbg(s, 2, "[GPIOG] RD addr=0x%llx off=0x%02x size=%u -> 0x%llx\n",
        (unsigned long long)(uint64_t)addr, off, size, (unsigned long long)ret);
    return ret;
}

// This function will emulate all device writes
void gpiog_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    gpiog_state_t *s = &g_gpiog;

    uint32_t off = norm_off(addr);
    uint32_t reg_off = off & ~0x3u;

    dbg(s, 2, "[GPIOG] WR addr=0x%llx off=0x%02x size=%u val=0x%llx\n",
        (unsigned long long)(uint64_t)addr, off, size, (unsigned long long)value);

    switch (reg_off) {
        case GPIO_MODER:
            write_u32_masked(&s->moder, off, value, size);
            break;

        case GPIO_ODR:
            // Critical for your discrepancy: store the write so next read returns it.
            write_u32_masked(&s->odr, off, value, size);
            dbg(s, 1, "[GPIOG] ODR latched = 0x%08x\n", s->odr);
            break;

        default:
            // Ignore unknown writes (safe behavior for this trace-limited model)
            break;
    }
}

void gpiog_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_gpiog, 0, sizeof(g_gpiog));

    // Trace shows POR reads as 0, so keep defaults at 0.
    g_gpiog.moder = 0x00000000u;
    g_gpiog.odr   = 0x00000000u;

    // Optional env knob
    const char *ll = getenv("GPIOG_LOG_LEVEL");
    g_gpiog.log_level = ll ? atoi(ll) : 1;

    dev_debug("[GPIOG] init done (handles absolute-or-offset MMIO addr)\n");
}
