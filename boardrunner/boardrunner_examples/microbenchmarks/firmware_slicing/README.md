# Firmware Slicing Microbenchmark

Modifier vs virtual dispatch overhead on a Cortex-M4 firmware, in a container.

## Run

```bash
# From FastDyn repo root:
./boardrunner/boardrunner_examples/microbenchmarks/firmware_slicing/scripts/run_benchmarks.sh 1000
```

Builds the image on first run, runs both benches, writes
`results/{modifier,virtual}_<timestamp>.log`, prints min/median/max/mean.

## Rebuild image with a larger firmware loop

```bash
docker build --build-arg N_INNER=1000000 \
    -f boardrunner/boardrunner_examples/microbenchmarks/firmware_slicing/scripts/Dockerfile \
    -t fastdyn-slice:latest .
```

Per-hit cost = `elapsed_ns / N_INNER`.
