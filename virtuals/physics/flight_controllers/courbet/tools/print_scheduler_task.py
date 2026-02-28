import gdb
import atexit
import os

# =====================
# Configuration
# =====================

# BREAK_ADDR = "AP_Scheduler.cpp:237"
BREAK_ADDR = "*0x0807bd8a"
EXPR = "task.name"

LOG_PATH = os.path.join(os.getcwd(), "gdb_trace.log")

# =====================
# Logging helpers
# =====================

_log = open(LOG_PATH, "w", buffering=1)  # line-buffered

def log(msg):
    _log.write(msg + "\n")

def _cleanup():
    log("[gdb-python] Closing log file")
    _log.close()

atexit.register(_cleanup)

# =====================
# Breakpoint
# =====================

class LoggingBreakpoint(gdb.Breakpoint):
    def __init__(self, spec):
        super().__init__(spec)
        self.silent = True  # no GDB console spam

    def stop(self):
        try:
            val = gdb.parse_and_eval(EXPR)
            time_available = gdb.parse_and_eval("time_available")

            try:
                out = val.string()
                time_available_str = time_available.string()
            except Exception:
                out = str(val)
                time_available_str = str(time_available)

            log(f"[gdb-python] {EXPR} = {out}, time_available = {time_available_str}")

            if out == "GCS::update_send":
                # break for inspection
                log(f"[gdb-python] Hit target task '{out}', breaking execution")
                return True  # stop execution

        except gdb.error as e:
            log(f"[gdb-python] ERROR evaluating {EXPR}: {e}")

        return False  # always continue

# =====================
# Install breakpoint
# =====================

LoggingBreakpoint(BREAK_ADDR)
log(f"[gdb-python] Logging breakpoint set at {BREAK_ADDR}")
log(f"[gdb-python] Writing log to {LOG_PATH}")

