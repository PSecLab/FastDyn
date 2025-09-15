/**
 * @brief Reusable virtuals for ardupilot header
 *
 * This file declares the common virtual functions used by ardupilot.
 *
 * @file FastDyn/include/ardupilot_virtuals.h
 * @author Michael Rooney
 */

#ifndef FASTDYN_INCLUDE_ARDUPILOT_VIRTUALS_H
#define FASTDYN_INCLUDE_ARDUPILOT_VIRTUALS_H

typedef struct port_intctx_t {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t lr;
} port_intctx_t;

typedef struct port_extctx_t {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} port_extctx_t;

typedef struct port_context_t {
    port_intctx_t intctx;
    port_extctx_t extctx;
} port_context_t;

/**
 * @brief Storage Read Block function
 *
 * This function simulates reading a block of data from storage (EEPROM/Flash).
 * It is used to emulate the behavior of reading data from non-volatile memory
 * in ArduPilot firmware.
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 *
 * Example usage in virtuals.txt:
 * 0xDEADBEEF storage_read_block *
 *
 * The '*' indicates that the virtual instruction is modifying the program counter (PC).
 * This tells QEMU to not execute the instruction at 0xDEADBEEF, but instead jump to the
 * new PC value set within the virtual function.
 */
void storage_read_block(unsigned int cpu_index, void *udata);

/**
 * @brief Storage Write Block function
 *
 * This function simulates writing a block of data to storage (EEPROM/Flash).
 * It is used to emulate the behavior of writing data to non-volatile memory
 * in ArduPilot firmware.
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 *
 * Example usage in virtuals.txt:
 * 0xDEADBEEF storage_write_block *
 */
void storage_write_block(unsigned int cpu_index, void *udata);

/**
 * @brief High-Resolution Timer function
 *
 * This function retrieves the current value of the high-resolution timer
 * in microseconds. It is used to emulate the behavior of getting the
 * system time in ArduPilot firmware.
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 *
 * Example usage in virtuals.txt:
 * 0xDEADBEEF hrt_micros64 *
 */
void hrt_micros64(unsigned int cpu_index, void *udata);

/**
 * @brief ArduPilot HAL Microseconds function
 *
 * This function retrieves the current value of the system timer in
 * microseconds. It is used to emulate the behavior of getting the
 * system time in ArduPilot firmware.
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 *
 * Example usage in virtuals.txt:
 * 0xDEADBEEF ap_hal_micros32
 */
void ap_hal_micros32(unsigned int cpu_index, void *udata);

/**
 * @brief ChibiOS helper to debug context switching
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 *
 * Example usage in virtuals.txt:
 * 0xDEADBEEF chDbgContextSwitching
 */
void chDbgContextSwitching(unsigned int cpu_index, void *udata);

/**
 * @brief Debug to just print that we reach here
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
 */
void debug_reached(unsigned int cpu_index, void *udata);

#endif // FASTDYN_INCLUDE_ARDUPILOT_VIRTUALS_H