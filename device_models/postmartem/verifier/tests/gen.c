#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Assuming device.h provides the necessary QEMU plugin API declarations,
// including the 'hwaddr' type and the 'dev_debug' function.
#include "device.h"

// Device Model for GPIOG

// Inferred Register base address and offsets
#define GPIOG_BASE_ADDR 0x40021800
#define GPIOG_MODER     0x00
#define GPIOG_OTYPER    0x04
#define GPIOG_OSPEEDR   0x08
#define GPIOG_PUPDR     0x0C
#define GPIOG_BSRR      0x18

// A struct to hold the state of the emulated GPIOG peripheral.
// We only need to store registers that are read back by the guest.
typedef struct {
    uint32_t moder;
    uint32_t otyper;
    uint32_t ospeedr;
    uint32_t pupdr;
} GPIOGState;

// A single global instance of our device's state.
static GPIOGState gpiog_dev;

/**
 * @brief Initializes the GPIOG device model.
 *
 * This function is called once at startup to set the emulated registers
 * to their initial state. The trace analysis shows that initial reads
 * from registers return 0x0, so we zero out the state to match this
 * observed behavior.
 *
 * @param opaque A user-defined pointer, not used in this model.
 */
void gpiog_init(void *opaque) {
	memset(&gpiog_dev, 0, sizeof(GPIOGState));
	dev_debug("INFO: GPIOG device model initialized.");
}

/**
 * @brief Emulates all MMIO reads from the GPIOG device.
 *
 * This function intercepts memory reads from the guest, calculates the
 * register offset, and returns the corresponding value from the device's
 * state struct. This correctly models the stateful nature of the
 * configuration registers.
 *
 * @param opaque A pointer to device-specific data (unused here).
 * @param addr The absolute memory address of the read access.
 * @param size The size of the read access (e.g., 4 for a 32-bit read).
 * @return The value of the requested register.
 */
uint64_t gpiog_read(void *opaque, hwaddr addr, unsigned size) {
    uint32_t offset = addr - GPIOG_BASE_ADDR;
    uint32_t value = 0;
    // char dbg_buf[128]; // Buffer for debug messages

	switch (offset) {
		case GPIOG_MODER:
			value = gpiog_dev.moder;
			break;
		case GPIOG_OTYPER:
			value = gpiog_dev.otyper;
			break;
		case GPIOG_OSPEEDR:
			value = gpiog_dev.ospeedr;
			break;
		case GPIOG_PUPDR:
			value = gpiog_dev.pupdr;
			break;
		default:
			// For reads to unimplemented or write-only registers, we return 0.
			// This is a safe default for most peripherals.
			break;
	}

    // Optional: Log the read access for debugging using the required API
    // snprintf(dbg_buf, sizeof(dbg_buf), "DEBUG: GPIOG Read from offset 0x%X, Value=0x%X", offset, value);
    // dev_debug(dbg_buf);

	return value;
}

/**
 * @brief Emulates all MMIO writes to the GPIOG device.
 *
 * This function intercepts memory writes from the guest. It updates the
 * internal state for the configuration registers. For the write-only BSRR
 * register, it decodes the written value to log the intended action
 * (setting or resetting a pin) using the dev_debug API.
 *
 * @param opaque A pointer to device-specific data (unused here).
 * @param addr The absolute memory address of the write access.
 * @param value The value being written by the guest.
 * @param size The size of the write access.
 */
void gpiog_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    uint32_t offset = addr - GPIOG_BASE_ADDR;
    uint32_t val32 = (uint32_t)value;
    // char dbg_buf[128]; // Buffer for debug messages

    // Optional: Log the write access for debugging using the required API
    // snprintf(dbg_buf, sizeof(dbg_buf), "DEBUG: GPIOG Write to offset 0x%X, Value=0x%X", offset, val32);
    // dev_debug(dbg_buf);

	switch (offset) {
		case GPIOG_MODER:
			gpiog_dev.moder = val32;
			break;
		case GPIOG_OTYPER:
			gpiog_dev.otyper = val32;
			break;
		case GPIOG_OSPEEDR:
			gpiog_dev.ospeedr = val32;
			break;
		case GPIOG_PUPDR:
			gpiog_dev.pupdr = val32;
			break;
		case GPIOG_BSRR:
			// The BSRR register is write-only.
			// Upper 16 bits reset pins (BRy), lower 16 bits set pins (BSy).
			if (val32 & 0xFFFF0000) { // Check if any reset bits are set
				uint32_t reset_bits = val32 >> 16;
				if (reset_bits & (1 << 13)) {
					// The trace shows value=0x20000000, which is 1<<(13+16).
					// This write targets BR13, resetting pin 13.
					dev_debug("INFO: Emulated GPIOG Pin 13 RESET (BR13).");
				}
			}
			if (val32 & 0x0000FFFF) { // Check if any set bits are set
				// Add logic here if pin setting needs to be logged.
			}
			break;
		default:
			// Silently ignore writes to unimplemented registers.
			break;
	}
}