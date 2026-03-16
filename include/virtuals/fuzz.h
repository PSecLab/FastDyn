#ifndef FUZZ_H
#define FUZZ_H

// Need this include for the ENABLE_LIBFUZZ definition
#include <config.h>

#if ENABLE_LIBFUZZ

void fuzz_add_observed_value(uint32_t val);

#else // stubs

static inline void fuzz_add_observed_value(uint32_t val) {
    (void)val;
}

#endif

#endif
