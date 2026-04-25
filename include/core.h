#ifndef CORE_H
#define CORE_H

#include <stdint.h>

/**
 * @brief Get the current program counter (PC) value.
 *
 * @return uint64_t The current PC.
 */
uint64_t core_get_pc(void);
uint64_t core_get_icount(void);

/**
 * @brief Wait for the tracer thread to catch up to the inline logger
 */
void core_wait_for_trace_drain(void);

/**
 * @brief Read from RAM at the specified address.
 *
 * @param address The memory address to read from.
 * @param size The number of bytes to read.
 * @param buffer The buffer to store the read data.
 * @return int 0 on success, -1 on failure.
 */
int core_read_ram(uintptr_t address, size_t size, void* buffer);

/**
 * @brief Write to RAM at the specified address.
 *
 * @param address The memory address to write to.
 * @param size The number of bytes to write.
 * @param buffer The buffer containing the data to write.
 * @return int 0 on success, -1 on failure.
 */
int core_write_ram(uintptr_t address, size_t size, const void* buffer);

// Wrapper for qemu's irq registration, allows multiple hooks
void core_register_irq_hook(void (*cb)(int), void (*cb_end)(int));

// Allow users to register a hook for when fastdyn exits
void core_register_exit_hook(void (*cb)(void));
#endif /* CORE_H */

