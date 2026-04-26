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
#include <virtuals/virt_fuzz.h>

#include "fuzz_trace.h"

bool                g_trace_enabled    = false;
#if ENABLE_AFLNET
static bool         g_trace_run_started = false;
static bool         g_trace_has_prev = false;
#endif

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

#if ENABLE_AFLNET
#include "afl-fastdyn.h"

/* ---- aflnet trace buffer functions ---- */
fastdyn_trace_run_t g_trace_completed   = { .count = 0 };
fastdyn_trace_run_t g_trace_current     = { .count = 0 };
static fastdyn_trace_run_t g_trace_prev = { .count = 0 };

void fuzz_trace_enable(void) {
    g_trace_enabled = true;
}

void fuzz_trace_reset(void) {
    g_trace_current.count = 0;
    g_trace_completed.count = 0;
    g_trace_prev.count = 0;
    g_trace_has_prev = false;
    g_trace_run_started = false;
}

void fuzz_trace_record_pc(uint32_t pc) {
    if (g_trace_current.count < FASTDYN_TRACE_CAP) {
        g_trace_current.entries[g_trace_current.count++] = pc;
    }
}

/*
 * Called from fuzz_snap_handler before fuzz_snapshot_flag is cleared.
 * Copies the current run's trace to g_trace_completed so aflnet can
 * read a stable snapshot as soon as fastdyn_snap_restore() returns.
 */
void fuzz_trace_commit_run(void) {
    uint32_t n = g_trace_current.count;
    g_trace_completed.count = n;
    memcpy(g_trace_completed.entries, g_trace_current.entries,
           n * sizeof(uint32_t));
    g_trace_current.count = 0;
    g_trace_run_started = false;
}

/*
 * Called from fuzz_eth_in on the first packet injection of each run.
 * Discards any PCs accumulated between snap restore and the first inject
 * (the firmware's idle loop back to the input hook) since those are constant
 * every run and would only obscure real divergences.
 * Subsequent calls within the same run are no-ops.
 */
void fuzz_trace_on_inject(void) {
    if (!g_trace_enabled) return;
    if (!g_trace_run_started) {
        g_trace_current.count = 0;
        g_trace_run_started = true;
    }
}

/*
 * Compare g_trace_completed against the fixed baseline from the first run.
 * On the first call, writes the baseline to fastdyn_work/trace_prev.log.
 * On any subsequent divergence, writes fastdyn_work/trace_cur.log and exits.
 * No-op when trace is disabled.
 */
void fuzz_trace_compare(void) {
    if (!g_trace_enabled) return;

    uint32_t cur_count      = g_trace_completed.count;
    const uint32_t *cur_ent = g_trace_completed.entries;

    if (!g_trace_has_prev) {
        g_trace_prev.count = cur_count;
        memcpy(g_trace_prev.entries, cur_ent, cur_count * sizeof(uint32_t));
        g_trace_has_prev = true;
        printf("[trace] First run recorded (%u PCs).\n", cur_count);

        FILE *fp = fopen("fastdyn_work/trace_prev.log", "w");
        if (fp) {
            for (uint32_t i = 0; i < cur_count; i++)
                fprintf(fp, "[%6u] 0x%08x\n", i, cur_ent[i]);
            fclose(fp);
            printf("[trace] Baseline written to fastdyn_work/trace_prev.log\n");
        }
        return;
    }

    uint32_t min_len = g_trace_prev.count < cur_count ? g_trace_prev.count : cur_count;
    bool identical = true;
    uint32_t div_idx = 0;

    for (uint32_t i = 0; i < min_len; i++) {
        if (g_trace_prev.entries[i] != cur_ent[i]) {
            div_idx  = i;
            identical = false;
            break;
        }
    }

    if (identical && g_trace_prev.count != cur_count) {
        printf("[trace] Length mismatch: prev=%u cur=%u\n", g_trace_prev.count, cur_count);
        identical = false;
        div_idx = min_len;
    }

    if (!identical) {
        printf("[trace] Runs diverge at index %u:\n", div_idx);
        if (div_idx < min_len)
            printf("  Previous: 0x%08x\n  Current : 0x%08x\n",
                   g_trace_prev.entries[div_idx], cur_ent[div_idx]);

        uint32_t start = div_idx > 5 ? div_idx - 5 : 0;

        printf("[trace] Previous run (%u PCs):\n", g_trace_prev.count);
        for (uint32_t i = start; i < g_trace_prev.count && i < div_idx + 10; i++)
            printf("  [%4u] 0x%08x%s\n", i, g_trace_prev.entries[i],
                   i == div_idx ? "  <-- diverge" : "");

        printf("[trace] Current run (%u PCs):\n", cur_count);
        for (uint32_t i = start; i < cur_count && i < div_idx + 10; i++)
            printf("  [%4u] 0x%08x%s\n", i, cur_ent[i],
                   i == div_idx ? "  <-- diverge" : "");

        FILE *fp = fopen("fastdyn_work/trace_cur.log", "w");
        if (fp) {
            for (uint32_t i = 0; i < cur_count; i++)
                fprintf(fp, "[%6u] 0x%08x\n", i, cur_ent[i]);
            fclose(fp);
            printf("[trace] Current run written to fastdyn_work/trace_cur.log\n");
        }

        exit(0);
    } else {
        printf("[trace] Run identical to baseline (%u PCs).\n", cur_count);
    }
}

#endif
