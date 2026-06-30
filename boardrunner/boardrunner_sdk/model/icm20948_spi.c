// SPI Slave implementation: icm20948_spi.c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>
#include <boardrunner/spi.h>

#define ICM20948_SPI_READ_FLAG      0x80U

#define ICM20948_REG_BANK_SEL       0x7FU

#define ICM20948_BANK0_WHOAMI       0x00U
#define ICM20948_BANK0_USER_CTRL    0x03U
#define ICM20948_BANK0_PWR_MGMT_1   0x06U
#define ICM20948_BANK0_PWR_MGMT_2   0x07U
#define ICM20948_BANK0_INT_STATUS   0x19U
#define ICM20948_BANK0_ACCEL_XOUT_H 0x2DU
#define ICM20948_BANK0_GYRO_XOUT_H  0x33U
#define ICM20948_BANK0_TEMP_OUT_H   0x39U
#define ICM20948_BANK0_FIFO_EN_2    0x67U
#define ICM20948_BANK0_FIFO_RST     0x68U
#define ICM20948_BANK0_FIFO_MODE    0x69U
#define ICM20948_BANK0_FIFO_COUNTH  0x70U
#define ICM20948_BANK0_FIFO_COUNTL  0x71U
#define ICM20948_BANK0_FIFO_R_W     0x72U

#define ICM20948_WHOAMI_VALUE       0xEAU

#define ICM20948_PWR_RESET          0x80U
#define ICM20948_USER_FIFO_EN       0x40U
#define ICM20948_USER_FIFO_RESET    0x04U

#define ICM20948_NUM_BANKS          4
#define ICM20948_FIFO_SAMPLE_SIZE   14

typedef struct {
    bool initialized;
    bool cs_active;
    bool expect_command;
    bool read_phase;
    uint8_t current_reg;
    uint8_t current_bank;
    uint8_t regs[ICM20948_NUM_BANKS][128];
    uint8_t fifo_sample[ICM20948_FIFO_SAMPLE_SIZE];
    unsigned fifo_pos;
    uint32_t sample_counter;
} ICM20948State;

static ICM20948State g_icm20948;

static void icm20948_store_be16(uint8_t *dst, int16_t v) {
    dst[0] = (uint8_t)(((uint16_t)v >> 8) & 0xFFU);
    dst[1] = (uint8_t)((uint16_t)v & 0xFFU);
}

static void icm20948_update_sample(ICM20948State *s) {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t temp;
    int16_t gx;
    int16_t gy;
    int16_t gz;

    s->sample_counter++;

    ax = 64;
    ay = 0;
    az = 16384;
    temp = (int16_t)(320 + (s->sample_counter & 0x1FU));
    gx = (int16_t)(12 + (s->sample_counter & 0x0FU));
    gy = 0;
    gz = 0;

    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_ACCEL_XOUT_H + 0], ax);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_ACCEL_XOUT_H + 2], ay);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_ACCEL_XOUT_H + 4], az);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_TEMP_OUT_H], temp);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_GYRO_XOUT_H + 0], gx);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_GYRO_XOUT_H + 2], gy);
    icm20948_store_be16(&s->regs[0][ICM20948_BANK0_GYRO_XOUT_H + 4], gz);

    memcpy(s->fifo_sample,
           &s->regs[0][ICM20948_BANK0_ACCEL_XOUT_H],
           sizeof(s->fifo_sample));
}

static void icm20948_reset_registers(ICM20948State *s) {
    memset(s->regs, 0, sizeof(s->regs));
    s->current_bank = 0U;
    s->regs[0][ICM20948_BANK0_WHOAMI] = ICM20948_WHOAMI_VALUE;
    s->regs[0][ICM20948_REG_BANK_SEL] = 0x00U;
    s->regs[0][ICM20948_BANK0_PWR_MGMT_1] = 0x40U;
    s->regs[0][ICM20948_BANK0_INT_STATUS] = 0x01U;
    s->fifo_pos = 0U;
    icm20948_update_sample(s);
}

static void icm20948_init_state(ICM20948State *s) {
    memset(s, 0, sizeof(*s));
    s->initialized = true;
    s->expect_command = true;
    icm20948_reset_registers(s);
}

static bool icm20948_fifo_enabled(ICM20948State *s) {
    return (s->regs[0][ICM20948_BANK0_FIFO_EN_2] != 0U) ||
           ((s->regs[0][ICM20948_BANK0_USER_CTRL] & ICM20948_USER_FIFO_EN) != 0U);
}

static uint8_t icm20948_read_reg(ICM20948State *s, uint8_t reg) {
    uint8_t addr = reg & 0x7FU;

    if (addr == ICM20948_REG_BANK_SEL) {
        return (uint8_t)(s->current_bank << 4);
    }

    if (s->current_bank == 0U) {
        switch (addr) {
        case ICM20948_BANK0_INT_STATUS:
            icm20948_update_sample(s);
            return 0x01U;

        case ICM20948_BANK0_FIFO_COUNTH:
            if (icm20948_fifo_enabled(s)) {
                icm20948_update_sample(s);
                s->fifo_pos = 0U;
            }
            return 0x00U;

        case ICM20948_BANK0_FIFO_COUNTL:
            return icm20948_fifo_enabled(s) ? (uint8_t)sizeof(s->fifo_sample) : 0x00U;

        case ICM20948_BANK0_FIFO_R_W:
            if (!icm20948_fifo_enabled(s)) {
                return 0x00U;
            }
            if (s->fifo_pos >= sizeof(s->fifo_sample)) {
                s->fifo_pos = 0U;
                icm20948_update_sample(s);
            }
            return s->fifo_sample[s->fifo_pos++];

        case ICM20948_BANK0_ACCEL_XOUT_H:
        case ICM20948_BANK0_ACCEL_XOUT_H + 1:
        case ICM20948_BANK0_ACCEL_XOUT_H + 2:
        case ICM20948_BANK0_ACCEL_XOUT_H + 3:
        case ICM20948_BANK0_ACCEL_XOUT_H + 4:
        case ICM20948_BANK0_ACCEL_XOUT_H + 5:
        case ICM20948_BANK0_TEMP_OUT_H:
        case ICM20948_BANK0_TEMP_OUT_H + 1:
        case ICM20948_BANK0_GYRO_XOUT_H:
        case ICM20948_BANK0_GYRO_XOUT_H + 1:
        case ICM20948_BANK0_GYRO_XOUT_H + 2:
        case ICM20948_BANK0_GYRO_XOUT_H + 3:
        case ICM20948_BANK0_GYRO_XOUT_H + 4:
        case ICM20948_BANK0_GYRO_XOUT_H + 5:
            icm20948_update_sample(s);
            return s->regs[0][addr];

        default:
            break;
        }
    }

    return s->regs[s->current_bank][addr];
}

static void icm20948_write_reg(ICM20948State *s, uint8_t reg, uint8_t value) {
    uint8_t addr = reg & 0x7FU;

    if (addr == ICM20948_REG_BANK_SEL) {
        s->current_bank = (uint8_t)((value >> 4) & 0x03U);
        s->regs[0][ICM20948_REG_BANK_SEL] = (uint8_t)(s->current_bank << 4);
        return;
    }

    if (s->current_bank == 0U &&
        addr == ICM20948_BANK0_PWR_MGMT_1 &&
        (value & ICM20948_PWR_RESET) != 0U) {
        bool was_initialized = s->initialized;
        bool cs_active = s->cs_active;

        icm20948_init_state(s);
        s->initialized = was_initialized;
        s->cs_active = cs_active;
        s->expect_command = false;
        return;
    }

    s->regs[s->current_bank][addr] = value;

    if (s->current_bank == 0U &&
        addr == ICM20948_BANK0_USER_CTRL &&
        (value & ICM20948_USER_FIFO_RESET) != 0U) {
        s->fifo_pos = 0U;
    }

    if (s->current_bank == 0U &&
        addr == ICM20948_BANK0_FIFO_RST) {
        s->fifo_pos = 0U;
    }
}

uint32_t slave_spi_transfer(uint32_t value) {
    ICM20948State *s = &g_icm20948;
    uint8_t byte = (uint8_t)(value & 0xFFU);

    if (!s->initialized) {
        icm20948_init_state(s);
    }
    if (!s->cs_active) {
        return 0xFFU;
    }

    if (s->expect_command) {
        s->read_phase = ((byte & ICM20948_SPI_READ_FLAG) != 0U);
        s->current_reg = (uint8_t)(byte & 0x7FU);
        s->expect_command = false;
        return 0xFFU;
    }

    if (s->read_phase) {
        uint8_t ret = icm20948_read_reg(s, s->current_reg);
        s->current_reg = (uint8_t)((s->current_reg + 1U) & 0x7FU);
        return ret;
    }

    icm20948_write_reg(s, s->current_reg, byte);
    s->current_reg = (uint8_t)((s->current_reg + 1U) & 0x7FU);
    return 0xFFU;
}

void slave_spi_set_cs(int level) {
    ICM20948State *s = &g_icm20948;

    if (!s->initialized) {
        icm20948_init_state(s);
    }

    s->cs_active = (level == 0);
    s->expect_command = true;
    s->read_phase = false;
    s->current_reg = 0U;
}
