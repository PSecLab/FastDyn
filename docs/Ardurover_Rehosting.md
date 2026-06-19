# ArduRover Rehosting Setup

This guide covers the legacy ArduRover/Courbet Gazebo setup. The maintained default FastDyn path uses the FMU/Rumoca backend; use this flow when you specifically need the older Gazebo backend with `LIBGZ=true` and `LIBHW=true`.

## Build Instructions

### Prerequisites
The trace analysis pipeline requires `universal-ctags` or `exuberant-ctags` for fallback source extraction. Please install it using your package manager (e.g., `sudo apt-get install universal-ctags`).

Gazebo Harmonic is required. If the Gazebo pkg-config modules (`gz-transport13`, `gz-msgs10`, `protobuf`) are not available, install them via:
```bash
bash virtuals/physics/flight_controllers/courbet/utils/install_gazebo_harmonic.sh
```
`LIBHW=true` also requires a buildable `libhw` checkout and `libstlink-dev`.

### Build
To initialize submodules, build Gazebo dependencies, and build FastDyn, run from the FastDyn root:
```bash
source ./setup.sh --build-qemu --build-gazebo --skip-optifuzz
```
*(Omit `--build-qemu` if the patched QEMU is already built).*

## 1. Static Analysis Phase

Before running certain analyses or traces, execute the static analysis frontend to extract binary metadata, SVD structures, source maps, and callgraphs.

Run the static analyzer using your configuration (make sure to update the paths in the `configs/automatic_ardurover462.toml`):
```bash
fastdyn static-analyze -c configs/automatic_ardurover462.toml -s third_party/common/cmsis-svd-data --force
```
This extracts the analysis artifacts into the cache directory specified in your TOML configuration under `[Rehosting.directories]`.

## 2. Probe Run Phase

This phase runs the binary on QEMU using the FastDyn plugin and consumes artifacts from the static analysis phase (e.g., `probe_faults.json`).

Run the command using your configuration:
```bash
fastdyn probe-run -c configs/automatic_ardurover462.toml -o fastdyn_recent_run
```
This will generate the artifacts under the `fastdyn_recent_run`

## 3. Trace Analyze Phase

After a probe run completes (or hangs), the trace analyzer parses the execution artifacts (`io.log`, `BBL_counts`) and correlates them with the static analysis data to produce a structured, deterministic LLM prompt. It avoids running QEMU or directly calling the LLM.

Run the command:
```bash
fastdyn trace-analyze -c configs/automatic_ardurover462.toml -o fastdyn_work --latest-run-dir fastdyn_recent_run
```
This will generate `fastdyn_work/prompt.txt` containing the hardware configuration, stateful analysis, tight loops, and full source context, ready to be passed to an LLM.
