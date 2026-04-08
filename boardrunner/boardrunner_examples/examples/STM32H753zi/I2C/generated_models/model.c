// Device Model for I2C1

#include <device.h>
#include <boardrunner/vio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Inferred Register Functions:
// CR1     - control register, mainly PE observed
// CR2     - master transfer setup: SADD, RD_WRN, START, STOP, NBYTES, RELOAD, AUTOEND
// OAR1    - own address register 1
// OAR2    - own address register 2
// TIMINGR - timing configuration
// TIMEOUTR- timeout configuration
// ISR     - status flags (TXE, TXIS, RXNE, NACKF, STOPF, TC, TCR, BUSY, DIR)
// ICR     - interrupt/status flag clear register
// PECR    - PEC register (kept as zero/minimal)
// RXDR    - receive data register
// TXDR    - transmit data register

#define I2C1_BASE 0x40005400ULL

#define I2C1_CR1_OFFSET      0x00
#define I2C1_CR2_OFFSET      0x04
#define I2C1_OAR1_OFFSET     0x08
#define I2C1_OAR2_OFFSET     0x0C
#define I2C1_TIMINGR_OFFSET  0x10
#define I2C1_TIMEOUTR_OFFSET 0x14
#define I2C1_ISR_OFFSET      0x18
#define I2C1_ICR_OFFSET      0x1C
#define I2C1_PECR_OFFSET     0x20
#define I2C1_RXDR_OFFSET     0x24
#define I2C1_TXDR_OFFSET     0x28

// CR1 bits
#define I2C_CR1_PE           (1U << 0)

// CR2 bits
#define I2C_CR2_SADD_MASK    0x3FFU
#define I2C_CR2_RD_WRN       (1U << 10)
#define I2C_CR2_ADD10        (1U << 11)
#define I2C_CR2_START        (1U << 13)
#define I2C_CR2_STOP         (1U << 14)
#define I2C_CR2_NACK         (1U << 15)
#define I2C_CR2_NBYTES_MASK  (0xFFU << 16)
#define I2C_CR2_RELOAD       (1U << 24)
#define I2C_CR2_AUTOEND      (1U << 25)

// ISR bits
#define I2C_ISR_TXE          (1U << 0)
#define I2C_ISR_TXIS         (1U << 1)
#define I2C_ISR_RXNE         (1U << 2)
#define I2C_ISR_NACKF        (1U << 4)
#define I2C_ISR_STOPF        (1U << 5)
#define I2C_ISR_TC           (1U << 6)
#define I2C_ISR_TCR          (1U << 7)
#define I2C_ISR_BUSY         (1U << 15)
#define I2C_ISR_DIR          (1U << 16)

// ICR bits line up with the corresponding status flags for the flags we care about
#define I2C_ICR_NACKCF       (1U << 4)
#define I2C_ICR_STOPCF       (1U << 5)

typedef struct {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t oar1;
    uint32_t oar2;
    uint32_t timingr;
    uint32_t timeoutr;
    uint32_t isr;
    uint32_t pecr;
    uint8_t rxdr;
    uint8_t txdr;

    I2CBus bus;

    bool enabled;
    bool transfer_active;
    bool is_read;
    bool addr10;
    bool autoend;
    bool reload;

    uint16_t current_addr;
    uint8_t remaining;
} I2C1State;

static I2C1State g_i2c1;

static uint64_t i2c1_size_mask(uint64_t value, unsigned size) {
    switch (size) {
    case 1:
        return value & 0xFFU;
    case 2:
        return value & 0xFFFFU;
    default:
        return value & 0xFFFFFFFFU;
    }
}

static void i2c1_clear_data_phase_flags(I2C1State *s) {
    s->isr &= ~(I2C_ISR_TXIS | I2C_ISR_RXNE | I2C_ISR_TC |
                I2C_ISR_TCR | I2C_ISR_BUSY | I2C_ISR_DIR);
    s->isr |= I2C_ISR_TXE;
}

static void i2c1_finish_transfer(I2C1State *s, bool set_stopf) {
    if (s->transfer_active) {
        api_i2c_end_transfer(&s->bus);
    }

    s->transfer_active = false;
    s->remaining = 0;
    i2c1_clear_data_phase_flags(s);

    if (set_stopf) {
        s->isr |= I2C_ISR_STOPF;
    }
}

static int i2c1_bus_start(I2C1State *s, uint32_t cr2) {
    if (cr2 & I2C_CR2_ADD10) {
        uint16_t addr10 = (uint16_t)(cr2 & I2C_CR2_SADD_MASK);
        return api_i2c_start_transfer_10bit(&s->bus, addr10, s->is_read);
    } else {
        // STM32 master mode uses 7-bit address left-shifted in SADD[7:1].
        uint8_t addr7 = (uint8_t)((cr2 & I2C_CR2_SADD_MASK) >> 1);
        return api_i2c_start_transfer(&s->bus, addr7, s->is_read);
    }
}

static void i2c1_begin_or_reload_phase(I2C1State *s, uint32_t cr2, bool do_start) {
    int rc;

    s->is_read = (cr2 & I2C_CR2_RD_WRN) != 0;
    s->addr10 = (cr2 & I2C_CR2_ADD10) != 0;
    s->autoend = (cr2 & I2C_CR2_AUTOEND) != 0;
    s->reload = (cr2 & I2C_CR2_RELOAD) != 0;
    s->current_addr = (uint16_t)(cr2 & I2C_CR2_SADD_MASK);
    s->remaining = (uint8_t)((cr2 & I2C_CR2_NBYTES_MASK) >> 16);

    i2c1_clear_data_phase_flags(s);
    if (s->is_read) {
        s->isr |= I2C_ISR_DIR;
    }

    if (do_start) {
        rc = i2c1_bus_start(s, cr2);
        if (rc != 0) {
            // Address phase NACK / no device present.
            s->transfer_active = false;
            s->isr |= I2C_ISR_NACKF;

            if (s->autoend || s->remaining == 0) {
                s->isr |= I2C_ISR_STOPF;
            }

            // Ensure the bus API sees a terminated transfer if it created one.
            api_i2c_end_transfer(&s->bus);
            return;
        }

        s->transfer_active = true;
    } else {
        if (!s->transfer_active) {
            // A reload without an active transfer is meaningless for this model.
            return;
        }
    }

    if (s->is_read) {
        if (s->remaining == 0) {
            if (s->autoend) {
                i2c1_finish_transfer(s, true);
            } else if (s->reload) {
                s->isr |= I2C_ISR_TCR;
            } else {
                s->isr |= I2C_ISR_TC;
            }
            return;
        }

        s->isr |= I2C_ISR_BUSY | I2C_ISR_DIR;
        s->rxdr = api_i2c_recv(&s->bus);
        s->isr |= I2C_ISR_RXNE;
    } else {
        if (s->remaining == 0) {
            // This matches the observed device-ready probe:
            // START + AUTOEND + NBYTES=0 => immediate STOPF with TXE still set.
            if (s->autoend) {
                i2c1_finish_transfer(s, true);
            } else if (s->reload) {
                s->isr |= I2C_ISR_TCR;
            } else {
                s->isr |= I2C_ISR_TC;
            }
            return;
        }

        s->isr |= I2C_ISR_BUSY | I2C_ISR_TXIS;
    }
}

static uint32_t i2c1_reg_read_rxdr(I2C1State *s) {
    uint32_t val = s->rxdr;

    if (s->transfer_active && s->is_read && (s->isr & I2C_ISR_RXNE)) {
        if (s->remaining > 0) {
            s->remaining--;
        }

        s->isr &= ~I2C_ISR_RXNE;

        if (s->remaining > 0) {
            s->rxdr = api_i2c_recv(&s->bus);
            s->isr |= I2C_ISR_RXNE | I2C_ISR_BUSY | I2C_ISR_DIR;
        } else {
            if (s->autoend) {
                i2c1_finish_transfer(s, true);
            } else if (s->reload) {
                s->isr |= I2C_ISR_TCR | I2C_ISR_BUSY | I2C_ISR_DIR;
            } else {
                s->isr |= I2C_ISR_TC | I2C_ISR_BUSY | I2C_ISR_DIR;
            }
        }
    }

    return val;
}

static void i2c1_reg_write_txdr(I2C1State *s, uint8_t data) {
    int rc;

    s->txdr = data;

    if (!s->transfer_active || s->is_read) {
        return;
    }

    rc = api_i2c_send(&s->bus, data);
    s->isr &= ~I2C_ISR_TXIS;

    if (rc != 0) {
        s->isr |= I2C_ISR_NACKF;
        i2c1_finish_transfer(s, true);
        return;
    }

    if (s->remaining > 0) {
        s->remaining--;
    }

    if (s->remaining > 0) {
        s->isr |= I2C_ISR_TXIS | I2C_ISR_BUSY;
        return;
    }

    if (s->reload) {
        s->isr |= I2C_ISR_TCR | I2C_ISR_BUSY;
    } else if (s->autoend) {
        i2c1_finish_transfer(s, true);
    } else {
        s->isr |= I2C_ISR_TC | I2C_ISR_BUSY;
    }
}

// This function will emulate all device reads
uint64_t i2c1_read(void *opaque, hwaddr addr, unsigned size) {
    I2C1State *s = (I2C1State *)opaque;
    hwaddr offset = addr - I2C1_BASE;
    uint32_t val = 0;

    switch (offset) {
    case I2C1_CR1_OFFSET:
        val = s->cr1;
        break;
    case I2C1_CR2_OFFSET:
        val = s->cr2;
        break;
    case I2C1_OAR1_OFFSET:
        val = s->oar1;
        break;
    case I2C1_OAR2_OFFSET:
        val = s->oar2;
        break;
    case I2C1_TIMINGR_OFFSET:
        val = s->timingr;
        break;
    case I2C1_TIMEOUTR_OFFSET:
        val = s->timeoutr;
        break;
    case I2C1_ISR_OFFSET:
        val = s->isr;
        break;
    case I2C1_ICR_OFFSET:
        val = 0;
        break;
    case I2C1_PECR_OFFSET:
        val = s->pecr;
        break;
    case I2C1_RXDR_OFFSET:
        val = i2c1_reg_read_rxdr(s);
        break;
    case I2C1_TXDR_OFFSET:
        val = s->txdr;
        break;
    default:
        val = 0;
        break;
    }

    return i2c1_size_mask(val, size);
}

// This function will emulate all device writes
void i2c1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    I2C1State *s = (I2C1State *)opaque;
    hwaddr offset = addr - I2C1_BASE;
    uint32_t v = (uint32_t)i2c1_size_mask(value, size);

    switch (offset) {
    case I2C1_CR1_OFFSET:
        s->cr1 = v;
        s->enabled = (s->cr1 & I2C_CR1_PE) != 0;
        break;

    case I2C1_CR2_OFFSET: {
        bool do_start = (v & I2C_CR2_START) != 0;
        bool do_stop = (v & I2C_CR2_STOP) != 0;

        // START/STOP are command bits and read back cleared after acceptance.
        s->cr2 = v & ~(I2C_CR2_START | I2C_CR2_STOP);

        if (!s->enabled) {
            break;
        }

        if (do_start) {
            i2c1_begin_or_reload_phase(s, v, true);
        } else if ((s->isr & I2C_ISR_TCR) != 0) {
            // Support reloading NBYTES while the transfer is still active.
            s->isr &= ~I2C_ISR_TCR;
            i2c1_begin_or_reload_phase(s, v, false);
        }

        if (do_stop) {
            i2c1_finish_transfer(s, true);
        }
        break;
    }

    case I2C1_OAR1_OFFSET:
        s->oar1 = v;
        break;

    case I2C1_OAR2_OFFSET:
        s->oar2 = v;
        break;

    case I2C1_TIMINGR_OFFSET:
        s->timingr = v;
        break;

    case I2C1_TIMEOUTR_OFFSET:
        s->timeoutr = v;
        break;

    case I2C1_ICR_OFFSET:
        // Clear sticky status flags.
        if (v & I2C_ICR_NACKCF) {
            s->isr &= ~I2C_ISR_NACKF;
        }
        if (v & I2C_ICR_STOPCF) {
            s->isr &= ~I2C_ISR_STOPF;
        }
        break;

    case I2C1_TXDR_OFFSET:
        i2c1_reg_write_txdr(s, (uint8_t)(v & 0xFFU));
        break;

    case I2C1_RXDR_OFFSET:
    case I2C1_ISR_OFFSET:
    case I2C1_PECR_OFFSET:
    default:
        // Ignore writes to read-only or unimplemented registers.
        break;
    }
}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* i2c1_init(ConfigSection* model_info) {
    memset(&g_i2c1, 0, sizeof(g_i2c1));

    g_i2c1.bus = api_i2c_init_bus(model_info);

    // Observed idle status in trace is TXE=1.
    g_i2c1.isr = I2C_ISR_TXE;

    return &g_i2c1;
}
