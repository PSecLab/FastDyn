# Cerebri CUBS2 Closed-Loop FastDyn Runbook

This document starts from a built `cerebri_cubs2` firmware and runs the full
closed loop:

```text
FastDyn/QEMU firmware <-> TAP Ethernet <-> Zenoh/Rumoca SIL harness
```

The successful configuration uses:

```text
firmware IP: 192.0.2.1
host bridge IP: 192.0.2.2
Zenoh locator: udp/192.0.2.2:7447
UART model: lpuart6
UART model-side PTY: /tmp/lpuart6_pty
UART host-side PTY: /tmp/host_lpuart6
```

## Repositories

Expected checkout layout:

```text
/scratch/Fastdyn/zephyr_rehosting/FastDyn
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2
/scratch/Fastdyn/zephyr_rehosting/qemu
/scratch/Fastdyn/zephyr_rehosting/libhw
/scratch/Fastdyn/zephyr_rehosting/zephyr
/scratch/Fastdyn/zephyr_rehosting/modules
```

## Build Firmware

```bash
cd /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2

nix run .#west-update
nix run .#build -- -p always
```

Expected ELF:

```text
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/build-mr_vmu_tropic/zephyr/zephyr.elf
```

## Apply SVD Patch

Patch the local CMSIS SVD before building the static cache or running FastDyn.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

git -C third_party/common/cmsis-svd-data \
  apply ../../../patches/MIMXRT1064_snvs_size.patch
```

If the patch is already applied, verify that SNVS uses the corrected `0xC00`
size:

```bash
rg -n -C 4 '<name>SNVS|<baseAddress>0x400D4000|<size>0xC00' \
  third_party/common/cmsis-svd-data/data/NXP/MIMXRT1064.svd
```

This avoids the bad upstream `SNVS` range from swallowing nearby MIMXRT1064
peripherals during SVD-based range detection.

## Build FastDyn Runtime

Run this if `build/libfastdyn.so` or BoardRunner model `.so` files are missing
or stale.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

source ./setup.sh --build-qemu --build-gazebo --skip-optifuzz

make PROBE=true DEV=true LIBHW=true LIBGZ=true FLIGHT_CONTROLLERS=true DEBUG_PRINT=true LIBFUZZ=true

export LD_LIBRARY_PATH=/scratch/Fastdyn/zephyr_rehosting/libhw/out:$PWD/build:${LD_LIBRARY_PATH:-}

cmake -S boardrunner/boardrunner_sdk \
  -B boardrunner/boardrunner_sdk/build \
  -DFASTDYN_INCLUDE_DIR="$PWD/include" \
  -DQEMU_INCLUDE_DIR=/scratch/Fastdyn/zephyr_rehosting/qemu/include

cmake --build boardrunner/boardrunner_sdk/build -j
```

Use the repo-local FastDyn binary:

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

FASTDYN=./fastdyn-env/bin/fastdyn
CONFIG=configs/cerebri_cubs2_mr_vmu_tropic.toml
SVD=third_party/common/cmsis-svd-data
```

## Build Static Cache

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

$FASTDYN static-analyze \
  -c "$CONFIG" \
  -s "$SVD" \
  --force
```

## Network Setup

The ENET model opens TAP devices named `enet` and `enet2`. Pre-create them so
QEMU can attach without `CAP_NET_ADMIN`.

```bash
sudo modprobe tun

sudo ip link add br-fastdyn type bridge 2>/dev/null || true
sudo ip addr flush dev br-fastdyn
sudo ip addr add 192.0.2.2/24 dev br-fastdyn
sudo ip link set br-fastdyn up

sudo ip tuntap add dev enet mode tap user "$USER" 2>/dev/null || true
sudo ip tuntap add dev enet2 mode tap user "$USER" 2>/dev/null || true
sudo ip link set enet master br-fastdyn
sudo ip link set enet2 master br-fastdyn
sudo ip link set enet up
sudo ip link set enet2 up
```

If QEMU still prints `ioctl(TUNSETIFF) failed: Operation not permitted`, either
the TAP devices were not created for the current user or the process still lacks
permission to open `/dev/net/tun`.

## UART Setup

Start this before `fastdyn run`.

```bash
rm -f /tmp/lpuart6_pty /tmp/host_lpuart6 /tmp/socat_lpuart6.log

nohup socat -d -d \
  PTY,link=/tmp/lpuart6_pty,raw,echo=0 \
  PTY,link=/tmp/host_lpuart6,raw,echo=0 \
  > /tmp/socat_lpuart6.log 2>&1 &
```

Read the firmware console from another terminal:

```bash
cat /tmp/host_lpuart6
```

Expected early UART output:

```text
*** Booting Zephyr OS build ...
<inf> csyn_zenoh: csyn zenoh client udp/192.0.2.2:7447
<inf> csyn_zros: mocap decoded valid=1 pos=[0.000 0.000 0.100]
cubs2:~$
```

The UART is intentionally sparse. Use the SIL CSV/report files for control-loop
validation.

## Start FastDyn Firmware

Run this in its own terminal and leave it running until the SIL command exits.

```bash
cd /scratch/Fastdyn/zephyr_rehosting/FastDyn

LD_LIBRARY_PATH=/scratch/Fastdyn/zephyr_rehosting/libhw/out:$PWD/build:${LD_LIBRARY_PATH:-} \
  $FASTDYN run \
    -c "$CONFIG" \
    -s "$SVD" \
    -o fastdyn_work_cerebri_cubs2_run
```

Do not use `probe-run` for the closed-loop validation. `probe-run` is for model
discovery and can stop on valid CUBS2 ITCM execution at `PC=0x00000000`.

## Start SIL Harness

The SIL runner expects to supervise a `--sim` process. During FastDyn closed-loop
validation, FastDyn/QEMU is the firmware, so use a placeholder process.

```bash
printf '#!/usr/bin/env bash\nsleep infinity\n' > /tmp/fastdyn_external_sim_placeholder
chmod +x /tmp/fastdyn_external_sim_placeholder
```

Start the Rumoca/Zenoh SIL harness from the firmware repo:

```bash
cd /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2

nix run .#native-sim-sil-run -- \
  --sim /tmp/fastdyn_external_sim_placeholder \
  --scenario /tmp/rumoca-scenario.fastdyn.toml \
  --locator udp/192.0.2.2:7447 \
  --artifacts artifacts/fastdyn-sil \
  --t-end 40
```

The SIL command stops automatically at `--t-end`. After it exits and writes the
report, stop `fastdyn run` with `Ctrl+C`.

## Optional Network Checks

After FastDyn boots, ping the firmware:

```bash
ping -I br-fastdyn -c 3 192.0.2.1
ip neigh show 192.0.2.1
```

Capture traffic:

```bash
sudo tcpdump -i br-fastdyn -U -nnevvv -XX \
  'arp or icmp or udp port 7447' \
  | tee /scratch/Fastdyn/zephyr_rehosting/FastDyn/fastdyn_work_cerebri_cubs2_logs/tap_bridge_capture.log
```

Healthy traffic includes ARP, ICMP replies from `192.0.2.1`, and UDP traffic
between `192.0.2.1` and `192.0.2.2:7447`.

## Outputs

FastDyn run artifacts:

```text
/scratch/Fastdyn/zephyr_rehosting/FastDyn/fastdyn_work_cerebri_cubs2_run
```

SIL artifacts:

```text
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/artifacts/fastdyn-sil/native-sim-summary.md
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/artifacts/fastdyn-sil/native-sim-report.html
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/artifacts/fastdyn-sil/native-sim-flight.csv
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/artifacts/fastdyn-sil/native-sim-pwm.csv
/scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2/artifacts/fastdyn-sil/native-sim-mocap.csv
```

Known current result: the closed loop runs for the full 40 seconds and passes
transport/control-loop checks, but the flight validation still fails route and
altitude/crosstrack criteria. That is now a flight behavior issue, not a basic
rehosting/connectivity failure.

## Optional Native Baseline

To compare against the repo's normal Zephyr `native_sim` path without FastDyn:

```bash
cd /scratch/Fastdyn/zephyr_rehosting/cerebri_cubs2

nix run .#native-sim-sil-test -- \
  --artifacts artifacts/native-sim-baseline \
  --t-end 40
```
