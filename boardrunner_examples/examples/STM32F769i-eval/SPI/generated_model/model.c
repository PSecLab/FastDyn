#include <stdint.h>
#include <string.h> // For memset
#include <stdio.h>  // For snprintf
#include <device.h>
#include <devmodels_apis.h>

// Base address for SPI1
#define SPI1_BASE 0x40013000

// Inferred Register Offsets:
#define SPI_CR1_OFFSET 0x00
#define SPI_CR2_OFFSET 0x04
#define SPI_SR_OFFSET  0x08
#define SPI_DR_OFFSET  0x0C
#define SPI_I2SCFGR_OFFSET 0x1C

// SPI_SR bit definitions
#define SPI_SR_RXNE (1 << 0) // Receive buffer not empty
#define SPI_SR_TXE  (1 << 1) // Transmit buffer empty
// This bit (0x400) was observed in the hardware trace (SR read 0x403)
// after a transfer. It is set with RXNE and cleared on DR read.
#define SPI_SR_MYSTERY_BIT (1 << 10)

// SPI_CR1 bit definitions
#define SPI_CR1_SPE (1 << 6) // SPI Enable

/**
 * @brief Holds the internal state of the SPI1 peripheral.
 */
typedef struct {
    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint32_t dr;
    uint32_t i2scfgr;

    // SPI bus handle
    SPIBus spi_bus;

    // Debugging
    char dbg_msg[128];
} spi1_state_t;

// Static instance of our device state
static spi1_state_t s;

/**
 * @brief Handles MMIO reads from the SPI1 peripheral.
 */
uint64_t spi1_read(void *opaque, hwaddr addr, unsigned size) {
    uint32_t offset = addr - SPI1_BASE;
    uint64_t value = 0;

    switch (offset) {
        case SPI_CR1_OFFSET:
            value = s.cr1;
            break;

        case SPI_CR2_OFFSET:
            value = s.cr2;
            break;

        case SPI_SR_OFFSET:
            value = s.sr;
            break;

        case SPI_DR_OFFSET:
            // --- CORRECTION ---
            // On read, clear the "Receive Not Empty" flag
            // and the mystery bit (0x400). This matches the hardware
            // behavior where SR becomes 0x2 after the read.
            s.sr &= ~SPI_SR_RXNE;
            s.sr &= ~SPI_SR_MYSTERY_BIT;

            value = s.dr;
            break;

        case SPI_I2SCFGR_OFFSET:
            value = s.i2scfgr;
            break;

        default:
            snprintf(s.dbg_msg, sizeof(s.dbg_msg), "SPI1: Unhandled read from offset 0x%x", offset);
            dev_debug(s.dbg_msg);
            break;
    }

    return value;
}

/**
 * @brief Handles MMIO writes to the SPI1 peripheral.
 */
void spi1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    uint32_t offset = addr - SPI1_BASE;

    switch (offset) {
        case SPI_CR1_OFFSET:
            s.cr1 = (uint32_t)value;
            // If SPI is enabled (SPE bit), TXE flag is set
            if (s.cr1 & SPI_CR1_SPE) {
                s.sr |= SPI_SR_TXE;
            }
            break;

        case SPI_CR2_OFFSET:
            s.cr2 = (uint32_t)value;
            break;

        case SPI_DR_OFFSET:
            // This is the data transfer trigger
            if (s.cr1 & SPI_CR1_SPE) {
                // 1. Mark Transmit buffer as not empty (processing)
                s.sr &= ~SPI_SR_TXE;

                // 2. Perform the full-duplex transfer on the SPI bus
                //    We call this to ensure any slave device logic is triggered.
                api_spi_transfer(&s.spi_bus, (uint32_t)value);

                // --- CORRECTION ---
                // The hardware trace (Loop 1) shows that reading DR
                // returns the *exact* value that was written (0x2A2A),
                // not 0xFFFFFFFF. This implies a loopback or echo.
                // We override the MISO value and store the written value.
                s.dr = (uint32_t)value;

                // 4. Mark Receive buffer as "Not Empty"
                s.sr |= SPI_SR_RXNE;

                // 5. Mark Transmit buffer as "Empty" again (ready for next write)
                s.sr |= SPI_SR_TXE;

                // --- CORRECTION ---
                // The hardware trace shows SR becomes 0x403 (TXE | RXNE | 0x400)
                // after the transfer. We must set this mystery bit.
                s.sr |= SPI_SR_MYSTERY_BIT;

            } else {
                snprintf(s.dbg_msg, sizeof(s.dbg_msg), "SPI1: Write to DR while SPI is disabled (SPE=0)");
                dev_debug(s.dbg_msg);
            }
            break;

        case SPI_I2SCFGR_OFFSET:
            s.i2scfgr = (uint32_t)value;
            break;

        default:
            snprintf(s.dbg_msg, sizeof(s.dbg_msg), "SPI1: Unhandled write to offset 0x%x value 0x%lx", offset, value);
            dev_debug(s.dbg_msg);
            break;
    }
}

/**
 * @brief Initializes the SPI1 device model.
 */
void spi1_init(ConfigSection* model_info) {
    // Clear the state struct
    memset(&s, 0, sizeof(s));

    // Set registers to their known reset values
    // SR: TXE=1, RXNE=0 (Reset value is 0x0002)
    // This is correct, as Loop 2 polls for 0x2 before writing.
    s.sr = SPI_SR_TXE;

    // I2SCFGR: Trace shows this is 0x0
    s.i2scfgr = 0x0;

    // CR2: Reset value is 0x0700 (8-bit data)
    // The init trace is consistent with this starting value.
    s.cr2 = 0x0700;

    // Initialize the SPI bus and register all configured slave devices
    s.spi_bus = api_spi_init_bus(model_info);

    dev_debug("SPI1: Device model initialized and corrected.");
}