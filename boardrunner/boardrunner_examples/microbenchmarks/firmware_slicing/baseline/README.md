# Halucinator slicing microbenchmark

Runs the `slice_probe` firmware inside the `halucinator` docker container
with two intercept handlers, N=1000 dispatches each, 10 iterations per
handler. Reports per-iter `SENTINEL_ELAPSED` + per-op cost.

## Handlers compared

- `PcUpdate`    (`halucinator.bp_handlers.sentinel_log.PcUpdate`)    — modifier analog: `return True, None`  (halucinator does PC=LR, emulated function skipped)
- `CounterVirt` (`halucinator.bp_handlers.sentinel_log.CounterVirt`) — virtual-callback analog: `counter++; return False, None`  (GDB single-steps BKPT, emulated function runs)

## Prereqs (one-time)

1. `halucinator` docker container up.
2. Inside container at `/root/microbench/`: `microbench_slice.axf` built for
   N=1000 (`-DN_INNER=1000 -DN_OUTER=1`) plus the three config YAMLs and
   `sentinel_log.py` (containing `PcUpdate` + `CounterVirt`).

Sync configs + handler from `halucinator_configs/`:

```bash
docker cp halucinator_configs/sentinel_log.py                       halucinator:/root/microbench/sentinel_log.py
docker cp halucinator_configs/sentinel_log.py                       halucinator:/root/halucinator/src/halucinator/bp_handlers/sentinel_log.py
docker cp halucinator_configs/microbench_slice_pcupdate_config.yaml halucinator:/root/microbench/
docker cp halucinator_configs/microbench_slice_counter_config.yaml  halucinator:/root/microbench/
```

Rebuild firmware at N=1000:

```bash
docker exec halucinator bash -c "cd /root/microbench && \
    arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
        -Wall -Wextra -O0 -g -ffreestanding -nostdlib -fno-common -fno-builtin \
        -DBENCH_SLICE -DN_INNER=1000 -DN_OUTER=1 \
        -T stm32_flash.ld -nostdlib -Wl,--gc-sections -Wl,-Map=build/output.map \
        startup.s uart.c microbench.c -o build/microbench_slice.axf && \
    arm-none-eabi-objcopy -O binary build/microbench_slice.axf build/microbench_slice.bin"
```

## Run

```bash
scripts/run_microbenchmarks.sh              # ITERS=10 (default), N=1000
ITERS=5 scripts/run_microbenchmarks.sh
```

## Output

Per-handler CSVs in `results/`:

```
results/slice_pcupdate_N1000.csv  # PcUpdate
results/slice_counter_N1000.csv   # CounterVirt
```

Each has columns `iter,elapsed_s,per_op_ms`.
