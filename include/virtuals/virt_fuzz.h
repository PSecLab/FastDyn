#ifndef VIRT_FUZZ_H
#define VIRT_FUZZ_H

#include <stdint.h>
#include <stdbool.h>

// Need this include for the ENABLE_LIBFUZZ definition
#include <config.h>

#if ENABLE_LIBFUZZ

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
