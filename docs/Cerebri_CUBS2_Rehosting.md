# Cerebri CUBS2 FastDyn Rehosting

This is the command runbook for rehosting the Zephyr `cerebri_cubs2` firmware
for `mr_vmu_tropic/mimxrt1064`.

Use the FastDyn checkout and configure the firmware/workspace locations:

```bash
cd /path/to/FastDyn
export CEREBRI_CUBS2_ROOT=/path/to/cerebri_cubs2
export CUBS2_WORKSPACE_ROOT=/path/to/cerebri-west-workspace
```

Use the repo-local FastDyn entrypoint. A global `fastdyn` may point at a
different checkout.

```bash
FASTDYN=./fastdyn-env/bin/fastdyn
CONFIG=configs/cerebri_cubs2_mr_vmu_tropic.toml
SVD=third_party/common/cmsis-svd-data
```

## Inputs

The default TOML supports sibling repositories, or the environment overrides
above for another layout:

```text
workspace/FastDyn
workspace/cerebri_cubs2
workspace/qemu
workspace/libhw
workspace/zephyr
workspace/modules
```

Firmware ELF:

```text
$CEREBRI_CUBS2_ROOT/build-mr_vmu_tropic/zephyr/zephyr.elf
```

FastDyn TOML:

```text
configs/cerebri_cubs2_mr_vmu_tropic.toml
```

## Apply SVD Patch

Patch the local CMSIS SVD before running `static-analyze`, `probe-run`, or
`trace-analyze`.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

git -C third_party/common/cmsis-svd-data \
  apply ../../../patches/MIMXRT1064_snvs_size.patch
```

If the patch is already applied, the command may fail. Verify the expected SNVS
range with:

```bash
rg -n -C 4 '<name>SNVS|<baseAddress>0x400D4000|<size>0xC00' \
  third_party/common/cmsis-svd-data/data/NXP/MIMXRT1064.svd
```

The fix changes the `SNVS` address block from `0x10000` bytes to `0xC00` bytes,
so FastDyn does not treat SNVS as covering adjacent MIMXRT1064 peripherals.

## Build Firmware

```bash
cd "$CEREBRI_CUBS2_ROOT"
nix run .#west-update
nix run .#build -- -p always
```

The expected board build directory is:

```text
$CEREBRI_CUBS2_ROOT/build-mr_vmu_tropic
```

## Static Analysis

Run this after changing the firmware ELF, TOML path, SVD, source roots, or build
root.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN static-analyze \
  -c "$CONFIG" \
  -s "$SVD" \
  --force
```

Static cache:

```text
fastdyn_static_analysis_cerebri_cubs2
```

If `probe-run` or `trace-analyze` reports `cache key mismatch`, rerun
`static-analyze --force`.

## Probe Run

Use `probe-run` only while discovering missing peripherals or collecting a new
trace for model generation.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN probe-run \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_probe
```

Preserve the output directory if needed:

```bash
$FASTDYN probe-run -p \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_probe
```

Known CUBS2 note: execution at `PC=0x00000000` can be valid because
`FixedWingOuterLoop_dostep` is relocated into ITCM at address `0x0`. Treat
`probe-run` panic/assert stops there as probe heuristics, not proof of firmware
panic. Use `fastdyn run` for final closed-loop validation.

## Trace Analyze

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN trace-analyze \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_analysis \
  --latest-run-dir fastdyn_work_cerebri_cubs2_probe
```

If a routing update exists:

```bash
$FASTDYN trace-analyze \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_analysis \
  --latest-run-dir fastdyn_work_cerebri_cubs2_probe \
  --apply-routing
```

Main outputs:

```text
fastdyn_work_cerebri_cubs2_analysis/prompt.txt
fastdyn_work_cerebri_cubs2_analysis/analysis.json
fastdyn_work_cerebri_cubs2_analysis/routing.json
```

## LLM Model Update

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN llm \
  -d fastdyn_work_cerebri_cubs2_analysis \
  --compile \
  --model gpt-5.4 \
  --reasoning-effort medium \
  --evaluate
```

If the LLM returns routing-only guidance, update `routing.json`, rerun
`trace-analyze --apply-routing`, then rerun `llm`.

## Full Rehosting Loop

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN static-analyze -c "$CONFIG" -s "$SVD" --force

$FASTDYN probe-run \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_probe

$FASTDYN trace-analyze \
  -c "$CONFIG" \
  -s "$SVD" \
  -o fastdyn_work_cerebri_cubs2_analysis \
  --latest-run-dir fastdyn_work_cerebri_cubs2_probe \
  --apply-routing

$FASTDYN llm \
  -d fastdyn_work_cerebri_cubs2_analysis \
  --compile \
  --model gpt-5.4 \
  --reasoning-effort medium \
  --evaluate
```

After the first valid static cache, repeat `probe-run`, `trace-analyze`, and
`llm`. Rerun `static-analyze --force` only when the cache inputs change.

## Closed-Loop Run

For the complete FastDyn plus Rumoca/SIL run, use:

```text
docs/Cerebri_CUBS2_Closed_Loop.md
```

## Notes

This rehosting was completed with manual agent guidance plus GDB scripting to
inspect RTOS state, control-loop progress, and model routing decisions. The
complete agentic orchestration flow is not currently pushed to the remote
repository, so this document intentionally records the reproducible commands
only.
