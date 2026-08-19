"""Minimal BPHandler that logs a fixed tag AND measures the delta between
SLICE_START and SLICE_DONE internally, avoiding docker-exec pipe latency
that made external timing bimodal.

    - class: halucinator.bp_handlers.sentinel_log.SentinelLog
      function: bench_start
      registration_args: {tag: "SLICE_START"}
    - class: halucinator.bp_handlers.sentinel_log.SentinelLog
      function: bench_done
      registration_args: {tag: "SLICE_DONE"}

On bench_start the handler stamps a monotonic timestamp.  On bench_done it
computes the delta, prints:
    SENTINEL_ELAPSED: <seconds>
and continues.  The driver reads that single line — its wall-clock arrival
time is irrelevant because the delta was computed in-process.
"""

import gc
import time
from halucinator.bp_handlers.bp_handler import BPHandler, bp_handler
from halucinator import hal_log

_log = hal_log.getHalLogger()

# Class-level so bench_start and bench_done handlers share state even when
# HALucinator instantiates SentinelLog once per intercept.
_start_ts = [None]


class SentinelLog(BPHandler):
    def __init__(self):
        self.tag = {}

    def register_handler(self, qemu, addr, func_name, tag="TAG"):
        self.tag[addr] = tag
        return SentinelLog.emit

    @bp_handler
    def emit(self, qemu, addr):
        tag = self.tag[addr]
        if tag == "SLICE_START":
            # Freeze the cyclic GC across the measured window.  A collection
            # cycle firing between SLICE_START and SLICE_DONE would add
            # ~15 ms of unrelated jitter — matches the bimodal outliers we
            # saw before this guard was added.
            gc.disable()
            _start_ts[0] = time.monotonic()
            _log.info("SENTINEL: SLICE_START")
            return True, 0
        elif tag == "SLICE_DONE":
            now = time.monotonic()
            if _start_ts[0] is not None:
                elapsed = now - _start_ts[0]
                _start_ts[0] = None
            else:
                elapsed = None
            # Drain any deferred collection OUTSIDE the timed window before
            # logging / shutting down, so a large collection doesn't leak
            # into a subsequent iteration.
            gc.enable()
            gc.collect()
            if elapsed is not None:
                _log.info("SENTINEL_ELAPSED: %.9f", elapsed)
            else:
                _log.info("SENTINEL: SLICE_DONE (no start recorded)")
            try:
                qemu.halucinator_shutdown(0)
            except Exception:
                import os as _os
                _os._exit(0)
            return True, 0
        else:
            _log.info("SENTINEL: %s", tag)
            return True, 0


class PcUpdate(BPHandler):
    """Modifier-equivalent intercept: skip the emulated function.

    Config:
        - class: halucinator.bp_handlers.sentinel_log.PcUpdate
          function: slice_probe
          symbol:   slice_probe

    Handler body does nothing but `return True, None`. Halucinator's
    interceptor sees intercept=True and calls target.execute_return(None)
    on avatar2, which does PC = LR (and R0 = 0). The next target.cont()
    resumes at the caller -- the emulated slice_probe body is NEVER run.

    Semantic: identical to SkipFunc -- "skip this function, act as if it
    returned". Equivalent to FastDyn's inline modifier (r15 = r14) but
    routed through halucinator's Python dispatch + avatar2's execute_return
    RPC + a GDB round-trip on every hit. Measured cost ~40 ms/op on our
    baseline host, dominated by the execute_return path.
    """

    def __init__(self):
        pass

    def register_handler(self, qemu, addr, func_name):  # pylint: disable=unused-argument
        return PcUpdate.pc_update

    @bp_handler
    def pc_update(self, qemu, addr):  # pylint: disable=unused-argument
        # True → halucinator calls target.execute_return() which sets PC=LR.
        return True, None


# Module-level counter shared across all CounterVirt intercept instances,
# mirroring how FastDyn's native-C virtual callback in virtuals/microbench.c
# increments a static uint64_t slice_probe_cb_counter per hit.
_call_counter = [0]


class CounterVirt(BPHandler):
    """Virtual-callback-equivalent intercept: side-effect only, no PC touch.

    Config:
        - class: halucinator.bp_handlers.sentinel_log.CounterVirt
          function: slice_probe
          symbol:   slice_probe

    Handler body increments a module-level counter and returns
    (False, None). Because intercept=False, halucinator's interceptor
    skips execute_return() and just calls target.cont(). PC is still at
    the BKPT address, so GDB's standard step-over-breakpoint kicks in:
    temporarily unpatch the BKPT, single-step the original instruction,
    replace the BKPT, resume. The emulated slice_probe body then runs
    to completion naturally.

    Semantic: side-effect + observation -- mirrors FastDyn's native-C
    virtual callback in virtuals/microbench.c (`slice_probe_cb_counter++`
    while the emulated slice_probe still executes). The emulated function
    is NOT skipped; the intercept just observes and does a scalar update.

    Cost profile: ~10 ms/op, i.e. about 4x cheaper per hit than PcUpdate
    because the False path avoids halucinator's multi-step execute_return
    RPC. The bottleneck is the plain GDB round-trip for the cont+step-over
    exchange. The Python counter increment itself is nanoseconds-level and
    completely swamped by the RPC.
    """

    def __init__(self):
        pass

    def register_handler(self, qemu, addr, func_name):  # pylint: disable=unused-argument
        return CounterVirt.count_and_return

    @bp_handler
    def count_and_return(self, qemu, addr):  # pylint: disable=unused-argument
        # False → no execute_return; GDB step-over resumes execution,
        # letting the emulated slice_probe body run normally.
        _call_counter[0] += 1
        return False, None
