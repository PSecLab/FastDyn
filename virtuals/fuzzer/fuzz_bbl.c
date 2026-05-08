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

// fuzzer needs to restart fastdyn with persistent work directory
#define DEFAULT_BBL_DUMP_PATH "fastdyn_work/bbl.txt"
static const char *bbl_dump_path = DEFAULT_BBL_DUMP_PATH;
static GHashTable *bbl_set;
static GHashTable *bbl_size;  /* pc -> instruction count, populated at TB translation time */
static int fd = -1;

static const char *resolve_bbl_dump_path(void) {
    const char *path = getenv("FASTDYN_BBL_FILE");
    if (path && path[0]) {
        return path;
    }

    const char *work_dir = getenv("FASTDYN_WORK_DIR");
    if (work_dir && work_dir[0]) {
        static char buffer[4096];
        snprintf(buffer, sizeof(buffer), "%s/bbl.txt", work_dir);
        return buffer;
    }

    return DEFAULT_BBL_DUMP_PATH;
}

void fuzz_bbl_observe(uint32_t pc, uint32_t size) {
    guint bbl_pc = pc & (~1ULL);
    g_hash_table_insert(bbl_size, GUINT_TO_POINTER(bbl_pc), GUINT_TO_POINTER((guint)size));
}

void fuzz_bbl_add(uint32_t pc) {
    guint bbl_pc = pc & (~1ULL);
    if (!g_hash_table_lookup(bbl_set, GUINT_TO_POINTER(bbl_pc))) {
        guint timestamp = (guint)(g_get_monotonic_time() / 1000);
        guint insn_count = GPOINTER_TO_UINT(g_hash_table_lookup(bbl_size, GUINT_TO_POINTER(bbl_pc)));

        g_hash_table_insert(
            bbl_set,
            GUINT_TO_POINTER(bbl_pc),
            GUINT_TO_POINTER(timestamp)
        );

        if (fd >= 0) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "%08x\t%u\t%u\n", bbl_pc, insn_count, timestamp);
            write(fd, buf, len);
        }
    }
}

void fuzz_dump_bbl(void)
{
    if (!bbl_set) {
        utils_die("bbl_set is not initialized");
    }

    // FILE *f = fopen(bbl_dump_path, "w");
    // if (!f) {
    //     utils_die("Failed to open dump file for writing");
    // }

    // GHashTableIter iter;
    // gpointer key, value;
    // int total = 0;

    // g_hash_table_iter_init(&iter, bbl_set);

    // while (g_hash_table_iter_next(&iter, &key, &value)) {
    //     guint pc = GPOINTER_TO_UINT(key);
    //     guint timestamp = GPOINTER_TO_UINT(value);

    //     /* Write as: hex_pc<TAB>timestamp */
    //     fprintf(f, "%08x\t%u\n", pc, timestamp);

    //     total++;
    // }

    close(fd);

    //printf("Dumped %d blocks\n", total);
}

void fuzz_bbl_init(void)
{
    int total = 0;
    bbl_dump_path = resolve_bbl_dump_path();

    bbl_set = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!bbl_set) {
        utils_die("Failed to allocate bbl_set");
    }

    bbl_size = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!bbl_size) {
        utils_die("Failed to allocate bbl_size");
    }

    FILE *ff = fopen(bbl_dump_path, "r");
    if (!ff) {
        printf("Allocating new bbl_set\n");
    } else {
        guint pc;
        guint insn_count;
        guint timestamp;

        /* Read lines like: 08001234<TAB>4<TAB>1573 */
        while (fscanf(ff, "%x\t%u\t%u", &pc, &insn_count, &timestamp) == 3) {
            total++;

            g_hash_table_insert(bbl_set,  GUINT_TO_POINTER(pc), GUINT_TO_POINTER(timestamp));
            g_hash_table_insert(bbl_size, GUINT_TO_POINTER(pc), GUINT_TO_POINTER(insn_count));
        }

        fclose(ff);
    }

    fd = open(bbl_dump_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
        utils_die("Failed to open dump file for writing");
    }

    printf("Loaded %d blocks\n", total);
}
