// Device Model for SPI4
#include <device.h>
#include <boardrunner/vio.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Base address and register offsets for SPI4
#define SPI4_BASE_ADDR   0x40013400
#define SPI4_CR1_OFS     0x00
#define SPI4_CR2_OFS     0x04
#define SPI4_SR_OFS      0x08
#define SPI4_DR_OFS      0x0C
#define SPI4_I2SCFGR_OFS 0x1C

// Control Register 1 (CR1) bits
#define CR1_SPE          (1u << 6) // SPI Enable

// Status Register (SR) bits
#define SR_RXNE          (1u << 0) // Receive buffer not empty
#define SR_TXE           (1u << 1) // Transmit buffer empty

// Device state structure
typedef struct {
    // Memory-mapped registers
    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint32_t dr; // Shadow data register
    uint32_t i2scfgr;

    // SPI bus connection and state
    SPIBus bus;
    bool bus_initialized;
    int active_cs_id; // Assumes a default CS line, e.g., 0

    // Debugging helper
    char debug_str[128];
} Spi4State;

// Use a single static instance for the device model's state
static Spi4State spi4_state;

/**
 * Inferred Register Functions:
 * - CR1: Used for configuration and enabling the peripheral (SPE bit).
 * - SR: Polled for TXE (transmit ready) and RXNE (receive ready) flags.
 * - DR: Writing initiates a transfer; reading retrieves data and clears RXNE.
 * - CR2/I2SCFGR: Acknowledged but have no side effects in this model.
*/

uint64_t spi4_read(void *opaque, hwaddr addr, unsigned size) {
    Spi4State *s = &spi4_state;
    uint32_t offset = addr - SPI4_BASE_ADDR;
    uint64_t value = 0;

    switch (offset) {
        case SPI4_CR1_OFS:
            value = s->cr1;
            break;
        case SPI4_CR2_OFS:
            value = s->cr2;
            break;
        case SPI4_SR_OFS:
            value = s->sr;
            break;
        case SPI4_DR_OFS:
            value = s->dr;
            // Per hardware behavior, reading DR clears the RXNE flag.
            s->sr &= ~SR_RXNE;
            break;
        case SPI4_I2SCFGR_OFS:
            value = s->i2scfgr;
            break;
        default:
            snprintf(s->debug_str, sizeof(s->debug_str), "SPI4: Read from unhandled offset 0x%x", offset);
            dev_debug(s->debug_str);
            break;
    }
    return value;
}

void spi4_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    Spi4State *s = &spi4_state;
    uint32_t offset = addr - SPI4_BASE_ADDR;

    switch (offset) {
        case SPI4_CR1_OFS:
            s->cr1 = value;
            break;
        case SPI4_CR2_OFS:
            s->cr2 = value;
            break;
        case SPI4_DR_OFS:
            // Only perform transfer if the peripheral is enabled (SPE=1)
            if ((s->cr1 & CR1_SPE) && s->bus_initialized) {
                // CORRECTION: Properly manage the Chip Select line around the transfer.

                // 1. Assert CS to select the slave (active low)
                api_spi_set_cs(&s->bus, s->active_cs_id, 0);

                // 2. Perform the full-duplex transfer
                uint32_t received_data = api_spi_transfer(&s->bus, (uint32_t)value);
                s->dr = received_data;

                // 3. De-assert CS to release the slave
                api_spi_set_cs(&s->bus, s->active_cs_id, 1);

                // 4. Set RXNE flag to indicate new data has been received.
                s->sr |= SR_RXNE;
            }
            break;
        case SPI4_I2SCFGR_OFS:
            s->i2scfgr = value;
            break;
        default:
            snprintf(s->debug_str, sizeof(s->debug_str), "SPI4: Write to unhandled offset 0x%x", offset);
            dev_debug(s->debug_str);
            break;
    }
}

void spi4_init(ConfigSection* model_info) {
    Spi4State *s = &spi4_state;
    memset(s, 0, sizeof(Spi4State));

    // By default, the transmit buffer is empty.
    s->sr = SR_TXE;
    s->active_cs_id = 0; // Assume we are controlling CS line 0

    // Initialize the SPI bus to connect with slave device models.
    s->bus = api_spi_init_bus(model_info);
    if (s->bus.Slaves.num_slaves > 0) {
        s->bus_initialized = true;
        dev_debug("SPI4: Model initialized and connected to bus.");
    } else {
        s->bus_initialized = false;
        dev_debug("SPI4: Model initialized without slave devices.");
    }
}