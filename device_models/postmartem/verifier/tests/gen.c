#include <stdint.h>
#include <string.h>
#include "device.h" // Provides qemu_plugin_* APIs and hwaddr type

// Device Model for GPIOG

// Inferred Register Functions:
// 0x00 - MODER:   Configure pin mode (e.g., Input, Output)
// 0x04 - OTYPER:  Configure pin output type (e.g., Push-Pull)
// 0x08 - OSPEEDR: Configure pin output speed
// 0x0C - PUPDR:   Configure pin pull-up/pull-down resistors
// 0x14 - ODR:     Stores the output state for all pins
// 0x18 - BSRR:    Atomically sets or resets individual pins

/* Base address of the GPIOG peripheral */
#define GPIOG_BASE 0x40021800

/* Register offsets from the base address */
#define GPIOG_MODER   0x00
#define GPIOG_OTYPER  0x04
#define GPIOG_OSPEEDR 0x08
#define GPIOG_PUPDR   0x0C
#define GPIOG_ODR     0x14
#define GPIOG_BSRR    0x18

/**
 * @brief Holds the internal state of the emulated GPIOG peripheral.
 *
 * This structure contains variables for each of the major configuration
 * registers identified in the MMIO trace analysis.
 */
typedef struct {
    uint32_t moder;
    uint32_t otyper;
    uint32_t ospeedr;
    uint32_t pupdr;
    uint32_t odr; // Output Data Register
} GPIOGState;

// Global state for our GPIOG instance.
static GPIOGState gpiog_state;

/**
 * @brief Emulates memory-mapped reads from the GPIOG peripheral.
 *
 * This function is registered as a callback for MMIO reads. It returns the
 * current value of the targeted register from the device's state.
 *
 * @param opaque User-defined data (unused).
 * @param addr   The physical memory address being read.
 * @param size   The size of the read access in bytes.
 * @return The value of the requested register.
 */
uint64_t gpiog_read(void *opaque, hwaddr addr, unsigned size) {
    uint32_t offset = addr - GPIOG_BASE;
    uint32_t value = 0;

    switch (offset) {
        case GPIOG_MODER:
            value = gpiog_state.moder;
            break;
        case GPIOG_OTYPER:
            value = gpiog_state.otyper;
            break;
        case GPIOG_OSPEEDR:
            value = gpiog_state.ospeedr;
            break;
        case GPIOG_PUPDR:
            value = gpiog_state.pupdr;
            break;
        case GPIOG_ODR:
            value = gpiog_state.odr;
            break;
        default:
            // Unhandled reads can be logged here if necessary.
            break;
    }

    return value;
}

/**
 * @brief Emulates memory-mapped writes to the GPIOG peripheral.
 *
 * This function is registered as a callback for MMIO writes. It updates the
 * device's state based on the value written to a specific register.
 *
 * @param opaque User-defined data (unused).
 * @param addr   The physical memory address being written to.
 * @param value  The data being written to the register.
 * @param size   The size of the write access in bytes.
 */
void gpiog_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    uint32_t offset = addr - GPIOG_BASE;

    switch (offset) {
        case GPIOG_MODER:
            gpiog_state.moder = (uint32_t)value;
            break;
        case GPIOG_OTYPER:
            gpiog_state.otyper = (uint32_t)value;
            break;
        case GPIOG_OSPEEDR:
            gpiog_state.ospeedr = (uint32_t)value;
            break;
        case GPIOG_PUPDR:
            gpiog_state.pupdr = (uint32_t)value;
            break;
        case GPIOG_ODR:
            gpiog_state.odr = (uint32_t)value;
            break;
        case GPIOG_BSRR:
            // BSRR provides atomic bit-level control of the ODR register.
            // A single write can set some bits and clear others.
            // The lower 16 bits (BSy) set the corresponding ODR bits to 1.
            gpiog_state.odr |= (value & 0xFFFF);
            // The upper 16 bits (BRy) clear the corresponding ODR bits to 0.
            gpiog_state.odr &= ~((value >> 16) & 0xFFFF);
            break;
        default:
            // Unhandled writes can be logged here if necessary.
            break;
    }
}

/**
 * @brief Initializes the GPIOG device model state.
 *
 * This function should be called once at startup to reset the peripheral's
 * state to its default values. Based on the STM32 reference manual and the
 * trace data (which shows initial reads returning 0), the registers are
 * all zeroed out on reset.
 */
void gpiog_init() {
    memset(&gpiog_state, 0, sizeof(GPIOGState));
}
