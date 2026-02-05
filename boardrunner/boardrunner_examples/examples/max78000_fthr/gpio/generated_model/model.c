// Device Model for GPIO2 (MAX78000)
// Backward-pass fix: VSSEL mismatch (HW: 0xC0->0xC1, Emu was: 0xC6->0xC7)
//
// Root cause implied by traces:
//   - Firmware does RMW: new = read | 0x1.
//   - If emu read returns 0xC6 instead of 0xC0, firmware will write 0xC7 instead of 0xC1.
// Fix implemented here:
//   - VSSEL reset/read behavior returns base 0xC0 plus only allowed low bits.
//   - Bits 1..2 are forced to 0 in reads (matches observed HW vs emu delta of 0x6).
//
// Notes:
//   - OUT_SET / OUT_CLR semantics implemented via an internal output latch.
//   - Other registers are not referenced by address in the provided data; they are not modeled beyond safe defaults.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <device.h>
#include <boardrunner/vio.h>

#ifndef hwaddr
// If your environment doesn't define hwaddr in device.h, uncomment:
// typedef uint64_t hwaddr;
#endif

#define GPIO2_BASE_ADDR      (0x40080400ull)
#define GPIO2_MMIO_SIZE      (0x400ull)   // conservative

// Observed offsets
#define GPIO2_OFF_OUT_SET    (0x01Cull)   // 0x4008041C
#define GPIO2_OFF_OUT_CLR    (0x020ull)   // 0x40080420
#define GPIO2_OFF_VSSEL      (0x0C0ull)   // 0x400804C0

// VSSEL behavior inferred from discrepancy:
//  - HW read observed: 0xC0
//  - Emu read observed: 0xC6 (extra bits 1..2)
// So clamp those bits to 0.
// Keep upper bits 7..6 = 1 (0xC0), and allow bit0 to exist (firmware sets it).
#define VSSEL_FIXED_HI       (0xC0u)
#define VSSEL_FORCE_ZERO     (0x06u)      // bits 1..2 forced to 0 on reads
#define VSSEL_WRITABLE_LO    (0x3Fu)      // conservative: only low 6 bits ever considered

typedef struct {
    uint32_t out_latch;
    uint32_t vssel_shadow_lo;   // only low bits (we clamp); high bits are fixed in read
} gpio2_state_t;

static gpio2_state_t g_gpio2;

static inline void dbg_hex(const char *prefix, unsigned long long addr, uint32_t v)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "%s addr=0x%llx v=0x%08x",
             prefix, addr, (unsigned)v);
    dev_debug(buf);
}

// Normalize addr to an offset. Works whether callbacks pass absolute addresses or offsets.
static inline uint32_t gpio2_off(hwaddr addr)
{
    if (addr >= (hwaddr)GPIO2_BASE_ADDR && addr < (hwaddr)(GPIO2_BASE_ADDR + GPIO2_MMIO_SIZE)) {
        return (uint32_t)(addr - (hwaddr)GPIO2_BASE_ADDR);
    }
    return (uint32_t)addr;
}

static inline uint32_t load_subword_le(uint32_t word, uint32_t byte_off, unsigned size)
{
    if (size == 1) return (word >> (8u * byte_off)) & 0xFFu;
    if (size == 2) return (word >> (8u * byte_off)) & 0xFFFFu;
    return word;
}

static inline uint32_t store_subword_le(uint32_t old_word, uint32_t byte_off, unsigned size, uint32_t val)
{
    if (size == 1) {
        uint32_t mask = 0xFFu << (8u * byte_off);
        return (old_word & ~mask) | ((val & 0xFFu) << (8u * byte_off));
    }
    if (size == 2) {
        uint32_t mask = 0xFFFFu << (8u * byte_off);
        return (old_word & ~mask) | ((val & 0xFFFFu) << (8u * byte_off));
    }
    return val;
}

static inline uint32_t gpio2_vssel_read(void)
{
    // Compose read value:
    //  - fixed upper bits = 0xC0
    //  - include low shadow bits but force bits1..2 to 0
    uint32_t lo = g_gpio2.vssel_shadow_lo & VSSEL_WRITABLE_LO;
    lo &= ~VSSEL_FORCE_ZERO;
    return VSSEL_FIXED_HI | lo;
}

static inline void gpio2_vssel_write(uint32_t v)
{
    // Store only low bits; high bits are fixed on read.
    // Also optionally clamp reserved bits by storing them cleared.
    uint32_t lo = v & VSSEL_WRITABLE_LO;
    lo &= ~VSSEL_FORCE_ZERO; // ensure we never "learn" the wrong 0x6 bits
    g_gpio2.vssel_shadow_lo = lo;

    dbg_hex("GPIO2 VSSEL write:", (unsigned long long)(GPIO2_BASE_ADDR + GPIO2_OFF_VSSEL), gpio2_vssel_read());
}

// This function will emulation all device reads
uint64_t gpio2_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;

    if (!(size == 1 || size == 2 || size == 4 || size == 8)) {
        dev_debug("GPIO2: unsupported read size");
        return 0;
    }

    // We only model 32-bit regs. For 8-byte reads, return duplicated 32-bit value.
    if (size == 8) {
        uint64_t lo = (uint64_t)gpio2_read(opaque, addr, 4);
        return lo | (lo << 32);
    }

    uint32_t off = gpio2_off(addr);
    uint32_t off_aligned = off & ~3u;
    uint32_t byte_off = off & 3u;

    // OUT_SET/OUT_CLR are write-only aliases. Return 0 on read.
    if (off_aligned == GPIO2_OFF_OUT_SET || off_aligned == GPIO2_OFF_OUT_CLR) {
        return 0;
    }

    if (off_aligned == GPIO2_OFF_VSSEL) {
        uint32_t v = gpio2_vssel_read();
        return (uint64_t)load_subword_le(v, byte_off, size);
    }

    // Unknown registers: safe default 0.
    return 0;
}

// This function will emulate all device writes
void gpio2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    (void)opaque;

    if (!(size == 1 || size == 2 || size == 4 || size == 8)) {
        dev_debug("GPIO2: unsupported write size");
        return;
    }

    if (size == 8) {
        gpio2_write(opaque, addr, (uint32_t)(value & 0xFFFFFFFFu), 4);
        gpio2_write(opaque, addr + 4, (uint32_t)((value >> 32) & 0xFFFFFFFFu), 4);
        return;
    }

    uint32_t off = gpio2_off(addr);
    uint32_t off_aligned = off & ~3u;
    uint32_t byte_off = off & 3u;

    // Treat subword writes as updating only the subword of the 32-bit target register.
    // For OUT_SET/OUT_CLR, hardware generally expects word writes; but we still accept subwords safely.
    uint32_t v32 = (uint32_t)value;
    if (size != 4) {
        // For aliases/config, merge into a 32-bit value as if the register existed.
        // This keeps behavior deterministic for odd firmware accesses.
        uint32_t cur = 0;
        if (off_aligned == GPIO2_OFF_VSSEL) cur = gpio2_vssel_read();
        // For OUT aliases, cur is irrelevant; store_subword_le still provides a stable v32.
        v32 = store_subword_le(cur, byte_off, size, v32);
    }

    if (off_aligned == GPIO2_OFF_OUT_SET) {
        uint32_t old = g_gpio2.out_latch;
        g_gpio2.out_latch |= v32;
        if (g_gpio2.out_latch != old) {
            dbg_hex("GPIO2 OUT_SET (latch):", (unsigned long long)(GPIO2_BASE_ADDR + GPIO2_OFF_OUT_SET), g_gpio2.out_latch);
        }
        return;
    }

    if (off_aligned == GPIO2_OFF_OUT_CLR) {
        uint32_t old = g_gpio2.out_latch;
        g_gpio2.out_latch &= ~v32;
        if (g_gpio2.out_latch != old) {
            dbg_hex("GPIO2 OUT_CLR (latch):", (unsigned long long)(GPIO2_BASE_ADDR + GPIO2_OFF_OUT_CLR), g_gpio2.out_latch);
        }
        return;
    }

    if (off_aligned == GPIO2_OFF_VSSEL) {
        // Key fix lives here: clamp away the erroneous +0x6 bits.
        gpio2_vssel_write(v32);
        return;
    }

    // Unknown writes: ignore safely.
}

// Minimal init for emulation
void gpio2_init(ConfigSection* model_info)
{
    (void)model_info;
    memset(&g_gpio2, 0, sizeof(g_gpio2));

    // Hardware observed first VSSEL read = 0xC0.
    // We implement this via fixed high bits and low shadow bits = 0.
    g_gpio2.vssel_shadow_lo = 0;

    dev_debug("GPIO2: init done (VSSEL readback starts at 0xC0; bits1..2 forced 0)");
}
