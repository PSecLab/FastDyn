#ifndef VIRT_FUZZ_H
#define VIRT_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Need this include for the ENABLE_LIBFUZZ definition
#include <config.h>

#define MAP_SIZE 65536 // should match AFL

int fuzz_init(int argc, char **argv);

// gets fuzzed data into buf of max size len, returns copied size
size_t fuzz_get_data(char* buf, size_t len);

// sets data that should be returned to fuzzer
void fuzz_set_data(char* buf, size_t len);

uint32_t fuzz_get_register(int reg);
void fuzz_set_register(uint32_t value, int reg);

int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

#if ENABLE_LIBFUZZ || ENABLE_AFLNET

void fuzz_bbl_observe(uint32_t pc, uint32_t size);
void fuzz_add_observed_value(uint32_t val);

#else // stubs

static inline void fuzz_bbl_observe(uint32_t pc, uint32_t size) {
    (void)pc; (void)size;
}

static inline void fuzz_add_observed_value(uint32_t val) {
    (void)val;
}

#endif

#endif
