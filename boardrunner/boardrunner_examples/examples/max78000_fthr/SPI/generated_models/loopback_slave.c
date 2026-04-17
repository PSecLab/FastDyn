// spi_loopback_slave.c
//
// Minimal stateful SPI slave model for the MAX78000 SPI loopback example.
// This model behaves like an external MOSI<->MISO wire loopback:
// while CS is active, each transfer returns the same value it received.
//
// Required exported callbacks:
//   - slave_spi_set_cs
//   - slave_spi_transfer
//
// Build:
//   gcc -shared -fPIC -O2 -o slave.so spi_loopback_slave.c

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <device.h>
#include <boardrunner/vio.h>

typedef struct {
    uint8_t regs[256];

    // Kept to follow the expected model structure. This device has no real
    // register-address phase, so we use this as a "first byte in frame" flag.
    bool    expect_reg_addr;
    uint8_t reg_ptr;

    bool    cs_active;
    bool    inited;

    uint32_t transfer_count;
    uint8_t  last_mosi;
    uint8_t  last_miso;
} spi_loopback_state_t;

static spi_loopback_state_t g_state;

// Load power-on defaults and clear transaction state.
static void load_defaults(spi_loopback_state_t *s)
{
    memset(s->regs, 0, sizeof(s->regs));

    s->expect_reg_addr = false;
    s->reg_ptr         = 0;

    s->cs_active       = false;
    s->transfer_count  = 0;
    s->last_mosi       = 0x00;
    s->last_miso       = 0x00;
}

// Lazy init guard.
static void lazy_init(void)
{
    if (!g_state.inited) {
        memset(&g_state, 0, sizeof(g_state));
        load_defaults(&g_state);
        g_state.inited = true;
        dev_debug("[spi-loopback] initialized\n");
    }
}

// REQUIRED callback: level 0 = CS active, level 1 = CS inactive.
void slave_spi_set_cs(int level)
{
    lazy_init();

    if (level == 0) {
        g_state.cs_active = true;
        g_state.expect_reg_addr = true;
        g_state.transfer_count = 0;
    } else {
        g_state.cs_active = false;
        g_state.expect_reg_addr = false;
    }
}

// REQUIRED callback: full-duplex SPI transfer.
// This loopback device immediately echoes the master's transmitted value
// back to the master while CS is active.
//
// The firmware uses 8-bit transfers, but returning the full 32-bit value
// keeps the model tolerant of wider future transfers.
uint32_t slave_spi_transfer(uint32_t val)
{
    lazy_init();

    if (!g_state.cs_active) {
        return 0x00000000u;
    }

    if (g_state.expect_reg_addr) {
        // No real command/register phase exists for this device; clear the
        // marker after the first transfer of the frame.
        g_state.expect_reg_addr = false;
    }

    g_state.last_mosi = (uint8_t)(val & 0xFFu);
    g_state.last_miso = g_state.last_mosi;

    // Keep a circular byte history to maintain explicit statefulness.
    g_state.regs[g_state.reg_ptr] = g_state.last_mosi;
    g_state.reg_ptr = (uint8_t)(g_state.reg_ptr + 1);
    g_state.transfer_count++;

    // Echo input to output: emulates MOSI wired directly to MISO.
    return val;
}

// Optional aliases
void spi_loopback_set_cs(int level)
{
    slave_spi_set_cs(level);
}

uint32_t spi_loopback_transfer(uint32_t val)
{
    return slave_spi_transfer(val);
}