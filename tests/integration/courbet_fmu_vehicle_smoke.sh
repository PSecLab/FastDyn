#!/usr/bin/env bash
set -Eeuo pipefail

config="${FASTDYN_COURBET_CONFIG:?set FASTDYN_COURBET_CONFIG to a FastDyn TOML config}"
label="${FASTDYN_COURBET_LABEL:-$(basename "$config" .toml)}"
log_file="${FASTDYN_COURBET_LOG:-out/ci/${label}_fmu_smoke.log}"
timeout_sec="${FASTDYN_COURBET_TIMEOUT_SEC:-240}"

mkdir -p "$(dirname "$log_file")"
: >"$log_file"
start_seconds=$SECONDS

log_ci() {
  printf '[ci] +%ss %s\n' "$((SECONDS - start_seconds))" "$*" | tee -a "$log_file"
}

if [[ -f fastdyn-env/bin/activate ]]; then
  # shellcheck disable=SC1091
  source fastdyn-env/bin/activate
fi

run_pid=""
cleanup() {
  if [[ -n "$run_pid" ]] && kill -0 "$run_pid" 2>/dev/null; then
    kill -INT -- "-$run_pid" 2>/dev/null || kill -INT "$run_pid" 2>/dev/null || true
    sleep 3
    kill -TERM -- "-$run_pid" 2>/dev/null || kill -TERM "$run_pid" 2>/dev/null || true
    wait "$run_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

smoke_check() {
  python3 - "$log_file" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text(errors="replace")
checks = {
    "fmu_ready": "FMU ready:" in text,
    "fmu_backend_loaded": "FMU backend loaded:" in text,
    "lockstep_clock": "FMU backend advances synchronously from QEMU timer ticks" in text,
    "timer_irq_period": "Periodic IRQ 66 every 1000000 ns" in text,
    "mavcesium": "MAVCesium web viewer:" in text,
    "heartbeat": "[health] heartbeat from" in text,
    "telemetry": "[health] telemetry observed" in text,
}
if all(checks.values()):
    print("[ci] FMI v3 vehicle smoke criteria passed")
    raise SystemExit(0)
missing = ",".join(name for name, ok in checks.items() if not ok)
print(f"[ci] waiting for FMI v3 vehicle smoke criteria missing={missing}")
raise SystemExit(1)
PY
}

log_ci "starting FMI v3 vehicle smoke: label=${label} config=${config}"
setsid fastdyn run -c "$config" -o "out/ci/${label}_work" >>"$log_file" 2>&1 &
run_pid=$!

deadline=$((SECONDS + timeout_sec))
while (( SECONDS < deadline )); do
  if smoke_check | tee -a "$log_file"; then
    log_ci "FMI v3 vehicle smoke passed: ${label}"
    exit 0
  fi

  if ! kill -0 "$run_pid" 2>/dev/null; then
    wait "$run_pid" || true
    if smoke_check | tee -a "$log_file"; then
      log_ci "FMI v3 vehicle smoke passed after fastdyn exit: ${label}"
      exit 0
    fi
    log_ci "fastdyn exited before smoke criteria passed" >&2
    tail -200 "$log_file" >&2
    exit 1
  fi

  sleep 2
done

log_ci "timed out waiting for FMI v3 vehicle smoke criteria" >&2
tail -200 "$log_file" >&2
exit 1
