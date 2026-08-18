/* Microbenchmark harness for BoardRunner Figure 7.
 *
 * Build one of:
 *   -DBENCH_SLICE         firmware slicing: N_OUTER * N_INNER slice_probe() calls
 *   -DBENCH_HITL_READ     HITL reads:  N loads   from GPIOA_IDR   (0x40020010)
 *   -DBENCH_HITL_WRITE    HITL writes: N stores  to  GPIOA_BSRR   (0x40020018)
 *
 * Sentinel marker functions bound the measured region: bench_start() and
 * bench_done() are no-op functions whose PC addresses are used as
 * breakpoints (Avatar2) or vcpu_insn_exec_cb callback anchors (FastDyn).
 * The addresses shift with N -- downstream tooling MUST `nm` the ELF
 * instead of hard-coding.
 *
 * MMIO addresses:
 *   GPIOA_IDR   (0x40020010, read-only)  -- returns current input pin state
 *   GPIOA_BSRR  (0x40020018, write-only) -- atomic bit set/reset; writing 0
 *                                           is a valid no-op transaction
 * Both live inside STM32F4's vendor peripheral region 0x40000000-0x60000000,
 * which the passthrough / avatar-rmemory setups forward to the real chip.
 * We deliberately avoid Cortex-M Private Peripheral Bus (0xE0000000-
 * 0xE00FFFFF) because QEMU's armv7m CPU model handles that internally and
 * reads/writes there never escape to our forwarded range.
 *
 * Paper wording: "100 instances of its primary operation per iteration."
 * Default: N_INNER=100, N_OUTER=1 -> exactly 100 dispatches per run.
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
#define N_INNER 100
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

#ifdef BENCH_HITL_READ
    volatile unsigned int * const GPIOA_IDR =
        (volatile unsigned int *)0x40020010U;
    volatile unsigned int sink = 0;
    bench_start();
    for (int i = 0; i < N_OUTER; i++)
        for (int j = 0; j < N_INNER; j++)
            sink = *GPIOA_IDR;
    bench_done();
    (void)sink;
#endif

#ifdef BENCH_HITL_WRITE
    /* BSRR is write-only; writing 0 is a valid no-op transaction that still
     * costs a full SWD write round-trip -- exactly what we want to time. */
    volatile unsigned int * const GPIOA_BSRR =
        (volatile unsigned int *)0x40020018U;
    bench_start();
    for (int i = 0; i < N_OUTER; i++)
        for (int j = 0; j < N_INNER; j++)
            *GPIOA_BSRR = 0U;
    bench_done();
#endif

    while (1);
}
