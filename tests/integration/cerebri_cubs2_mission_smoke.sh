#!/usr/bin/env bash
set -Eeuo pipefail

config="${FASTDYN_CUBS2_CONFIG:-configs/cerebri_cubs2_mr_vmu_tropic.toml}"
work_dir="${FASTDYN_CUBS2_WORK_DIR:-out/ci/cerebri_cubs2_work}"
artifacts="$work_dir/cerebri_cubs2_fmi3"
result="${FASTDYN_CUBS2_RESULT:-$work_dir/cerebri_cubs2_mission.json}"
log_file="${FASTDYN_CUBS2_LOG:-out/ci/cerebri_cubs2_mission.log}"
timeout_sec="${FASTDYN_CUBS2_TIMEOUT_SEC:-300}"
network_setup="${FASTDYN_CUBS2_NETWORK_SETUP:-true}"
simulated_target="${CUBS2_FASTDYN_T_END:-40}"

mkdir -p "$(dirname "$log_file")" "$work_dir"
: >"$log_file"

if [[ -f fastdyn-env/bin/activate ]]; then
  # shellcheck disable=SC1091
  source fastdyn-env/bin/activate
fi

cleanup() {
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
  export FASTDYN_ENET_TAP=enet
fi

export CUBS2_FASTDYN_T_END="$simulated_target"
export FASTDYN_QEMU_MEMORY_DIR="$work_dir/memory"
export FASTDYN_QMP_SOCKET="/tmp/fastdyn-cubs2-ci-qmp.sock"

echo "[ci] launching rehosted cerebri_cubs2 FMI3 mission" | tee -a "$log_file"
set +e
timeout --signal=INT --kill-after=10 "$timeout_sec" \
  fastdyn run -c "$config" -o "$work_dir" >>"$log_file" 2>&1
run_rc=$?
set -e

summary="$artifacts/native-sim-summary.md"
flight_csv="$artifacts/native-sim-flight.csv"
timing="$work_dir/fastdyn_timing.jsonl"
if [[ ! -s "$summary" || ! -s "$flight_csv" || ! -s "$timing" ]]; then
  echo "[ci] CUBS2 mission artifacts are incomplete (fastdyn rc=$run_rc)" >&2
  tail -200 "$log_file" >&2
  exit 1
fi

simulated="$(awk -F, 'NR > 1 { value=$1 } END { print value }' "$flight_csv")"
overall_wall="$(jq -sr '[.[] | select(.event == "phase_end" and .phase == "fastdyn.run.total")] | last | .duration_s' "$timing")"
lockstep_speedup="$(awk -F'|' '$2 ~ /lockstep_speed_x/ { gsub(/[[:space:]]/, "", $3); print $3 }' "$summary")"
max_alt="$(awk -F'|' '$2 ~ /max_altitude_m/ { gsub(/[[:space:]]/, "", $3); print $3 }' "$summary")"
overall_speedup="$(awk -v simulated="$simulated" -v wall="$overall_wall" \
  'BEGIN { printf "%.9f", simulated / wall }')"

jq -n \
  --argjson passed "$([[ "$run_rc" == 0 ]] && echo true || echo false)" \
  --argjson simulated_seconds "$simulated" \
  --argjson overall_wall_seconds "$overall_wall" \
  --argjson overall_speedup_over_realtime "$overall_speedup" \
  --argjson lockstep_speedup_over_realtime "$lockstep_speedup" \
  --argjson max_altitude_m "$max_alt" \
  '{passed: $passed,
    simulated_seconds: $simulated_seconds,
    overall_wall_seconds: $overall_wall_seconds,
    overall_speedup_over_realtime: $overall_speedup_over_realtime,
    lockstep_speedup_over_realtime: $lockstep_speedup_over_realtime,
    max_altitude_m: $max_altitude_m}' >"$result"

printf '[ci] CUBS2 mission passed=%s simulated=%ss wall=%ss overall=%sx lockstep=%sx max_alt=%sm\n' \
  "$([[ "$run_rc" == 0 ]] && echo true || echo false)" "$simulated" "$overall_wall" \
  "$overall_speedup" "$lockstep_speedup" "$max_alt" | tee -a "$log_file"

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo '## FastDyn + cerebri_cubs2 FMI3 mission'
    echo
    echo '| Result | Simulated time | Overall wall time | Overall speedup | Lockstep speedup | Max altitude |'
    echo '|---|---:|---:|---:|---:|---:|'
    printf '| %s | %.3f s | %.3f s | **%.2fx** | %.2fx | %.2f m |\n' \
      "$([[ "$run_rc" == 0 ]] && echo PASS || echo FAIL)" "$simulated" "$overall_wall" \
      "$overall_speedup" "$lockstep_speedup" "$max_alt"
  } >>"$GITHUB_STEP_SUMMARY"
fi

if ((run_rc != 0)); then
  tail -200 "$log_file" >&2
  exit "$run_rc"
fi
