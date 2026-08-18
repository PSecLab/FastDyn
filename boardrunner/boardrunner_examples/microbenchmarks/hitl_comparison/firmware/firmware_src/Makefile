# HITL microbenchmark firmware, built from vendored microbench.c.
#
# Produces read + write variants at three sizes:
#   build/microbench_hitl_read_N<N>.axf    (N loads from GPIOA_IDR)
#   build/microbench_hitl_write_N<N>.axf   (N stores to GPIOA_BSRR)
#   ...  for N in {100, 1000, 100000}.
#
# All variants are bare-metal Cortex-M4 ELFs. bench_start() and bench_done()
# are no-op functions used only as PC markers for Avatar2 breakpoints /
# FastDyn native-C callbacks; their addresses shift with N, so downstream
# tooling MUST nm the ELF instead of hard-coding.

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

BUILD   := build

CFLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
           -Wall -Wextra -O0 -g -ffreestanding -nostdlib \
           -fno-common -fno-builtin
LDFLAGS := -T stm32_flash.ld -nostdlib -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/output.map

COMMON_SRC := startup.s uart.c

N_SIZES := 100 1000 100000

READ_AXF  := $(foreach n,$(N_SIZES),$(BUILD)/microbench_hitl_read_N$(n).axf)
WRITE_AXF := $(foreach n,$(N_SIZES),$(BUILD)/microbench_hitl_write_N$(n).axf)
ALL_AXF   := $(READ_AXF) $(WRITE_AXF)
ALL_BIN   := $(ALL_AXF:.axf=.bin)
ALL_DIS   := $(ALL_AXF:.axf=.dis)

.PHONY: all clean
all: $(ALL_AXF) $(ALL_BIN) $(ALL_DIS)

$(BUILD)/microbench_hitl_read_N%.axf: $(COMMON_SRC) microbench.c | $(BUILD)
	$(CC) $(CFLAGS) -DBENCH_HITL_READ -DN_INNER=$* $(LDFLAGS) $^ -o $@

$(BUILD)/microbench_hitl_write_N%.axf: $(COMMON_SRC) microbench.c | $(BUILD)
	$(CC) $(CFLAGS) -DBENCH_HITL_WRITE -DN_INNER=$* $(LDFLAGS) $^ -o $@

%.bin: %.axf
	$(OBJCOPY) -O binary $< $@

%.dis: %.axf
	$(OBJDUMP) -d $< > $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
