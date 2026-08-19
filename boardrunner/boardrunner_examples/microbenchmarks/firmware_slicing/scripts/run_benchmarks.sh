#!/usr/bin/env bash
# Build (if missing) and run both microbenchmarks in the container, write
# timestamped logs to results/, print min/median/max/mean per bench.
#
# Usage:  ./run_benchmarks.sh [iterations] [n_inner]   (defaults: 1000 1000)
#
# `iterations` = number of QEMU runs per bench (statistical samples).
# `n_inner`    = ops per run baked into the firmware. Must match the
#                --build-arg N_INNER used when building the image.
# Reported numbers are per-op nanoseconds: elapsed_ns / n_inner.

set -eu

ITERS="${1:-1000}"
N_INNER="${2:-1000}"
IMAGE="fastdyn-slice:latest"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH_ROOT="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(cd "$BENCH_ROOT/../../../.." && pwd)"
RESULTS="$BENCH_ROOT/results"
STAMP="$(date +%Y%m%d-%H%M%S)"

mkdir -p "$RESULTS"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">> building $IMAGE"
    docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE" "$REPO_ROOT"
fi

for BENCH in modifier virtual; do
    LOG="$RESULTS/${BENCH}_${STAMP}.log"
    echo ">> $BENCH x $ITERS -> $LOG"
    docker run --rm "$IMAGE" "$BENCH" "$ITERS" > "$LOG"
done

for BENCH in modifier virtual; do
    LOG="$RESULTS/${BENCH}_${STAMP}.log"
    # `|| true` so an empty log (no matches) doesn't abort under `set -e`.
    (grep -oE 'elapsed_ns=[0-9]+' "$LOG" || true) | cut -d= -f2 | sort -n | \
    awk -v b="$BENCH" -v N="$N_INNER" 'BEGIN{i=0}{a[i++]=$1;s+=$1} END{
        if (i==0) { printf "%-8s no samples\n", b; exit }
        mean = s/i
        ss = 0
        for (k=0; k<i; k++) { d = a[k]-mean; ss += d*d }
        stddev = (i > 1) ? sqrt(ss/(i-1)) : 0
        cv = (mean > 0) ? 100*stddev/mean : 0
        # Per-op ns: divide each per-run elapsed_ns by N_INNER (ops per run).
        # Log files still carry raw per-run elapsed_ns lines.
        printf "%-8s n=%d  min=%.2f  median=%.2f  max=%.2f  mean=%.2f  stddev=%.2f  cv=%.1f%%  (ns/op, N=%d ops/run)\n",
               b, i, a[0]/N, a[int(i/2)]/N, a[i-1]/N, mean/N, stddev/N, cv, N}'
done
