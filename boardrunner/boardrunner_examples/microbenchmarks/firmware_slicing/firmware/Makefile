# Cortex-M4 firmware for the FastDyn firmware-slicing microbenchmark.
#
# Targets:
#   make                      Build with default N=100 (N_INNER=100, N_OUTER=1)
#   make N_INNER=1000         Build with 1000 slice_probe calls per run
#   make N_INNER=100 N_OUTER=10   Build with 100 x 10 = 1000 calls
#   make symbols              Print the PCs the plugin config expects
#   make clean                Remove build outputs

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
NM      := $(CROSS)nm

N_INNER ?= 100
N_OUTER ?= 1

BUILD   := build
TARGET  := microbench_slice

CFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
           -Wall -O0 -g -ffreestanding -nostdlib \
           -fno-common -fno-builtin \
           -DBENCH_SLICE -DN_INNER=$(N_INNER) -DN_OUTER=$(N_OUTER)
LDFLAGS := -T stm32_flash.ld -nostdlib -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/output.map

SRC := startup.s microbench.c

.PHONY: all symbols clean
all: $(BUILD)/$(TARGET).axf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).dis

$(BUILD)/$(TARGET).axf: $(SRC) stm32_flash.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) -o $@
	@echo "# built: N_INNER=$(N_INNER) N_OUTER=$(N_OUTER) -> $$(( $(N_INNER) * $(N_OUTER) )) slice_probe calls per run"

%.bin: %.axf
	$(OBJCOPY) -O binary $< $@

%.dis: %.axf
	$(OBJDUMP) -d $< > $@

# Print the PCs the plugin configs reference (mods.txt / virts.txt).
# Must match the values encoded there.
symbols: $(BUILD)/$(TARGET).axf
	@$(NM) $< | grep -E "slice_probe|bench_start|bench_done"

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
