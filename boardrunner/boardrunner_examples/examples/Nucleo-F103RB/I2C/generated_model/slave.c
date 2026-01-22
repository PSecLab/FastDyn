// bme280_slave_hardcoded.c
//
// Hardcoded I2C slave model for BME280-like behavior:
// - Register pointer on first byte of a write phase
// - Sequential reads auto-increment the pointer
//
// Exports the fixed symbols you require:
//   - STM32F4_event
//   - STM32F4_send
//   - STM32F4_receive
//
// Build:
//   gcc -shared -fPIC -O2 -o slave.so bme280_slave_hardcoded.c
//
// Notes:
// - STATUS (0xF3) is hardcoded to 0x00 to match your hardware trace.
// - First calibration byte at 0x88 is hardcoded to 0x93 to match your trace.
// - Everything else is deterministic and stable.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <boardrunner/vio.h>  // enum i2c_event + dev_debug()

// ---- BME280 registers ----
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

    // On I2C_START_SEND, first byte is treated as register pointer.
    bool    expect_reg_addr;
    uint8_t reg_ptr;

    bool    inited;
} bme280_state_t;

static bme280_state_t g_bme;

static void dbg(const char *msg) {
    dev_debug((char*)msg);
}

static void bme280_load_hardcoded_defaults(bme280_state_t *s) {
    memset(s->regs, 0, sizeof(s->regs));

    // Chip ID
    s->regs[REG_ID] = CHIP_ID_BME280;

    // STATUS: match your hardware trace expectation (0x00)
    s->regs[REG_STATUS] = 0x00;

    // Control defaults (stable)
    s->regs[REG_CTRL_HUM]  = 0x00;
    s->regs[REG_CTRL_MEAS] = 0x00;
    s->regs[REG_CONFIG]    = 0x00;

    // Calibration region (0x88..0xA1 and 0xE1..0xE7) — deterministic.
    // The only byte you *proved* from trace is 0x88 == 0x93.
    // Fill the rest with stable values (doesn’t matter unless firmware checks exact bytes).
    for (int i = 0; i < 26; i++) s->regs[0x88 + i] = (uint8_t)(0x20 + i);
    s->regs[0x88] = 0x93;  // <-- trace-matching fix

    for (int i = 0; i < 7; i++) s->regs[0xE1 + i] = (uint8_t)(0x80 + i);

    // One extra calibration byte often used
    s->regs[0xA1] = 0x55;

    // Stable sensor data bytes at 0xF7..0xFE:
    // press(3), temp(3), hum(2). Deterministic and safe.
    s->regs[0xF7] = 0x64; s->regs[0xF8] = 0x00; s->regs[0xF9] = 0x00;
    s->regs[0xFA] = 0x7A; s->regs[0xFB] = 0x00; s->regs[0xFC] = 0x00;
    s->regs[0xFD] = 0x40; s->regs[0xFE] = 0x00;

    s->expect_reg_addr = false;
    s->reg_ptr = 0x00;
}

static void bme280_lazy_init(void) {
    if (!g_bme.inited) {
        memset(&g_bme, 0, sizeof(g_bme));
        bme280_load_hardcoded_defaults(&g_bme);
        g_bme.inited = true;
        dbg("[bme280] hardcoded slave initialized\n");
    }
}

#ifdef __cplusplus
extern "C" {
#endif

// Called when master sends a byte. Return 0=ACK, 1=NACK.
int STM32F4_send(uint8_t data) {
    bme280_lazy_init();

    if (g_bme.expect_reg_addr) {
        // First byte after START_SEND is the register pointer.
        g_bme.reg_ptr = data;
        g_bme.expect_reg_addr = false;
        return 0; // ACK
    }

    // Payload write to current register
    uint8_t reg = g_bme.reg_ptr;
    g_bme.regs[reg] = data;

    // Handle soft reset
    if (reg == REG_RESET && data == RESET_CMD) {
        // Keep calibration deterministic; reset control/data/status to defaults.
        uint8_t saved_cal_88_0 = g_bme.regs[0x88];
        uint8_t saved_cal_a1   = g_bme.regs[0xA1];
        uint8_t saved_cal_e1_0 = g_bme.regs[0xE1];

        bme280_load_hardcoded_defaults(&g_bme);

        // Re-apply any explicitly-trace-matched calibration bytes (redundant but clear)
        g_bme.regs[0x88] = saved_cal_88_0 ? saved_cal_88_0 : 0x93;
        g_bme.regs[0xA1] = saved_cal_a1   ? saved_cal_a1   : 0x55;
        g_bme.regs[0xE1] = saved_cal_e1_0 ? saved_cal_e1_0 : 0x80;

        return 0;
    }

    // Auto-increment for multi-byte writes
    g_bme.reg_ptr = (uint8_t)(g_bme.reg_ptr + 1);
    return 0; // ACK
}

// Called when master reads a byte from slave.
uint8_t STM32F4_receive(void) {
    bme280_lazy_init();

    uint8_t reg = g_bme.reg_ptr;
    uint8_t val = g_bme.regs[reg];

    // Hard guarantee: STATUS is stable 0x00 (matches your trace expectation)
    if (reg == REG_STATUS) {
        val = 0x00;
    }

    // Sequential read behavior
    g_bme.reg_ptr = (uint8_t)(g_bme.reg_ptr + 1);
    return val;
}

// Called on bus events.
int STM32F4_event(enum i2c_event event) {
    bme280_lazy_init();

    switch (event) {
        case I2C_START_SEND:
            // Next byte is register pointer
            g_bme.expect_reg_addr = true;
            break;

        case I2C_START_RECV:
            // Read phase: keep reg_ptr as set
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

#ifdef __cplusplus
} // extern "C"
#endif
