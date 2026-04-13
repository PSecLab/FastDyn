#!/usr/bin/env bash
# flash_firmware.sh
# Flash script for MAX78000 (FTHR_RevA) using Maxim MSDK OpenOCD

set -e

# 1. Update Path to Maxim SDK (Based on your log)
MAXIM_PATH="/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder"

# 2. Point to the specific Maxim OpenOCD binary
# The log shows the tools are in $MAXIM_PATH/Tools/OpenOCD
OPENOCD="$MAXIM_PATH/Tools/OpenOCD/openocd"

# 3. Set the Scripts path so OpenOCD finds max78000.cfg
OPENOCD_SCRIPTS="$MAXIM_PATH/Tools/OpenOCD/scripts"

# 4. Set MAX78000 Flash Address (from log: lma 0x10000000)
FLASH_ADDR="0x10000000"

# 5. Default firmware path (Updated to match your likely build output name if desired)
FIRMWARE="${1:-build/GPIO.elf}"

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: Firmware '$FIRMWARE' not found"
    echo "Usage: $0 <firmware.elf|firmware.bin>"
    exit 1
fi

echo "Flashing $FIRMWARE to MAX78000..."

# 6. Run OpenOCD
# -s: Sets the search path for config files
# -f: Loads interface (cmsis-dap) and target (max78000)
if [[ "$FIRMWARE" == *.bin ]]; then
    # Binary files need an explicit flash address
    $OPENOCD -s "$OPENOCD_SCRIPTS" \
      -f interface/cmsis-dap.cfg \
      -f target/max78000.cfg \
      -c "init; reset halt; program $FIRMWARE $FLASH_ADDR verify reset exit"
else
    # ELF files contain their own address information
    $OPENOCD -s "$OPENOCD_SCRIPTS" \
      -f interface/cmsis-dap.cfg \
      -f target/max78000.cfg \
      -c "init; reset halt; program $FIRMWARE verify reset exit"
fi

echo "✅ Flash done."