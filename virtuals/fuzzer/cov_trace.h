#ifndef COV_TRACE_H
#define COV_TRACE_H

#include <config.h>

#define FASTDYN_TRACE_CAP 16384 * 4   /* PCs per run (64 KB per buffer) */

typedef struct {
    uint32_t count;
    uint32_t entries[FASTDYN_TRACE_CAP];
} fastdyn_trace_run_t;


/* Record one PC into g_trace_current (no-op if trace not enabled). */
void fuzz_trace_record_pc(uint32_t pc);

/*
 * Commit the current run: copy g_trace_current → g_trace_completed and
 * reset g_trace_current.count to zero.
 *
 * Must be called from fuzz_snap_handler *before* fuzz_snapshot_flag is
 * cleared so that fastdyn_snap_restore() returns only after the snapshot
 * is stable.
 */
void fuzz_trace_commit_run(void);

/* Enable recording into g_trace_current (idempotent). */
void fuzz_trace_enable(void);

/* Clear only the current in-progress trace window. */
void fuzz_trace_begin_window(void);

/*
 * Compare g_trace_completed against the fixed baseline from the first run.
 * Call after fuzz_trace_commit_run() in fuzz_snap_handler.
 * Writes trace_prev.log after run 1; writes trace_cur.log and exits on divergence.
 */
void fuzz_trace_compare(void);

/* Reset the baseline so the next non-empty run becomes the new reference. */
void fuzz_trace_reset(void);

#endif
