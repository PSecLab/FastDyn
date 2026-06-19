# ArduRover Rehosting Setup

This guide covers the legacy ArduRover/Courbet Gazebo setup. The maintained default FastDyn path uses the FMU/Rumoca backend; use this flow when you specifically need the older Gazebo backend with `LIBGZ=true` and `LIBHW=true`.

## Build Instructions

### Prerequisites
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

## Static Analysis Phase

Before running certain analyses or traces, execute the static analysis frontend to extract binary metadata, SVD structures, source maps, and callgraphs.

Run the static analyzer using your configuration (make sure to update the paths in the `configs/automatic_ardurover462.toml`):
```bash
fastdyn static-analyze -c configs/automatic_ardurover462.toml -s third_party/common/cmsis-svd-data --force
```
This extracts the analysis artifacts into the cache directory specified in your TOML configuration under `[Rehosting.directories]`.
