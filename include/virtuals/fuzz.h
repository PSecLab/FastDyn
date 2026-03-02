#ifndef FUZZ_H
#define FUZZ_H

// Need this include for the ENABLE_LIBFUZZ definition
#include <config.h>

#if ENABLE_LIBFUZZ

typedef void (*fuzz_anchor_callback_t)(uint8_t *buff, size_t len);

void fuzz_register_callback(fuzz_anchor_callback_t cb);

void virt_assert(unsigned int cpu_index, void *udata);
void anchor(unsigned int cpu_index, void *udata);

void fuzz_dump_bbl(void);
void fuzz_bbl_init(void);

void fuzz_add_observed_value(uint32_t val);

int fuzz_init(int argc, char **argv);

#else // link to functions that do nothing if not using fuzzer, in case use accidentally uses coverage=1

static inline void fuzz_add_observed_value(uint32_t val) {
    (void)val;
}

static inline void fuzz_dump_bbl(void) {
    (void)0;
}

static inline void fuzz_bbl_init(void) {
    (void)0;
}

#endif

#endif
