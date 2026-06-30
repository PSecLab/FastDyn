// SPI Slave implementation: icm20602_spi.c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>
#include <boardrunner/spi.h>

#define ICM20602_REG_SMPLRT_DIV    0x19
#define ICM20602_REG_CONFIG        0x1A
#define ICM20602_REG_GYRO_CONFIG   0x1B
#define ICM20602_REG_ACCEL_CONFIG  0x1C
#define ICM20602_REG_ACCEL_CONFIG2 0x1D
#define ICM20602_REG_FIFO_EN       0x23
#define ICM20602_REG_INT_PIN_CFG   0x37
#define ICM20602_REG_INT_ENABLE    0x38
#define ICM20602_REG_INT_STATUS    0x3A
#define ICM20602_REG_ACCEL_XOUT_H  0x3B
#define ICM20602_REG_TEMP_OUT_H    0x41
#define ICM20602_REG_GYRO_XOUT_H   0x43
#define ICM20602_REG_SIGNAL_PATH_RESET 0x68
#define ICM20602_REG_USER_CTRL     0x6A
#define ICM20602_REG_PWR_MGMT_1    0x6B
#define ICM20602_REG_PWR_MGMT_2    0x6C
#define ICM20602_REG_FIFO_COUNTH   0x72
#define ICM20602_REG_FIFO_COUNTL   0x73
#define ICM20602_REG_FIFO_R_W      0x74
#define ICM20602_REG_WHOAMI        0x75

#define ICM20602_WHOAMI_VALUE      0x12
#define ICM20602_SPI_READ_FLAG     0x80

#define ICM20602_PWR_RESET         0x80
#define ICM20602_USER_FIFO_RESET   0x04
#define ICM20602_USER_FIFO_EN      0x40

typedef struct {
    bool initialized;
    bool cs_active;
    bool expect_command;
    bool read_phase;
    uint8_t current_reg;
    uint8_t regs[128];
    uint8_t fifo_sample[14];
    unsigned fifo_pos;
    uint32_t sample_counter;
} ICM20602State;

static ICM20602State g_icm20602;

static void icm20602_store_be16(uint8_t *dst, int16_t v) {
    dst[0] = (uint8_t)(((uint16_t)v >> 8) & 0xFFU);
    dst[1] = (uint8_t)((uint16_t)v & 0xFFU);
}

static void icm20602_update_sample(ICM20602State *s) {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t temp;
    int16_t gx;
    int16_t gy;
    int16_t gz;

    s->sample_counter++;

    ax = 0;
    ay = 0;
    az = 16384;
    temp = 300;
    gx = 0;
    gy = 0;
    gz = 0;

    icm20602_store_be16(&s->regs[ICM20602_REG_ACCEL_XOUT_H + 0], ax);
    icm20602_store_be16(&s->regs[ICM20602_REG_ACCEL_XOUT_H + 2], ay);
    icm20602_store_be16(&s->regs[ICM20602_REG_ACCEL_XOUT_H + 4], az);
    icm20602_store_be16(&s->regs[ICM20602_REG_TEMP_OUT_H], temp);
    icm20602_store_be16(&s->regs[ICM20602_REG_GYRO_XOUT_H + 0], gx);
    icm20602_store_be16(&s->regs[ICM20602_REG_GYRO_XOUT_H + 2], gy);
    icm20602_store_be16(&s->regs[ICM20602_REG_GYRO_XOUT_H + 4], gz);

    memcpy(s->fifo_sample, &s->regs[ICM20602_REG_ACCEL_XOUT_H], sizeof(s->fifo_sample));
}

static void icm20602_reset_registers(ICM20602State *s) {
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ICM20602_REG_WHOAMI] = ICM20602_WHOAMI_VALUE;
    s->regs[ICM20602_REG_PWR_MGMT_1] = 0x40U;
    s->regs[ICM20602_REG_INT_STATUS] = 0x01U;
    s->fifo_pos = 0U;
    icm20602_update_sample(s);
}

static void icm20602_init_state(ICM20602State *s) {
    memset(s, 0, sizeof(*s));
    s->initialized = true;
    s->expect_command = true;
    icm20602_reset_registers(s);
}

static bool icm20602_fifo_enabled(ICM20602State *s) {
    return (s->regs[ICM20602_REG_FIFO_EN] != 0U) ||
           ((s->regs[ICM20602_REG_USER_CTRL] & ICM20602_USER_FIFO_EN) != 0U);
}

static bool icm20602_reg_autoincrement(uint8_t reg) {
    uint8_t addr = reg & 0x7FU;

    if (addr == ICM20602_REG_FIFO_R_W) {
        return false;
    }

    return true;
}

static bool icm20602_reg_refreshes_sample(uint8_t reg) {
    uint8_t addr = reg & 0x7FU;

    switch (addr) {
    case ICM20602_REG_ACCEL_XOUT_H:
    case ICM20602_REG_TEMP_OUT_H:
    case ICM20602_REG_GYRO_XOUT_H:
        return true;
    default:
        return false;
    }
}

static uint8_t icm20602_read_reg(ICM20602State *s, uint8_t reg) {
    uint8_t addr = reg & 0x7FU;

    switch (addr) {
    case ICM20602_REG_INT_STATUS:
        icm20602_update_sample(s);
        return 0x01U;

    case ICM20602_REG_FIFO_COUNTH:
        if (icm20602_fifo_enabled(s)) {
            icm20602_update_sample(s);
            s->fifo_pos = 0U;
            return 0x00U;
        }
        return 0x00U;

    case ICM20602_REG_FIFO_COUNTL:
        return icm20602_fifo_enabled(s) ? (uint8_t)sizeof(s->fifo_sample) : 0x00U;

    case ICM20602_REG_FIFO_R_W:
        if (!icm20602_fifo_enabled(s)) {
            return 0x00U;
        }
        if (s->fifo_pos >= sizeof(s->fifo_sample)) {
            s->fifo_pos = 0U;
            icm20602_update_sample(s);
        }
        return s->fifo_sample[s->fifo_pos++];

    default:
        if (icm20602_reg_refreshes_sample(addr)) {
            icm20602_update_sample(s);
        }
        return s->regs[addr];
    }
}

static void icm20602_write_reg(ICM20602State *s, uint8_t reg, uint8_t value) {
    uint8_t addr = reg & 0x7FU;

    if (addr == ICM20602_REG_PWR_MGMT_1 && (value & ICM20602_PWR_RESET) != 0U) {
        bool was_initialized = s->initialized;
        bool cs_active = s->cs_active;

        icm20602_init_state(s);
        s->initialized = was_initialized;
        s->cs_active = cs_active;
        s->expect_command = false;
        return;
    }

    s->regs[addr] = value;

    if (addr == ICM20602_REG_USER_CTRL &&
        (value & ICM20602_USER_FIFO_RESET) != 0U) {
        s->fifo_pos = 0U;
        icm20602_update_sample(s);
    }
}

uint32_t slave_spi_transfer(uint32_t value) {
    ICM20602State *s = &g_icm20602;
    uint8_t byte = (uint8_t)(value & 0xFFU);

    if (!s->initialized) {
        icm20602_init_state(s);
    }
    if (!s->cs_active) {
        return 0xFFU;
    }

    if (s->expect_command) {
        s->read_phase = ((byte & ICM20602_SPI_READ_FLAG) != 0U);
        s->current_reg = (uint8_t)(byte & 0x7FU);
        s->expect_command = false;
        return 0xFFU;
    }

    if (s->read_phase) {
        uint8_t ret = icm20602_read_reg(s, s->current_reg);

        if (icm20602_reg_autoincrement(s->current_reg)) {
            s->current_reg = (uint8_t)((s->current_reg + 1U) & 0x7FU);
        }

        return ret;
    }

    icm20602_write_reg(s, s->current_reg, byte);
    if (icm20602_reg_autoincrement(s->current_reg)) {
        s->current_reg = (uint8_t)((s->current_reg + 1U) & 0x7FU);
    }
    return 0xFFU;
}

void slave_spi_set_cs(int level) {
    ICM20602State *s = &g_icm20602;

    if (!s->initialized) {
        icm20602_init_state(s);
    }

    s->cs_active = (level == 0);
    s->expect_command = true;
    s->read_phase = false;
    s->current_reg = 0U;
}
