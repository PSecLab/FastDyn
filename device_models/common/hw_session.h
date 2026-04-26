#ifndef HW_SESSION_H
#define HW_SESSION_H

#include <stdint.h>
#include <device.h>

/*
 * hw_session: process-singleton wrapper around a libhw probe.
 *
 * One physical debug probe maps to one session. Multiple device-model
 * backends (passthrough, twintrace-record) acquire the same session by
 * backend name; the first acquire opens the probe and starts a single
 * IRQ-poll thread, later acquires bump a refcount and return the same
 * handle. This eliminates the libusb double-claim that occurs when each
 * backend privately calls hw_connect().
 *
 * Sessions are process-scoped; there is no release/teardown API. State
 * lives until process exit, matching the rest of the device-model layer.
 */

typedef struct hw_session hw_session_t;

/*
 * Acquire the session for `backend` (e.g. "stlink", "openocd", "jlink").
 *
 * Returns a borrowed handle (do not free). Subsequent acquires for the
 * same backend return the same handle. Returns NULL on hard failure
 * (probe unavailable, OOM, thread create failure, registry full); the
 * caller is expected to utils_die.
 *
 * Thread-safe: serialized by an internal registry mutex.
 */
hw_session_t *hw_session_acquire(const char *backend);

/*
 * Read `size` bytes (1 or 4) from `addr` over the probe.
 *
 * Retries up to 100x at 1ms while the board is halted (mid-IRQ). Returns
 * 0 on success and writes the value into *out; returns non-zero on hard
 * failure after retries exhausted. The caller is responsible for the
 * error policy (utils_die vs swallow).
 *
 * `pc` is plumbed through for diagnostic logging on failure only.
 */
int hw_session_read(hw_session_t *s, hwaddr addr, unsigned size,
                    uint64_t *out, uint64_t pc);

/*
 * Write `size` bytes (1 or 4) at `addr` over the probe. Same retry and
 * error semantics as hw_session_read.
 */
int hw_session_write(hw_session_t *s, hwaddr addr, uint64_t value,
                     unsigned size, uint64_t pc);

/*
 * DeviceModel.serve handler: writes the captured firing line into r1,
 * resumes the board, and clears the session's pending-IRQ state.
 *
 * The `qemu_line` argument (QEMU's exception number) is ignored; we resume
 * on the board-side line captured by the poll thread. This matches the
 * pre-refactor passthrough_serve at passthrough.c:152.
 *
 * Idempotent: a no-op when no IRQ is pending. Safe under spurious or
 * duplicate dispatches.
 */
int hw_session_serve(hw_session_t *s, int qemu_line);

#endif /* HW_SESSION_H */
