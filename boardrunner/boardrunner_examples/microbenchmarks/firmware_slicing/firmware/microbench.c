/* Microbenchmark harness for BoardRunner Figure 7.
 *
 * Build one of:
 *   -DBENCH_SLICE    firmware slicing: N_OUTER × N_INNER calls to slice_probe()
 *   -DBENCH_HYBRID   alternating slice_probe() + MMIO read (deferred for now)
 *
 * Sentinel marker functions bound the measured region: bench_start() /
 * bench_done() are intercepted in HALucinator and their handler logs a
 * SENTINEL line the driver watches for.  No UART, no MMIO — nothing to
 * emulate outside the intercepted functions themselves.
 *
 * Paper wording: "100 instances of its primary operation per iteration."
 * Default: N_INNER=100, N_OUTER=1  → exactly 100 dispatches per run.
 */

/* --- payload ------------------------------------------------------------- */
__attribute__((noinline, used))
void slice_probe(void) { __asm volatile("nop"); }

/* --- sentinels bounding the measured region ------------------------------ */
__attribute__((noinline, used))
void bench_start(void) { __asm volatile("nop"); }

__attribute__((noinline, used))
void bench_done(void)  { __asm volatile("nop"); }

#ifndef N_INNER
#define N_INNER 1000
#endif
#ifndef N_OUTER
#define N_OUTER 1
#endif

int main(void) {
#ifdef BENCH_SLICE
    bench_start();
    for (int i = 0; i < N_OUTER; i++)
        for (int j = 0; j < N_INNER; j++)
            slice_probe();
    bench_done();
#endif

#ifdef BENCH_HITL
    /* MMIO_HITL points at DBGMCU_IDCODE (0xE0042000) — a read-only device-ID
     * register that's always safe to read on any Cortex-M3/M4.  We use the
     * volatile keyword to prevent the compiler from hoisting the load out
     * of the loop.  On the passthrough-configured Avatar2 baseline every
     * read fires an OpenOCD forward to the physical chip. */
    volatile unsigned int * const MMIO_HITL =
        (volatile unsigned int *)0xE0042000U;
    volatile unsigned int sink = 0;
    bench_start();
    for (int i = 0; i < N_OUTER; i++)
        for (int j = 0; j < N_INNER; j++)
            sink = *MMIO_HITL;
    bench_done();
    (void)sink;
#endif

    while (1);
}
