#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

instances="${FASTDYN_SWARM_INSTANCES:-2}"
base_port="${FASTDYN_SWARM_BASE_PORT:-18000}"
smoke_sec="${FASTDYN_SWARM_SMOKE_SEC:-30}"
config="${FASTDYN_SWARM_CONFIG:-configs/copter462.toml}"
label="${FASTDYN_SWARM_LABEL:-$(basename "$config" .toml)}"
work_root="${FASTDYN_SWARM_WORK_ROOT:-out/ci/${label}_swarm}"

rm -rf "$work_root"
mkdir -p "$work_root"

echo "[ci] starting ${instances}-worker FastDyn swarm smoke: label=${label} config=${config}"
fastdyn swarm \
  -c "$config" \
  -n "$instances" \
  -o "$work_root" \
  --base-port "$base_port" \
  --smoke-sec "$smoke_sec"

for ((idx = 0; idx < instances; idx++)); do
  worker="$(printf 'worker-%03d' "$idx")"
  log_file="$work_root/logs/$worker.log"
  if [[ ! -f "$log_file" ]]; then
    echo "[ci] missing $worker log: $log_file" >&2
    exit 1
  fi
  grep -q "MAVCesium web viewer:" "$log_file" || {
    echo "[ci] $worker did not print MAVCesium URL" >&2
    exit 1
  }
  grep -q "FMU backend loaded" "$log_file" || {
    echo "[ci] $worker did not load the FMU backend" >&2
    exit 1
  }
  grep -q "Periodic IRQ 66 every 1000000 ns" "$log_file" || {
    echo "[ci] $worker did not use the 1 ms board tick" >&2
    exit 1
  }
done

echo "[ci] ${instances}-worker FastDyn swarm smoke passed"
