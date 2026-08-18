#!/usr/bin/env bash
# Flash the idle `while(1)` firmware to the STM32F429I-DISC1.
#
# Why we flash: the microbenchmarks measure probe->MCU memory round-trips.
# The libhw stlink backend halts the CPU during connect() but resumes it
# before the benchmark loop runs (see libhw/backends/stlink/hw_stlink.c),
# so the target CPU is executing whatever is in flash while writes to
# 0x20000000 are timed. If that firmware happens to touch our SRAM region
# it clobbers the write and the read-back verification fails
# non-deterministically. The idle firmware pins the CPU to `while(1)` so
# no CPU-side memory activity contends with the probe.

set -euo pipefail

FIRMWARE="${FIRMWARE:-$(dirname "$(readlink -f "$0")")/../idle_firmware/microbench_idle.axf}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
INTERFACE_CFG="${INTERFACE_CFG:-interface/stlink.cfg}"
TARGET_CFG="${TARGET_CFG:-target/stm32f4x.cfg}"

if [[ ! -f "${FIRMWARE}" ]]; then
    echo "flash.sh: firmware not found: ${FIRMWARE}" >&2
    exit 1
fi

if ! command -v openocd >/dev/null 2>&1; then
    echo "flash.sh: openocd not on PATH" >&2
    exit 1
fi

LOG=$(mktemp -t flash-openocd.XXXXXX.log)
trap 'rm -f "${LOG}"' EXIT

echo "flash.sh: programming ${FIRMWARE}"
if ! openocd \
    -s "${OPENOCD_SCRIPTS}" \
    -f "${INTERFACE_CFG}" \
    -f "${TARGET_CFG}" \
    -c "program \"${FIRMWARE}\" verify reset exit" \
    >"${LOG}" 2>&1; then
    echo "flash.sh: openocd failed; last 30 lines:" >&2
    tail -30 "${LOG}" >&2
    exit 1
fi

# openocd prints "** Verified OK **" on success; anything else is a lie
if ! grep -q 'Verified OK' "${LOG}"; then
    echo "flash.sh: openocd exited 0 but did not report 'Verified OK'; full log:" >&2
    cat "${LOG}" >&2
    exit 1
fi

echo "flash.sh: OK"
