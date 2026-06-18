# ArduRover Rehosting Setup

This guide covers the legacy ArduRover/Courbet Gazebo setup. The maintained
default FastDyn path uses the FMU/Rumoca backend; use this flow when you
specifically need the older Gazebo backend with `LIBGZ=true` and `LIBHW=true`.

## Prerequisites

Install Gazebo Harmonic if the Gazebo pkg-config modules are not available:

```bash
bash virtuals/physics/flight_controllers/courbet/utils/install_gazebo_harmonic.sh
```

The setup script checks for these modules before building the Gazebo path:

```bash
pkg-config --exists gz-transport13
pkg-config --exists gz-msgs10
pkg-config --exists protobuf
```

`LIBHW=true` also requires a buildable `libhw` checkout and its native ST-Link
dependency. On Debian/Ubuntu systems this is usually provided by `libstlink-dev`.

The default checkout layout is:

```text
automatic_ardurover_rehosting/
  FastDyn/
  qemu/
  libhw/
```

Use `--qemu-root` or `--libhw-root` if your local paths differ.

## One-Command Build

From the FastDyn repository root:

```bash
source ./setup.sh --build-qemu --build-gazebo --skip-optifuzz
```

Use this shorter form if patched QEMU is already built:

```bash
source ./setup.sh --build-gazebo --skip-optifuzz
```

`--skip-optifuzz` avoids setting up the Banquo checkout. It is not needed for
the Gazebo plugin build itself.

## What Setup Builds

`--build-gazebo` initializes the legacy Gazebo submodules and builds:

```text
third_party/courbet_deps/ardupilot_gazebo/build
virtuals/physics/flight_controllers/courbet/gazebo/build/services
virtuals/physics/physics_engines/gazebo/build/lib/libgz_wrapper.so
../libhw/out/libhw.so
build/libfastdyn.so
```

The final FastDyn plugin build is equivalent to:

```bash
make \
  qemu_path="$PWD/../qemu" \
  libhw_path="$PWD/../libhw" \
  LIBHW=true \
  LIBGZ=true \
  FLIGHT_CONTROLLERS=true \
  DEV=true \
  DEBUG_PRINT=true
```

If `--build-qemu` is included, setup first builds:

```text
../qemu/build/qemu-system-arm
```

## Manual Build Pieces

The manual dependency build sequence is:

```bash
cmake -S third_party/courbet_deps/ardupilot_gazebo \
  -B third_party/courbet_deps/ardupilot_gazebo/build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build third_party/courbet_deps/ardupilot_gazebo/build -j"$(nproc)"

cmake -S virtuals/physics/flight_controllers/courbet/gazebo \
  -B virtuals/physics/flight_controllers/courbet/gazebo/build
cmake --build virtuals/physics/flight_controllers/courbet/gazebo/build -j"$(nproc)"

cmake -S virtuals/physics/physics_engines/gazebo \
  -B virtuals/physics/physics_engines/gazebo/build
cmake --build virtuals/physics/physics_engines/gazebo/build -j"$(nproc)"

make -C ../libhw
```

Then run the FastDyn `make` command from the previous section.

## Running The Legacy Gazebo Pieces

Start the Courbet Gazebo services from the FastDyn root:

```bash
cd virtuals/physics/flight_controllers/courbet/gazebo
./run_and_attach_services.sh rover headless
```

For OptiFuzz experiments that intentionally use the legacy Gazebo backend:

```bash
cd virtuals/fuzzer/libafl_phi
FASTDYN_OPTIFUZZ_BACKEND=gazebo FASTDYN_OPTIFUZZ_VEHICLE=rover cargo run --bin baby_fuzzer
```

## Troubleshooting

If the FastDyn link fails with:

```text
cannot find -lgz_wrapper
cannot find -lhw
```

rerun:

```bash
source ./setup.sh --build-gazebo --skip-optifuzz
```

If setup reports a missing Gazebo module such as `gz-transport13`, install
Gazebo Harmonic with the script in the prerequisites section.

If setup reports that patched QEMU is missing, run:

```bash
source ./setup.sh --build-qemu --build-gazebo --skip-optifuzz
```
