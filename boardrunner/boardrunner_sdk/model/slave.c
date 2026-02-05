// bme280_slave.c
//
// I2C slave model for BME280-like behavior (register pointer + sequential reads).
// Exports BOTH naming schemes:
//   - STM32F4_event / STM32F4_send / STM32F4_receive   (fixed symbols)
//   - bme280_event  / bme280_send  / bme280_receive    (Name=bme280)
//
// Key hardcoded bytes to match your trace expectations:
//   - regs[0xF3] (STATUS) = 0x00
//   - regs[0x88]          = 0x93
//
// Build (standalone):
//   gcc -shared -fPIC -O2 -o slave.so bme280_slave.c
//
// Or let your boardrunner_sdk CMake build it.
//
// Verify exports:
//   nm -D slave.so | grep -E "bme280_(send|receive|event)|STM32F4_(send|receive|event)"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <device.h>         // provides dev_debug(fmt, ...) macro in your tree
#include <boardrunner/vio.h>

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

    // On I2C_START_SEND, next byte is the register pointer
    bool    expect_reg_addr;
    uint8_t reg_ptr;

    bool    inited;
} bme280_state_t;

static bme280_state_t g_bme;

static void bme280_load_defaults(bme280_state_t *s) {
    memset(s->regs, 0, sizeof(s->regs));

    // Chip ID
    s->regs[REG_ID] = CHIP_ID_BME280;

    // Default config/control/status
    s->regs[REG_CTRL_HUM]  = 0x00;
    s->regs[REG_CTRL_MEAS] = 0x00;
    s->regs[REG_CONFIG]    = 0x00;

    // IMPORTANT: match trace expectation (STATUS read -> 0x00)
    s->regs[REG_STATUS] = 0x00;

    // Calibration blocks (stable, but we override 0x88 specifically)
    for (int i = 0; i < 26; i++) s->regs[0x88 + i] = (uint8_t)(0x20 + i);
    for (int i = 0; i < 7;  i++) s->regs[0xE1 + i] = (uint8_t)(0x80 + i);
    s->regs[0xA1] = 0x55;

    // IMPORTANT: match trace expectation (read at 0x88 -> 0x93)
    s->regs[0x88] = 0x93;

    // Provide 8 bytes of sensor data at 0xF7..0xFE: press(3), temp(3), hum(2)
    s->regs[0xF7] = 0x64; s->regs[0xF8] = 0x00; s->regs[0xF9] = 0x00;
    s->regs[0xFA] = 0x7A; s->regs[0xFB] = 0x00; s->regs[0xFC] = 0x00;
    s->regs[0xFD] = 0x40; s->regs[0xFE] = 0x00;

    s->expect_reg_addr = false;
    s->reg_ptr = 0x00;
}

static void bme280_lazy_init(void) {
    if (!g_bme.inited) {
        memset(&g_bme, 0, sizeof(g_bme));
        bme280_load_defaults(&g_bme);
        g_bme.inited = true;
        dev_debug("[bme280] slave initialized\n");
    }
}

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------
// REQUIRED FIXED SYMBOLS (DO NOT RENAME)
// -------------------------------

// Return 0 for ACK, 1 for NACK.
int STM32F4_send(uint8_t data) {
    bme280_lazy_init();

    if (g_bme.expect_reg_addr) {
        g_bme.reg_ptr = data;
        g_bme.expect_reg_addr = false;
        return 0; // ACK
    }

    // payload write into current reg
    uint8_t reg = g_bme.reg_ptr;
    g_bme.regs[reg] = data;

    // Soft reset behavior
    if (reg == REG_RESET && data == RESET_CMD) {
        bme280_load_defaults(&g_bme);
        return 0;
    }

    // auto-increment
    g_bme.reg_ptr = (uint8_t)(g_bme.reg_ptr + 1);
    return 0;
}

uint8_t STM32F4_receive(void) {
    bme280_lazy_init();

    uint8_t reg = g_bme.reg_ptr;
    uint8_t val = g_bme.regs[reg];

    // Keep STATUS deterministic for your trace matching
    if (reg == REG_STATUS) {
        val = 0x00;
    }

    g_bme.reg_ptr = (uint8_t)(g_bme.reg_ptr + 1);
    return val;
}

int STM32F4_event(enum i2c_event event) {
    bme280_lazy_init();

    switch (event) {
        case I2C_START_SEND:
            g_bme.expect_reg_addr = true;
            break;
        case I2C_START_RECV:
            g_bme.expect_reg_addr = false;
            break;
        case I2C_FINISH:
        case I2C_NACK:
        default:
            g_bme.expect_reg_addr = false;
            break;
    }
    return 0;
}

// -------------------------------
// Name-based aliases: Name=bme280
// -------------------------------
int bme280_send(uint8_t data) { return STM32F4_send(data); }
uint8_t bme280_receive(void)  { return STM32F4_receive(); }
int bme280_event(enum i2c_event event) { return STM32F4_event(event); }

#ifdef __cplusplus
} // extern "C"
#endif
