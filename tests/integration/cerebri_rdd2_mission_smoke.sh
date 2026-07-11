#!/usr/bin/env bash
set -Eeuo pipefail

config="${FASTDYN_RDD2_CONFIG:-configs/cerebri_rdd2_mr_vmu_tropic.toml}"
work_dir="${FASTDYN_RDD2_WORK_DIR:-out/ci/cerebri_rdd2_work}"
report="${FASTDYN_RDD2_REPORT:-$work_dir/cerebri_rdd2_mission.json}"
log_file="${FASTDYN_RDD2_LOG:-out/ci/cerebri_rdd2_mission.log}"
timeout_sec="${FASTDYN_RDD2_TIMEOUT_SEC:-300}"
network_setup="${FASTDYN_RDD2_NETWORK_SETUP:-false}"

mkdir -p "$(dirname "$log_file")" "$work_dir"
: >"$log_file"

if [[ -f fastdyn-env/bin/activate ]]; then
  # shellcheck disable=SC1091
  source fastdyn-env/bin/activate
fi

zenoh_pid=""
cleanup() {
  if [[ -n "$zenoh_pid" ]] && kill -0 "$zenoh_pid" 2>/dev/null; then
    kill "$zenoh_pid" 2>/dev/null || true
    wait "$zenoh_pid" 2>/dev/null || true
  fi
  if [[ "$network_setup" == "true" ]]; then
    sudo ip link del br-fastdyn 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [[ "$network_setup" == "true" ]]; then
  sudo modprobe tun
  sudo ip link del br-fastdyn 2>/dev/null || true
  sudo ip link add br-fastdyn type bridge
  sudo ip addr add 192.0.2.2/24 dev br-fastdyn
  sudo ip link set br-fastdyn up
  for tap in enet enet2; do
    sudo ip tuntap del dev "$tap" mode tap 2>/dev/null || true
    sudo ip tuntap add dev "$tap" mode tap user "$USER"
    sudo ip link set "$tap" master br-fastdyn
    sudo ip link set "$tap" up
  done
  zenohd --listen udp/192.0.2.2:7447 >>"$log_file" 2>&1 &
  zenoh_pid=$!
fi

export RDD2_FASTDYN_REPORT="$report"
export FASTDYN_QEMU_MEMORY_DIR="$work_dir/memory"
export FASTDYN_QMP_SOCKET="/tmp/fastdyn-rdd2-ci-qmp.sock"

echo "[ci] launching rehosted cerebri_rdd2 mission" | tee -a "$log_file"
overall_start_ns="$(date +%s%N)"
set +e
timeout --signal=INT --kill-after=10 "$timeout_sec" \
  fastdyn run -c "$config" -o "$work_dir" >>"$log_file" 2>&1
run_rc=$?
set -e
overall_end_ns="$(date +%s%N)"

if [[ ! -s "$report" ]]; then
  echo "[ci] RDD2 mission did not produce $report (fastdyn rc=$run_rc)" >&2
  tail -200 "$log_file" >&2
  exit 1
fi

simulated="$(jq -r '.simulated_seconds' "$report")"
overall_wall="$(awk -v start="$overall_start_ns" -v end="$overall_end_ns" \
  'BEGIN { printf "%.9f", (end - start) / 1000000000 }')"
overall_speedup="$(awk -v simulated="$simulated" -v wall="$overall_wall" \
  'BEGIN { printf "%.9f", simulated / wall }')"
report_tmp="${report}.tmp"
jq --argjson wall "$overall_wall" --argjson speedup "$overall_speedup" \
  '.overall_wall_seconds = $wall | .overall_speedup_over_realtime = $speedup' \
  "$report" >"$report_tmp"
mv "$report_tmp" "$report"

passed="$(jq -r '.passed' "$report")"
max_alt="$(jq -r '.max_altitude_m' "$report")"
mission_speedup="$(jq -r '.speedup_over_realtime' "$report")"

printf '[ci] RDD2 mission passed=%s simulated=%ss mission_speedup=%sx launch_wall=%ss overall_speedup=%sx max_alt=%sm\n' \
  "$passed" "$simulated" "$mission_speedup" "$overall_wall" "$overall_speedup" "$max_alt" | tee -a "$log_file"

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo '## FastDyn + cerebri_rdd2 mission'
    echo
    echo '| Result | Simulated time | Mission speedup | Launch wall time | Overall launch speedup | Max altitude |'
    echo '|---|---:|---:|---:|---:|---:|'
    printf '| %s | %.3f s | **%.2fx** | %.3f s | %.2fx | %.2f m |\n' \
      "$passed" "$simulated" "$mission_speedup" "$overall_wall" "$overall_speedup" "$max_alt"
  } >>"$GITHUB_STEP_SUMMARY"
fi

if [[ "$passed" != "true" ]]; then
  jq -r '.failures[]' "$report" >&2
  exit 1
fi

if ((run_rc != 0)); then
  echo "[ci] FastDyn exited with status $run_rc after producing the report" >&2
  exit "$run_rc"
fi
