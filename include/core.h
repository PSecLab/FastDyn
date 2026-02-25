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

void cov_irq_entry(int irq);
void cov_irq_exit(int irq);

// fuzz buffer read
int fuzz_buffer_read(char *buf, size_t buf_size);

// fuzz finish
void fuzz_finish();

// fuzz report assert
void fuzz_report_assert(bool fatal);

void add_observed_value(uint32_t val);
uint32_t dump_trace_info(void *hFuzz);
void reset_and_dump_values(const char *filename);
void print_unique_bbl();
void dump_bbl();
#endif /* CORE_H */

