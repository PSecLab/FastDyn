#include <dlfcn.h>
#include <utils.h>
#include <core.h>
#include <common.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <virtuals/virt_fuzz.h>

#include "cov_trace.h"

bool                g_trace_enabled    = false;
static bool         g_trace_has_prev = false;

fastdyn_trace_run_t g_trace_completed   = { .count = 0, .capacity = 0, .entries = NULL };
fastdyn_trace_run_t g_trace_current     = { .count = 0, .capacity = 0, .entries = NULL };
static fastdyn_trace_run_t g_trace_prev = { .count = 0, .capacity = 0, .entries = NULL };

static void fuzz_trace_reserve(fastdyn_trace_run_t *run, uint32_t needed) {
    if (run->capacity >= needed) {
        return;
    }

    uint32_t capacity = run->capacity ? run->capacity : FASTDYN_TRACE_INITIAL_CAP;
    while (capacity < needed) {
        capacity *= 2;
    }

    uint32_t *entries = realloc(run->entries, capacity * sizeof(*entries));
    if (!entries) {
        fprintf(stderr, "[trace] Failed to allocate %u trace entries.\n", capacity);
        abort();
    }

    run->entries = entries;
    run->capacity = capacity;
}

void fuzz_trace_enable(void) {
    g_trace_enabled = true;
}

void fuzz_trace_disable(void) {
    g_trace_enabled = false;
}

void fuzz_trace_begin_window(void) {
    g_trace_current.count = 0;
}

void fuzz_trace_reset(void) {
    g_trace_current.count = 0;
    g_trace_completed.count = 0;
    g_trace_prev.count = 0;
    g_trace_has_prev = false;
}

void fuzz_trace_record_pc(uint32_t pc) {
    fuzz_trace_reserve(&g_trace_current, g_trace_current.count + 1);
    g_trace_current.entries[g_trace_current.count++] = pc;
}

/*
 * Called from fuzz_snap_handler before fuzz_snapshot_flag is cleared.
 * Copies the current run's trace to g_trace_completed so aflnet can
 * read a stable snapshot as soon as fastdyn_snap_restore() returns.
 */
void fuzz_trace_commit_run(void) {
    uint32_t n = g_trace_current.count;
    fuzz_trace_reserve(&g_trace_completed, n);
    g_trace_completed.count = n;
    if (n > 0) {
        memcpy(g_trace_completed.entries, g_trace_current.entries,
               n * sizeof(uint32_t));
    }
    g_trace_current.count = 0;
}

/*
 * Compare g_trace_completed against the fixed baseline from the first run.
 * On the first call, writes the baseline to fastdyn_work/trace_prev.log.
 * On any subsequent divergence, writes fastdyn_work/trace_cur.log and exits.
 * No-op when trace is disabled.
 */
void fuzz_trace_compare(void) {
    uint32_t cur_count      = g_trace_completed.count;
    const uint32_t *cur_ent = g_trace_completed.entries;

    if (cur_count == 0) {
        printf("[trace] Skipping empty testcase trace.\n");
        return;
    }

    if (!g_trace_has_prev) {
        fuzz_trace_reserve(&g_trace_prev, cur_count);
        g_trace_prev.count = cur_count;
        if (cur_count > 0) {
            memcpy(g_trace_prev.entries, cur_ent, cur_count * sizeof(uint32_t));
        }
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
