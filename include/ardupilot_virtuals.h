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

/**
 * @brief Storage Read Block function
 *
 * This function simulates reading a block of data from storage (EEPROM/Flash).
 * It is used to emulate the behavior of reading data from non-volatile memory
 * in ArduPilot firmware.
 *
 * @param cpu_index The index of the CPU (core) executing the instruction.
 * @param udata User-defined data passed to the function (not used here).
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
 */
void hrt_micros64(unsigned int cpu_index, void *udata);

#endif // FASTDYN_INCLUDE_ARDUPILOT_VIRTUALS_H