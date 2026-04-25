#ifndef FUZZ_TRACE_H
#define FUZZ_TRACE_H

#include <config.h>

void fuzz_trace_add_value(uint32_t val);
void fuzz_trace_finish_run(void);
int fuzz_trace_init();

#if ENABLE_AFLNET
/* ---- aflnet trace buffer helpers ---- */

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

/*
 * Call once per run at the moment the first Ethernet frame is injected.
 * Clears any PCs recorded between snap restore and first inject (firmware
 * idle loop), which are identical every run and not useful for comparison.
 * No-op on subsequent packets in the same run, and when trace is disabled.
 */
void fuzz_trace_on_inject(void);

/*
 * Compare g_trace_completed against the fixed baseline from the first run.
 * Call after fuzz_trace_commit_run() in fuzz_snap_handler.
 * Writes trace_prev.log after run 1; writes trace_cur.log and exits on divergence.
 */
void fuzz_trace_compare(void);

/* Reset the baseline so the next non-empty run becomes the new reference. */
void fuzz_trace_reset(void);
#else
static void fuzz_trace_record_pc(uint32_t pc) {
    (void)pc;
}
#endif

#endif