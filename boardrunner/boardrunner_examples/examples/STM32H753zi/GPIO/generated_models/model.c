// Device Model for GPIOB
//
// Fix for the observed mismatch:
// - The firmware trace addresses are absolute (0x580204xx).
// - Many emulation frameworks pass either absolute addresses OR offsets into the MMIO callback.
// - If the model only matches offsets but receives absolute addresses, reads fall through -> 0,
//   and firmware RMW writes become tiny (e.g., 0x3 instead of 0xC3).
// This model normalizes both address forms to a base-relative offset before decoding.
//
// Observed registers:
//   MODER   (0x00)  R/W
//   OTYPER  (0x04)  R/W
//   OSPEEDR (0x08)  R/W
//   PUPDR   (0x0C)  R/W
//   ODR     (0x14)  R/W (read in loops)
//   BSRR    (0x18)  W   (set/reset affects ODR)

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define GPIOB_BASE_ADDR      0x58020400ULL
#define GPIOB_MMIO_SPAN      0x400ULL  // generous decode window

#define GPIOB_MODER_OFF      0x00
#define GPIOB_OTYPER_OFF     0x04
#define GPIOB_OSPEEDR_OFF    0x08
#define GPIOB_PUPDR_OFF      0x0C
#define GPIOB_ODR_OFF        0x14
#define GPIOB_BSRR_OFF       0x18

typedef struct {
    uint32_t MODER;
    uint32_t OTYPER;   // only low 16 bits meaningful
    uint32_t OSPEEDR;
    uint32_t PUPDR;
    uint32_t ODR;      // only low 16 bits meaningful
} gpiob_state_t;

static gpiob_state_t g_gpiob;

static inline hwaddr gpiob_norm_off(hwaddr addr)
{
    // Accept either absolute MMIO addresses or already-normalized offsets.
    if ((uint64_t)addr >= GPIOB_BASE_ADDR && (uint64_t)addr < (GPIOB_BASE_ADDR + GPIOB_MMIO_SPAN)) {
        return (hwaddr)((uint64_t)addr - GPIOB_BASE_ADDR);
    }
    return addr;
}

static inline uint32_t mask_for_size(unsigned size)
{
    if (size >= 4) return 0xFFFFFFFFu;
    return (uint32_t)((1ULL << (size * 8)) - 1ULL);
}

static inline uint32_t apply_partial_write32(uint32_t oldv, hwaddr rel_off, uint64_t value, unsigned size)
{
    if (size >= 4) {
        return (uint32_t)value;
    }
    uint32_t m = mask_for_size(size);
    unsigned shift = (unsigned)((rel_off & 0x3) * 8);
    uint32_t w = (uint32_t)(value & m);
    uint32_t mask = (m << shift);
    return (oldv & ~mask) | (w << shift);
}

static inline uint64_t read_sized_u32(uint32_t v, unsigned size, hwaddr rel_off)
{
    if (size >= 4) return (uint64_t)v;
    uint32_t m = mask_for_size(size);
    unsigned shift = (unsigned)((rel_off & 0x3) * 8);
    return (uint64_t)((v >> shift) & m);
}

static void dbg(const char *msg, hwaddr rel_off, uint64_t val, unsigned size)
{
    char b[200];
    snprintf(b, sizeof(b), "GPIOB %s off=0x%llx size=%u val=0x%llx\n",
             msg,
             (unsigned long long)rel_off,
             (unsigned)size,
             (unsigned long long)val);
    dev_debug(b);
}

// This function will emulate all device reads
uint64_t gpiob_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    hwaddr off = gpiob_norm_off(addr);

    switch ((uint32_t)off) {
        case GPIOB_MODER_OFF:
            return read_sized_u32(g_gpiob.MODER, size, off);

        case GPIOB_OTYPER_OFF:
            return read_sized_u32(g_gpiob.OTYPER, size, off);

        case GPIOB_OSPEEDR_OFF:
            return read_sized_u32(g_gpiob.OSPEEDR, size, off);

        case GPIOB_PUPDR_OFF:
            return read_sized_u32(g_gpiob.PUPDR, size, off);

        case GPIOB_ODR_OFF:
            return read_sized_u32(g_gpiob.ODR, size, off);

        case GPIOB_BSRR_OFF:
            // Not expected to be read; return 0.
            return 0;

        default:
            // Unknown offset based on provided traces.
            // dbg("READ unknown -> 0", off, 0, size);
            return 0;
    }
}

// This function will emulate all device writes
void gpiob_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    (void)opaque;
    hwaddr off = gpiob_norm_off(addr);

    switch ((uint32_t)off) {
        case GPIOB_MODER_OFF:
            g_gpiob.MODER = apply_partial_write32(g_gpiob.MODER, off, value, size);
            break;

        case GPIOB_OTYPER_OFF:
            g_gpiob.OTYPER = apply_partial_write32(g_gpiob.OTYPER, off, value, size) & 0x0000FFFFu;
            break;

        case GPIOB_OSPEEDR_OFF:
            g_gpiob.OSPEEDR = apply_partial_write32(g_gpiob.OSPEEDR, off, value, size);
            break;

        case GPIOB_PUPDR_OFF:
            g_gpiob.PUPDR = apply_partial_write32(g_gpiob.PUPDR, off, value, size);
            break;

        case GPIOB_ODR_OFF:
            // Not observed as write in the provided traces, but implement safely.
            g_gpiob.ODR = apply_partial_write32(g_gpiob.ODR, off, value, size) & 0x0000FFFFu;
            break;

        case GPIOB_BSRR_OFF: {
            // Support partial writes too by placing the written bytes at the proper position
            // within the 32-bit BSRR word.
            //
            // BSRR layout (conceptual 32-bit):
            //   [15:0]   SET bits
            //   [31:16]  RESET bits
            //
            // This also naturally supports halfword writes to BSRR+2 behaving like reset-only.
            uint32_t m = mask_for_size(size);
            unsigned shift = (unsigned)((off & 0x3) * 8);
            uint32_t bsrr_word = ((uint32_t)value & m) << shift;

            uint32_t set_mask   = (bsrr_word & 0x0000FFFFu);
            uint32_t reset_mask = (bsrr_word >> 16) & 0x0000FFFFu;

            uint32_t odr = g_gpiob.ODR & 0x0000FFFFu;
            odr |= set_mask;
            odr &= ~reset_mask;
            g_gpiob.ODR = odr;

            break;
        }

        default:
            // dbg("WRITE unknown (ignored)", off, value, size);
            break;
    }
}

void gpiob_init(ConfigSection* model_info)
{
    (void)model_info;
    memset(&g_gpiob, 0, sizeof(g_gpiob));

    // Seed register values to match the *hardware* initial reads in init.txt.
    // This is critical for correct RMW behavior and to avoid the observed mismatch.
    g_gpiob.OSPEEDR = 0x000000C0u;   // hardware read before write: 0xC0
    g_gpiob.OTYPER  = 0x00000000u;   // hardware read: 0x0
    g_gpiob.PUPDR   = 0x00000100u;   // hardware read: 0x100
    g_gpiob.MODER   = 0xFFFFFEBFu;   // hardware read: 0xFFFFFEBF
    g_gpiob.ODR     = 0x00000000u;   // observed reads: 0x0

    dev_debug("GPIOB init: seeded defaults + robust absolute/offset address decode.\n");
}