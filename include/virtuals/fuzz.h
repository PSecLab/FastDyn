#ifndef FUZZ_H
#define FUZZ_H

// Need this include for the ENABLE_LIBFUZZ definition
#include <config.h>
#include <stdio.h>
#include <time.h>

#if ENABLE_LIBFUZZ

typedef void (*fuzz_anchor_callback_t)(char *buff, size_t len);

void fuzz_register_callback(fuzz_anchor_callback_t cb);

void virt_assert(unsigned int cpu_index, void *udata);
void anchor(unsigned int cpu_index, void *udata);

void fuzz_add_observed_value(uint32_t val);

int fuzz_init(int argc, char **argv);

#else // stubs/simple implementations for coverage tracking when fuzzer not enabled

static inline void fuzz_add_observed_value(uint32_t val) {
    static GHashTable *bbl_set = NULL;
    if (!bbl_set) {
        bbl_set = g_hash_table_new(g_direct_hash, g_direct_equal);
        if (!bbl_set) {
            utils_die("Failed to allocate bbl_set");
        }
    }
    
    guint bbl_pc = val & (~1ULL);
    if (!g_hash_table_lookup(bbl_set, GUINT_TO_POINTER(bbl_pc))) {
        guint timestamp = (guint)(g_get_monotonic_time() / 1000);

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(bbl_pc),
            GUINT_TO_POINTER(timestamp)
        );

        FILE *f = fopen("fastdyn_work/bbl.txt", "a");
        if (!f) {
            utils_die("Failed to open dump file for writing");
        }
        fprintf(f, "%08x\t%u\n", val, timestamp);
        fclose(f);
    }
}

#endif

#endif
