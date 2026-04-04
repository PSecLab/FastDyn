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
#include "fuzz_trace.h"

// ----------------------------------------------------------------------------------------------
// Purpose of this file, when forced_trace = true in fuzz.c, it will trace all blocks,
// and output the first discrepancy it sees in the coverage, allowing easier stability debugging.
// ----------------------------------------------------------------------------------------------

static GArray *current_run = NULL;
static GArray *previous_run = NULL;

static void fuzz_trace_print_run(GArray *run, const char *name) {
    printf("%s (%u entries):\n", name, run->len);
    for (guint i = 0; i < run->len; i++) {
        uint32_t val = g_array_index(run, uint32_t, i);
        printf("  [%4u] 0x%08x\n", i, val);
    }
}

// skip adding until initialized
void fuzz_trace_add_value(uint32_t val) {
    if (current_run) g_array_append_val(current_run, val);
}

// for debugging on forcing the same input to view changed path, forced_trace = true
// This is setup at compile time as its just a somewhat target specific debugging helper
void fuzz_trace_finish_run(void) {
    if (previous_run->len == 0) {
        printf("First run recorded.\n");
        g_array_append_vals(previous_run,
                            current_run->data,
                            current_run->len);
        g_array_set_size(current_run, 0);
        return;
    }

    gboolean identical = TRUE;
    guint min_len = MIN(previous_run->len, current_run->len);

    for (guint i = 0; i < min_len; i++) {
        uint32_t a = g_array_index(previous_run, uint32_t, i);
        uint32_t b = g_array_index(current_run, uint32_t, i);

        if (a != b) {
            printf("Runs diverge at index %u:\n", i);
            printf("  Previous: 0x%08x\n", a);
            printf("  Current : 0x%08x\n", b);
            identical = FALSE;
            break;
        }
    }

    if (identical && previous_run->len != current_run->len) {
        printf("Runs differ in length: prev=%u current=%u\n",
               previous_run->len,
               current_run->len);
        identical = FALSE;
    }

    if (!identical) {
        fuzz_trace_print_run(previous_run, "Previous run");
        fuzz_trace_print_run(current_run,  "Current run");
        exit(0);
    } else {
        printf("Run identical to previous.\n");
    }

    /* Replace previous with current */
    g_array_set_size(previous_run, 0);
    g_array_append_vals(previous_run,
                        current_run->data,
                        current_run->len);

    g_array_set_size(current_run, 0);
}

int fuzz_trace_init() {
    current_run  = g_array_new(FALSE, FALSE, sizeof(uint32_t));
    previous_run = g_array_new(FALSE, FALSE, sizeof(uint32_t));

    if (current_run && previous_run) return 0;
    return -1;
}