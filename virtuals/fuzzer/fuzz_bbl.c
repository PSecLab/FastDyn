#include <dlfcn.h>
#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "fuzz_bbl.h"

// fuzzer needs to restart fastdyn with persistant work directory
#define DEFAULT_BBL_DUMP_PATH "fastdyn_work/bbl.txt"
static const char *bbl_dump_path = DEFAULT_BBL_DUMP_PATH;
static GHashTable *bbl_set;

void fuzz_bbl_add(uint32_t pc) {
    guint bbl_pc = pc & (~1ULL);
    if (!g_hash_table_lookup(bbl_set, GUINT_TO_POINTER(bbl_pc))) {
        guint timestamp = (guint)(g_get_monotonic_time() / 1000);

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(bbl_pc),
            GUINT_TO_POINTER(timestamp)
        );
    }
}

void fuzz_dump_bbl(void)
{
    if (!bbl_set) {
        utils_die("bbl_set is not initialized");
    }

    FILE *f = fopen(bbl_dump_path, "w");
    if (!f) {
        utils_die("Failed to open dump file for writing");
    }

    GHashTableIter iter;
    gpointer key, value;
    int total = 0;

    g_hash_table_iter_init(&iter, bbl_set);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        guint pc = GPOINTER_TO_UINT(key);
        guint timestamp = GPOINTER_TO_UINT(value);

        /* Write as: hex_pc<TAB>timestamp */
        fprintf(f, "%08x\t%u\n", pc, timestamp);

        total++;
    }

    fclose(f);

    printf("Dumped %d blocks\n", total);
}

void fuzz_bbl_init(void)
{
    int total = 0;

    bbl_set = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!bbl_set) {
        utils_die("Failed to allocate bbl_set");
    }

    FILE *f = fopen(bbl_dump_path, "r");
    if (!f) {
        printf("Allocating new bbl_set\n");
        return;
    }

    guint pc;
    guint timestamp;

    /* Read lines like: 08001234<TAB>1573 */
    while (fscanf(f, "%x\t%u", &pc, &timestamp) == 2) {
        total++;

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(pc),
            GUINT_TO_POINTER(timestamp)
        );
    }

    fclose(f);

    printf("Loaded %d blocks\n", total);
}