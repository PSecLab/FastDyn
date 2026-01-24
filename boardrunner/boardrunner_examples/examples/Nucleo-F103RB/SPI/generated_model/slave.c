// bme280_spi_slave.c
//
// SPI slave model for BME280-like behavior (command byte + sequential reads/writes).
//
// Exports BOTH naming schemes:
//   - STM32F4_set_cs / STM32F4_transfer      (fixed symbols)
//   - bme280_set_cs  / bme280_transfer       (Name=bme280)
//
// SPI behavior implemented:
//   - CS low  => new transaction, expect command byte
//   - First byte after CS low is "command/address"
//       * read  if (cmd & 0x80)
//       * write if !(cmd & 0x80)
//   - Subsequent bytes:
//       * read  => return regs[ptr], ptr++
//       * write => regs[ptr]=mosi, ptr++
//   - Soft reset: write RESET_CMD (0xB6) to REG_RESET (0xE0) => reload defaults
//
// Robust address mapping to handle common Bosch-style SPI convention:
//   - If master sends "reg|0x80" for reads and "reg&0x7F" for writes,
//     then addresses like 0xD0 read are fine, and writes like 0x74 map to 0xF4.
//   - Mapping rule used here:
//       read:  if cmd<0x80 => reg = (cmd & 0x7F) | 0x80; else reg = cmd
//       write: if cmd<0x80 => reg = (cmd & 0x7F) | 0x80; else reg = cmd
//
// Key hardcoded bytes to match your earlier expectations:
//   - regs[0xD0] (ID)     = 0x60
//   - regs[0xF3] (STATUS) = 0x00
//   - regs[0x88]          = 0x93
//
// Verify exports:
//   nm -D slave.so | grep -E "bme280_(set_cs|transfer)|STM32F4_(set_cs|transfer)"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <device.h> // dev_debug(...)

// ---- BME280 register addresses ----
#define REG_ID         0xD0
#define REG_RESET      0xE0
#define REG_CTRL_HUM   0xF2
#define REG_STATUS     0xF3
#define REG_CTRL_MEAS  0xF4
#define REG_CONFIG     0xF5
#define REG_DATA       0xF7

#define CHIP_ID_BME280 0x60
#define RESET_CMD      0xB6

typedef struct {
    uint8_t regs[256];

    bool inited;

    // SPI transaction state
    bool cs_active;     // CS low => active
    bool expect_cmd;    // first byte after CS low
    bool is_read;       // current command direction
    uint8_t reg_ptr;    // current register pointer
} bme280_spi_state_t;

static bme280_spi_state_t g_bme_spi;

static void bme280_load_defaults(bme280_spi_state_t *s) {
    memset(s->regs, 0, sizeof(s->regs));

    // Chip ID
    s->regs[REG_ID] = CHIP_ID_BME280;

    // Default config/control/status
    s->regs[REG_CTRL_HUM]  = 0x00;
    s->regs[REG_CTRL_MEAS] = 0x00;
    s->regs[REG_CONFIG]    = 0x00;

    // Deterministic status
    s->regs[REG_STATUS] = 0x00;

    // Calibration blocks (stable), but override 0x88 specifically
    for (int i = 0; i < 26; i++) s->regs[0x88 + i] = (uint8_t)(0x20 + i);
    for (int i = 0; i < 7;  i++) s->regs[0xE1 + i] = (uint8_t)(0x80 + i);
    s->regs[0xA1] = 0x55;

    // Match your expectation: read at 0x88 => 0x93
    s->regs[0x88] = 0x93;

    // Provide 8 bytes of sensor data at 0xF7..0xFE: press(3), temp(3), hum(2)
    s->regs[0xF7] = 0x64; s->regs[0xF8] = 0x00; s->regs[0xF9] = 0x00;
    s->regs[0xFA] = 0x7A; s->regs[0xFB] = 0x00; s->regs[0xFC] = 0x00;
    s->regs[0xFD] = 0x40; s->regs[0xFE] = 0x00;

    // Transaction defaults
    s->cs_active  = false;
    s->expect_cmd = true;
    s->is_read    = false;
    s->reg_ptr    = 0x00;
}

static void bme280_lazy_init(void) {
    if (!g_bme_spi.inited) {
        memset(&g_bme_spi, 0, sizeof(g_bme_spi));
        bme280_load_defaults(&g_bme_spi);
        g_bme_spi.inited = true;
        dev_debug("[bme280-spi] slave initialized\n");
    }
}

// Map command byte to a register address in 0x80..0xFF space (BME280 regs live there).
static inline uint8_t map_cmd_to_reg(uint8_t cmd) {
    if (cmd < 0x80) {
        return (uint8_t)((cmd & 0x7F) | 0x80);
    }
    return cmd;
}

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------
// REQUIRED FIXED SYMBOLS (DO NOT RENAME)
// -------------------------------

// level: 0=active (CS low), 1=inactive (CS high)
void STM32F4_set_cs(int level) {
    bme280_lazy_init();

    if (level == 0) {
        // CS asserted => new transaction
        g_bme_spi.cs_active  = true;
        g_bme_spi.expect_cmd = true;
        // keep reg_ptr as-is across transactions? real parts often keep it;
        // but resetting to "expect_cmd" is enough for your simple firmware.
    } else {
        // CS deasserted => end transaction
        g_bme_spi.cs_active  = false;
        g_bme_spi.expect_cmd = true;
    }
}

// Full-duplex transfer. We use only the low 8 bits as one SPI byte.
uint32_t STM32F4_transfer(uint32_t val) {
    bme280_lazy_init();

    // If not selected, behave like "nothing on the bus".
    if (!g_bme_spi.cs_active) {
        return 0x00000000u;
    }

    uint8_t mosi = (uint8_t)(val & 0xFFu);
    uint8_t miso = 0x00;

    if (g_bme_spi.expect_cmd) {
        // Interpret first byte as command/address
        g_bme_spi.is_read = ((mosi & 0x80u) != 0);

        // Robust mapping to BME280 register space
        g_bme_spi.reg_ptr = map_cmd_to_reg(mosi);

        g_bme_spi.expect_cmd = false;

        // Commonly undefined; firmware ignores rxData[0] anyway.
        miso = 0x00;
    } else {
        if (g_bme_spi.is_read) {
            uint8_t reg = g_bme_spi.reg_ptr;
            uint8_t out = g_bme_spi.regs[reg];

            // Keep STATUS deterministic
            if (reg == REG_STATUS) out = 0x00;

            miso = out;
            g_bme_spi.reg_ptr = (uint8_t)(g_bme_spi.reg_ptr + 1);
        } else {
            // Write payload into current reg
            uint8_t reg = g_bme_spi.reg_ptr;
            g_bme_spi.regs[reg] = mosi;

            // Soft reset behavior
            if (reg == REG_RESET && mosi == RESET_CMD) {
                bme280_load_defaults(&g_bme_spi);
            } else {
                g_bme_spi.reg_ptr = (uint8_t)(g_bme_spi.reg_ptr + 1);
            }

            // During writes, devices typically output don't-care; return 0.
            miso = 0x00;
        }
    }

    return (uint32_t)miso;
}

// -------------------------------
// Name-based aliases: Name=bme280
// -------------------------------
void bme280_set_cs(int level) { STM32F4_set_cs(level); }
uint32_t bme280_transfer(uint32_t v) { return STM32F4_transfer(v); }

#ifdef __cplusplus
} // extern "C"
#endif
