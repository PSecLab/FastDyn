// Device Model for GPIOC (STM32F7x9)
//
// Inferred Register Functions (from provided trace):
// - MODER  @ 0x00 : mode register (stateful; RMW observed)
// - PUPDR  @ 0x0C : pull-up/pull-down register (stateful; RMW observed)
//
// Notes:
// - This model primarily provides stateful storage for MODER/PUPDR as required by the trace.
// - For robustness, it also implements a minimal shadow for common GPIO registers and basic
//   BSRR -> ODR behavior. If your firmware never touches those registers, they remain unused.
// - No interrupts are generated (none indicated in isr_analysis).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

// GPIO register offsets (standard STM32 GPIO block layout)
enum {
    GPIO_MODER   = 0x00,
    GPIO_OTYPER  = 0x04,
    GPIO_OSPEEDR = 0x08,
    GPIO_PUPDR   = 0x0C,
    GPIO_IDR     = 0x10,
    GPIO_ODR     = 0x14,
    GPIO_BSRR    = 0x18,
    GPIO_LCKR    = 0x1C,
    GPIO_AFRL    = 0x20,
    GPIO_AFRH    = 0x24,
};

typedef struct {
    // Core config regs seen in trace
    uint32_t MODER;
    uint32_t PUPDR;

    // Additional common GPIO state (shadowed)
    uint32_t OTYPER;
    uint32_t OSPEEDR;
    uint32_t IDR;    // input data shadow (can be tied to ODR for outputs)
    uint32_t ODR;    // output data shadow
    uint32_t LCKR;
    uint32_t AFRL;
    uint32_t AFRH;

    // Debug control
    int log_level;   // 0 = quiet, 1 = basic, 2 = verbose
} gpioc_state_t;

static gpioc_state_t g_gpioc;

static void gpioc_log(gpioc_state_t *s, int lvl, const char *fmt, ...)
{
    if (!s || s->log_level < lvl) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static uint32_t read_u32_sized(uint32_t full, unsigned size, unsigned addr_low2)
{
    // addr_low2 is (addr & 3)
    // Return subword for 8/16-bit accesses, little-endian.
    if (size == 4) return full;

    unsigned shift = (addr_low2 & 3u) * 8u;
    if (size == 2) {
        shift &= ~8u; // align to halfword boundary
        return (full >> shift) & 0xFFFFu;
    }
    if (size == 1) {
        return (full >> shift) & 0xFFu;
    }
    // Unknown size: fall back
    return full;
}

static uint32_t write_u32_sized(uint32_t prev, uint32_t value, unsigned size, unsigned addr_low2)
{
    // Merge subword writes into 32-bit register value.
    if (size == 4) return value;

    unsigned shift = (addr_low2 & 3u) * 8u;
    if (size == 2) {
        shift &= ~8u;
        uint32_t mask = 0xFFFFu << shift;
        return (prev & ~mask) | ((value & 0xFFFFu) << shift);
    }
    if (size == 1) {
        uint32_t mask = 0xFFu << shift;
        return (prev & ~mask) | ((value & 0xFFu) << shift);
    }
    // Unknown size: overwrite
    return value;
}

// Basic behavior: reflect output bits into IDR for pins configured as output.
// This is conservative and avoids introducing randomness without input modeling.
static void gpioc_refresh_idr(gpioc_state_t *s)
{
    // MODER: 2 bits per pin. Output mode is 0b01.
    // For pins in output mode, reflect ODR into IDR. Others read as 0 unless user sets IDR.
    uint32_t idr = s->IDR;
    for (int pin = 0; pin < 16; pin++) {
        uint32_t mode = (s->MODER >> (pin * 2)) & 0x3u;
        if (mode == 0x1u) { // output
            uint32_t bit = (s->ODR >> pin) & 0x1u;
            idr = (idr & ~(1u << pin)) | (bit << pin);
        }
    }
    s->IDR = idr;
}

// This function will emulate all device reads
uint64_t gpioc_read(void *opaque, hwaddr addr, unsigned size)
{
    gpioc_state_t *s = (opaque != NULL) ? (gpioc_state_t *)opaque : &g_gpioc;
    uint32_t off = (uint32_t)addr;

    // keep IDR consistent with ODR for output pins
    gpioc_refresh_idr(s);

    uint32_t full = 0;
    switch (off & ~3u) {
        case GPIO_MODER:   full = s->MODER;   break;
        case GPIO_OTYPER:  full = s->OTYPER;  break;
        case GPIO_OSPEEDR: full = s->OSPEEDR; break;
        case GPIO_PUPDR:   full = s->PUPDR;   break;
        case GPIO_IDR:     full = s->IDR;     break;
        case GPIO_ODR:     full = s->ODR;     break;
        case GPIO_LCKR:    full = s->LCKR;    break;
        case GPIO_AFRL:    full = s->AFRL;    break;
        case GPIO_AFRH:    full = s->AFRH;    break;

        // BSRR is write-only in real HW; return 0 to be safe.
        case GPIO_BSRR:    full = 0;          break;

        default:
            // Unknown/unused offset based on provided trace: return 0.
            full = 0;
            gpioc_log(s, 2, "[GPIOC] READ  off=0x%03X size=%u -> 0x%08X (default)\n",
                      off, size, full);
            return read_u32_sized(full, size, off & 3u);
    }

    uint32_t ret = read_u32_sized(full, size, off & 3u);
    gpioc_log(s, 2, "[GPIOC] READ  off=0x%03X size=%u -> 0x%08X\n", off, size, ret);
    return (uint64_t)ret;
}

// This function will emulate all device writes
void gpioc_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    gpioc_state_t *s = (opaque != NULL) ? (gpioc_state_t *)opaque : &g_gpioc;
    uint32_t off = (uint32_t)addr;
    uint32_t v32 = (uint32_t)value;

    switch (off & ~3u) {
        case GPIO_MODER:
            s->MODER = write_u32_sized(s->MODER, v32, size, off & 3u);
            gpioc_log(s, 1, "[GPIOC] WRITE MODER  size=%u val=0x%08X\n", size, s->MODER);
            break;

        case GPIO_PUPDR:
            s->PUPDR = write_u32_sized(s->PUPDR, v32, size, off & 3u);
            gpioc_log(s, 1, "[GPIOC] WRITE PUPDR  size=%u val=0x%08X\n", size, s->PUPDR);
            break;

        case GPIO_OTYPER:
            s->OTYPER = write_u32_sized(s->OTYPER, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE OTYPER size=%u val=0x%08X\n", size, s->OTYPER);
            break;

        case GPIO_OSPEEDR:
            s->OSPEEDR = write_u32_sized(s->OSPEEDR, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE OSPEEDR size=%u val=0x%08X\n", size, s->OSPEEDR);
            break;

        case GPIO_ODR:
            s->ODR = write_u32_sized(s->ODR, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE ODR    size=%u val=0x%08X\n", size, s->ODR);
            break;

        case GPIO_BSRR: {
            // Minimal semantics: lower 16 bits set, upper 16 bits reset
            // (commonly used for pin toggling/setting).
            uint32_t bsrr = (size == 4) ? v32 : write_u32_sized(0, v32, size, off & 3u);
            uint32_t set_mask   = (bsrr & 0x0000FFFFu);
            uint32_t reset_mask = (bsrr >> 16) & 0x0000FFFFu;

            s->ODR |= set_mask;
            s->ODR &= ~reset_mask;

            gpioc_log(s, 2,
                      "[GPIOC] WRITE BSRR   size=%u bsrr=0x%08X set=0x%04X rst=0x%04X -> ODR=0x%08X\n",
                      size, bsrr, (unsigned)set_mask, (unsigned)reset_mask, s->ODR);
            break;
        }

        case GPIO_LCKR:
            s->LCKR = write_u32_sized(s->LCKR, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE LCKR   size=%u val=0x%08X\n", size, s->LCKR);
            break;

        case GPIO_AFRL:
            s->AFRL = write_u32_sized(s->AFRL, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE AFRL   size=%u val=0x%08X\n", size, s->AFRL);
            break;

        case GPIO_AFRH:
            s->AFRH = write_u32_sized(s->AFRH, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE AFRH   size=%u val=0x%08X\n", size, s->AFRH);
            break;

        case GPIO_IDR:
            // Allow test harness to "inject" inputs by writing IDR (not typical in HW).
            // This is a pragmatic option; output pins will still be refreshed from ODR.
            s->IDR = write_u32_sized(s->IDR, v32, size, off & 3u);
            gpioc_log(s, 2, "[GPIOC] WRITE IDR(inject) size=%u val=0x%08X\n", size, s->IDR);
            break;

        default:
            gpioc_log(s, 1, "[GPIOC] WRITE off=0x%03X size=%u val=0x%08X (ignored)\n",
                      off, size, v32);
            break;
    }

    // keep IDR consistent after any output change
    gpioc_refresh_idr(s);
}

void gpioc_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_gpioc, 0, sizeof(g_gpioc));

    // Default logging level (can be changed by editing here if desired).
    // 0 = quiet, 1 = basic (MODER/PUPDR writes), 2 = verbose (all).
    g_gpioc.log_level = 1;

    // The trace shows initial reads of MODER/PUPDR returning 0, so defaults remain 0.
    dev_debug("[GPIOC] init: state cleared (MODER=0, PUPDR=0)\n");
}
