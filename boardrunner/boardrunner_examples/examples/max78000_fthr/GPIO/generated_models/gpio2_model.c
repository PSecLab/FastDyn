// Device Model for GPIO2
//
// Inferred Register Functions:
//   0x18 OUT        - GPIO output latch (inferred from OUT_SET/OUT_CLR)
//   0x1C OUT_SET    - Atomic set bits in OUT
//   0x20 OUT_CLR    - Atomic clear bits in OUT
//   0x30 INEN       - Input enable configuration (sticky RW, inferred)
//   0x68 PADCTRL0   - Pad control register 0 (sticky RW, inferred)
//   0x6C PADCTRL1   - Pad control register 1 (sticky RW, inferred)
//   0x98 DS0        - Drive strength register 0 (sticky RW, inferred)
//   0xA4 DS1        - Drive strength register 1 (sticky RW, inferred)
//   0xC0 VSSEL      - Voltage select register (sticky RW, reset observed as 0xC0)
//
// Notes:
// - The framework passes absolute physical addresses; we subtract GPIO2_BASE.
// - This model is intentionally minimal and stateful.
// - GPIO output changes are exposed in two ways:
//   1) api_signal_set(GPIO2_SIGNAL_BASE + pin, level)
//   2) FIFO text notifications to /tmp/max78000_gpio2

#include <device.h>
#include <boardrunner/vio.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define GPIO2_BASE              0x40080400ULL

#define GPIO2_REG_OUT           0x18
#define GPIO2_REG_OUT_SET       0x1C
#define GPIO2_REG_OUT_CLR       0x20
#define GPIO2_REG_INEN          0x30
#define GPIO2_REG_PADCTRL0      0x68
#define GPIO2_REG_PADCTRL1      0x6C
#define GPIO2_REG_DS0           0x98
#define GPIO2_REG_DS1           0xA4
#define GPIO2_REG_VSSEL         0xC0

#define GPIO2_RESET_INEN        0x000000FFu
#define GPIO2_RESET_PADCTRL0    0x000000C0u
#define GPIO2_RESET_VSSEL       0x000000C0u

#define GPIO2_FIFO_PATH         "/tmp/max78000_gpio2"
#define GPIO2_SIGNAL_BASE       64   // Port-based mapping: GPIO2 pin N => signal 64 + N

typedef struct {
    uint32_t out;
    uint32_t inen;
    uint32_t padctrl0;
    uint32_t padctrl1;
    uint32_t ds0;
    uint32_t ds1;
    uint32_t vssel;

    int fifo_fd;
} GPIO2State;

static GPIO2State g_gpio2;

static void gpio2_debugf(const char *fmt, unsigned long long a, unsigned long long b)
{
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, a, b);
    dev_debug(buf);
}

static uint32_t gpio2_size_mask(unsigned size)
{
    switch (size) {
    case 1: return 0x000000FFu;
    case 2: return 0x0000FFFFu;
    case 4: return 0xFFFFFFFFu;
    default: return 0xFFFFFFFFu;
    }
}

static uint32_t gpio2_read_reg(GPIO2State *s, hwaddr reg_off)
{
    switch (reg_off) {
    case GPIO2_REG_OUT:
        return s->out;
    case GPIO2_REG_OUT_SET:
    case GPIO2_REG_OUT_CLR:
        // Common MMIO convention for write-only aliases: read as 0.
        return 0;
    case GPIO2_REG_INEN:
        /*
         * Observed hardware behavior on GPIO2: INEN always reads back as 0xFF.
         * Firmware performs RMW-style accesses, but the read value does not track
         * prior writes in the trace, so model fixed readback rather than sticky RW.
         */
        return GPIO2_RESET_INEN;
    case GPIO2_REG_PADCTRL0:
        /*
         * Observed hardware behavior on GPIO2: PADCTRL0 reads back as 0xC0.
         * Keep readback fixed to match the trace instead of exposing a writable
         * shadow value.
         */
        return GPIO2_RESET_PADCTRL0;
    case GPIO2_REG_PADCTRL1:
        return s->padctrl1;
    case GPIO2_REG_DS0:
        return s->ds0;
    case GPIO2_REG_DS1:
        return s->ds1;
    case GPIO2_REG_VSSEL:
        return s->vssel;
    default:
        gpio2_debugf("GPIO2: unhandled read reg_off=0x%llx size/val=0x%llx",
                     (unsigned long long)reg_off, 0ULL);
        return 0;
    }
}

static void gpio2_fifo_report(GPIO2State *s, unsigned pin, unsigned level)
{
    if (s->fifo_fd < 0) {
        return;
    }

    char line[32];
    int len = snprintf(line, sizeof(line), "pin%u=%u\n", pin, level);
    if (len > 0) {
        api_fifo_write(s->fifo_fd, line, len);
    }
}

static void gpio2_publish_output_changes(GPIO2State *s, uint32_t old_out, uint32_t new_out)
{
    uint32_t changed = old_out ^ new_out;
    if (!changed) {
        return;
    }

    for (unsigned pin = 0; pin < 32; pin++) {
        uint32_t bit = (1u << pin);
        if (changed & bit) {
            bool level = (new_out & bit) != 0;
            api_signal_set(GPIO2_SIGNAL_BASE + (int)pin, level);
            gpio2_fifo_report(s, pin, level ? 1u : 0u);
        }
    }
}

static void gpio2_write_reg(GPIO2State *s, hwaddr reg_off, uint32_t write_val, uint32_t write_mask)
{
    uint32_t old_out, new_out;

    switch (reg_off) {
    case GPIO2_REG_OUT:
        old_out = s->out;
        s->out = (s->out & ~write_mask) | (write_val & write_mask);
        new_out = s->out;
        gpio2_publish_output_changes(s, old_out, new_out);
        break;

    case GPIO2_REG_OUT_SET:
        old_out = s->out;
        s->out |= (write_val & write_mask);
        new_out = s->out;
        gpio2_publish_output_changes(s, old_out, new_out);
        break;

    case GPIO2_REG_OUT_CLR:
        old_out = s->out;
        s->out &= ~(write_val & write_mask);
        new_out = s->out;
        gpio2_publish_output_changes(s, old_out, new_out);
        break;

    case GPIO2_REG_INEN:
        /*
         * Writes are accepted but do not affect readback on observed hardware.
         * Preserve the traced constant behavior.
         */
        s->inen = GPIO2_RESET_INEN;
        break;

    case GPIO2_REG_PADCTRL0:
        /*
         * Writes do not change the observed readback value for GPIO2 PADCTRL0.
         */
        s->padctrl0 = GPIO2_RESET_PADCTRL0;
        break;

    case GPIO2_REG_PADCTRL1:
        s->padctrl1 = (s->padctrl1 & ~write_mask) | (write_val & write_mask);
        break;

    case GPIO2_REG_DS0:
        s->ds0 = (s->ds0 & ~write_mask) | (write_val & write_mask);
        break;

    case GPIO2_REG_DS1:
        s->ds1 = (s->ds1 & ~write_mask) | (write_val & write_mask);
        break;

    case GPIO2_REG_VSSEL:
        s->vssel = (s->vssel & ~write_mask) | (write_val & write_mask);
        break;

    default:
        gpio2_debugf("GPIO2: unhandled write reg_off=0x%llx val=0x%llx",
                     (unsigned long long)reg_off,
                     (unsigned long long)write_val);
        break;
    }
}

// This function will emulate all device reads
uint64_t gpio2_read(void *opaque, hwaddr addr, unsigned size)
{
    GPIO2State *s = (GPIO2State *)opaque;
    hwaddr offset = addr - GPIO2_BASE;
    hwaddr reg_off = offset & ~0x3ULL;
    unsigned shift = (unsigned)((offset & 0x3ULL) * 8ULL);

    uint32_t reg_val = gpio2_read_reg(s, reg_off);
    uint32_t mask = gpio2_size_mask(size);

    return (uint64_t)((reg_val >> shift) & mask);
}

// This function will emulate all device writes
void gpio2_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    GPIO2State *s = (GPIO2State *)opaque;
    hwaddr offset = addr - GPIO2_BASE;
    hwaddr reg_off = offset & ~0x3ULL;
    unsigned shift = (unsigned)((offset & 0x3ULL) * 8ULL);

    uint32_t size_mask = gpio2_size_mask(size);
    uint32_t write_mask = size_mask << shift;
    uint32_t write_val = ((uint32_t)value & size_mask) << shift;

    gpio2_write_reg(s, reg_off, write_val, write_mask);
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* gpio2_init(ConfigSection* model_info)
{
    (void)model_info;

    memset(&g_gpio2, 0, sizeof(g_gpio2));

    // Reset/default values inferred from trace evidence.
    g_gpio2.inen = GPIO2_RESET_INEN;
    g_gpio2.padctrl0 = GPIO2_RESET_PADCTRL0;
    g_gpio2.vssel = GPIO2_RESET_VSSEL;

    // Expose GPIO output transitions to the host.
    g_gpio2.fifo_fd = api_fifo_open(GPIO2_FIFO_PATH);
    if (g_gpio2.fifo_fd < 0) {
        dev_debug("GPIO2: failed to open FIFO /tmp/max78000_gpio2");
    }

    return &g_gpio2;
}