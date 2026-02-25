// Device Model for GPIOI (STM32F7x9)
//
// Based ONLY on provided trace data:
//  - Configure PI15: OSPEEDR[31:30]=11, OTYPER[15]=0, PUPDR[31:30]=01, MODER[31:30]=01
//  - Runtime loop: read ODR, write BSRR=0x8000 (set PI15 high)
//
// This model implements:
//  - Register storage for MODER/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR (+ a few harmless extras)
//  - Correct BSRR semantics: lower 16 bits set, upper 16 bits reset
//  - IDR behavior: mirrors ODR for pins configured as output; otherwise uses stored IDR with optional pull-up/down synthesis
//
// No IRQ/EXTI behavior is modeled because ISR analysis is "None" and no EXTI registers appear in trace.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef HWADDR_MAX
#define HWADDR_MAX (~(hwaddr)0)
#endif

#define GPIOI_BASE 0x40022000ULL

// GPIO register offsets (STM32F7 GPIO)
#define GPIO_MODER   0x00
#define GPIO_OTYPER  0x04
#define GPIO_OSPEEDR 0x08
#define GPIO_PUPDR   0x0C
#define GPIO_IDR     0x10
#define GPIO_ODR     0x14
#define GPIO_BSRR    0x18
#define GPIO_LCKR    0x1C
#define GPIO_AFRL    0x20
#define GPIO_AFRH    0x24

typedef struct {
    uint32_t MODER;
    uint32_t OTYPER;
    uint32_t OSPEEDR;
    uint32_t PUPDR;
    uint32_t IDR;     // stored "external" input state (if ever extended)
    uint32_t ODR;
    uint32_t BSRR;    // last written value (optional visibility)
    uint32_t LCKR;
    uint32_t AFRL;
    uint32_t AFRH;

    // Debug toggle (keep default 0 to avoid log spam)
    int log_level;
} gpioi_state_t;

static gpioi_state_t g_gpioi;

static inline void dbg(gpioi_state_t *s, int level, const char *fmt, ...) {
    if (!s || s->log_level < level) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static inline uint32_t mmio_offset(hwaddr addr) {
    // Some frameworks pass absolute address, others pass offset into region.
    if (addr >= (hwaddr)GPIOI_BASE && addr < (hwaddr)(GPIOI_BASE + 0x400)) {
        return (uint32_t)(addr - (hwaddr)GPIOI_BASE);
    }
    return (uint32_t)addr;
}

static inline uint32_t mask_for_size(unsigned size) {
    switch (size) {
        case 1: return 0xFFu;
        case 2: return 0xFFFFu;
        case 4: return 0xFFFFFFFFu;
        default: return 0xFFFFFFFFu;
    }
}

static inline uint32_t extract_by_size(uint32_t reg, hwaddr addr, unsigned size) {
    uint32_t shift = ((uint32_t)addr & 0x3u) * 8u;
    uint32_t m = mask_for_size(size);
    return (reg >> shift) & m;
}

static inline uint32_t merge_by_size(uint32_t oldv, hwaddr addr, uint32_t val, unsigned size) {
    uint32_t shift = ((uint32_t)addr & 0x3u) * 8u;
    uint32_t m = mask_for_size(size);
    uint32_t masked = (val & m) << shift;
    uint32_t clear_mask = ~(m << shift);
    return (oldv & clear_mask) | masked;
}

// Compute IDR view:
// - For output pins (MODER=01): mirror ODR bit
// - For input pins (MODER=00): if stored IDR bit is 0, synthesize pull-up/down (optional)
// - Otherwise: use stored IDR bit
static uint32_t gpioi_compute_idr(gpioi_state_t *s) {
    uint32_t idr = s->IDR;

    for (int pin = 0; pin < 16; pin++) {
        uint32_t mode = (s->MODER >> (pin * 2)) & 0x3u;

        if (mode == 0x1u) {
            // output: mirror ODR level
            uint32_t od = (s->ODR >> pin) & 0x1u;
            idr = (idr & ~(1u << pin)) | (od << pin);
        } else if (mode == 0x0u) {
            // input: if nothing externally driving, use pulls as a sane default
            uint32_t ext = (idr >> pin) & 0x1u;
            if (ext == 0) {
                uint32_t pupd = (s->PUPDR >> (pin * 2)) & 0x3u;
                if (pupd == 0x1u) {         // pull-up
                    idr |= (1u << pin);
                } else if (pupd == 0x2u) {  // pull-down
                    idr &= ~(1u << pin);
                }
            }
        }
    }

    return idr;
}

static void gpioi_apply_bsrr(gpioi_state_t *s, uint32_t bsrr_val) {
    // Lower 16 bits: set corresponding ODR bits
    // Upper 16 bits: reset corresponding ODR bits
    uint32_t set_mask   = bsrr_val & 0x0000FFFFu;
    uint32_t reset_mask = (bsrr_val >> 16) & 0x0000FFFFu;

    s->ODR |= set_mask;
    s->ODR &= ~reset_mask;
}

// This function will emulate all device reads
uint64_t gpioi_read(void *opaque, hwaddr addr, unsigned size) {
    gpioi_state_t *s = (gpioi_state_t *)opaque;
    if (!s) s = &g_gpioi;

    uint32_t off = mmio_offset(addr);
    uint32_t regv = 0;

    switch (off & ~0x3u) {
        case GPIO_MODER:   regv = s->MODER; break;
        case GPIO_OTYPER:  regv = s->OTYPER; break;
        case GPIO_OSPEEDR: regv = s->OSPEEDR; break;
        case GPIO_PUPDR:   regv = s->PUPDR; break;
        case GPIO_IDR:     regv = gpioi_compute_idr(s); break;
        case GPIO_ODR:     regv = s->ODR; break;
        case GPIO_BSRR:    regv = s->BSRR; break; // readback not architecturally meaningful, but safe
        case GPIO_LCKR:    regv = s->LCKR; break;
        case GPIO_AFRL:    regv = s->AFRL; break;
        case GPIO_AFRH:    regv = s->AFRH; break;
        default:
            // Unused/unknown offsets: return 0 (matches trace reads being 0 on control regs at boot)
            regv = 0;
            break;
    }

    uint32_t sliced = extract_by_size(regv, (hwaddr)(off), size);

    dbg(s, 2, "[GPIOI] READ  off=0x%03X size=%u -> 0x%08X\n", off, size, sliced);
    return (uint64_t)sliced;
}

// This function will emulate all device writes
void gpioi_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    gpioi_state_t *s = (gpioi_state_t *)opaque;
    if (!s) s = &g_gpioi;

    uint32_t off = mmio_offset(addr);
    uint32_t v32 = (uint32_t)value;

    dbg(s, 2, "[GPIOI] WRITE off=0x%03X size=%u val=0x%08X\n", off, size, v32);

    switch (off & ~0x3u) {
        case GPIO_MODER:
            s->MODER = merge_by_size(s->MODER, (hwaddr)off, v32, size);
            return;

        case GPIO_OTYPER:
            // Only lower 16 bits used on real HW; keep it masked.
            s->OTYPER = merge_by_size(s->OTYPER, (hwaddr)off, v32, size) & 0x0000FFFFu;
            return;

        case GPIO_OSPEEDR:
            s->OSPEEDR = merge_by_size(s->OSPEEDR, (hwaddr)off, v32, size);
            return;

        case GPIO_PUPDR:
            s->PUPDR = merge_by_size(s->PUPDR, (hwaddr)off, v32, size);
            return;

        case GPIO_IDR:
            // Typically read-only in real HW. Accepting writes is harmless; can help tests.
            s->IDR = merge_by_size(s->IDR, (hwaddr)off, v32, size) & 0x0000FFFFu;
            return;

        case GPIO_ODR:
            // ODR is writable. Only lower 16 bits meaningful.
            s->ODR = merge_by_size(s->ODR, (hwaddr)off, v32, size) & 0x0000FFFFu;
            return;

        case GPIO_BSRR: {
            // Apply atomic set/reset.
            uint32_t merged = merge_by_size(s->BSRR, (hwaddr)off, v32, size);
            s->BSRR = merged;

            // Trace writes fullword 0x00008000 to set PI15 high.
            gpioi_apply_bsrr(s, merged);
            return;
        }

        case GPIO_LCKR:
            s->LCKR = merge_by_size(s->LCKR, (hwaddr)off, v32, size);
            return;

        case GPIO_AFRL:
            s->AFRL = merge_by_size(s->AFRL, (hwaddr)off, v32, size);
            return;

        case GPIO_AFRH:
            s->AFRH = merge_by_size(s->AFRH, (hwaddr)off, v32, size);
            return;

        default:
            // Ignore unknown writes
            return;
    }
}

void gpioi_init(ConfigSection* model_info) {
    (void)model_info;

    memset(&g_gpioi, 0, sizeof(g_gpioi));

    // Keep defaults as 0 to match trace reads during init (MODER/OTYPER/PUPDR/OSPEEDR were read as 0).
    g_gpioi.log_level = 0;

    dev_debug("[GPIOI] init: state cleared (trace-aligned reset=0)\n");
}
