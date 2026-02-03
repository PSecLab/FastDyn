# Makefile (ARM Cortex-M4F, Clang/LLVM bitcode pipeline, C + C++)
# - Compiles .cpp with clang++ and .c with clang (bitcode-in-obj via -flto=thin)
# - `make bc` merges objects into a single LLVM bitcode module (.bc)
# - `make exec` compiles .bc -> .o and links an ELF using GCC's libstdc++
# - Includes syscalls.c to satisfy newlib (_sbrk, _write, ...), so you don't get undefined symbol _sbrk

# ---------------- Toolchain / paths ----------------
TOOLCHAIN := /home/zzhong/proj/bind/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi

CC        := clang
CXX       := clang++
LLVM_LINK := llvm-link
LLVM_DIS  := llvm-dis
LLC       := llc

BIN       := RTOSDemo.axf
BUILD_DIR := build

# Adjust these if needed:
CMSIS_PATH ?= ./CMSIS_5/
PIECES     ?= /data/Pieces/
BSP_INSTALL ?= ./32f429idiscovery-bsp/
HAL_INSTALL ?= ./stm32f4xx_hal_driver/
CHECKED_C_INSTALL_INCLUDE ?= /data/checkedc/include

GCC_INSTALL_INCLUDE := $(TOOLCHAIN)/arm-none-eabi/include
GCCLIB             := $(TOOLCHAIN)/lib/gcc/arm-none-eabi/13.3.1/thumb/v7-m/nofp
SYSLIB             := $(TOOLCHAIN)/arm-none-eabi/lib/thumb/v7-m/nofp

# ---------------- Sources ----------------
CPP_SOURCES := main.cpp RunningAverage.cpp Adafruit_AHRS_Madgwick.cpp Adafruit_AHRS_Mahony.cpp arduino_sensorkit.cpp ardupilot.cpp cleanflight.cpp ntc_thermistor.cpp paparazzi.cpp pathtracing.cpp
C_SOURCES   := syscalls.c
ASM_SOURCES := startup.s

CPP_OBJS := $(CPP_SOURCES:%.cpp=$(BUILD_DIR)/%.o)
C_OBJS   := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJ_FILES := $(CPP_OBJS) $(C_OBJS)

STARTUP_OBJ := $(BUILD_DIR)/startup.o

BC_FILE  := $(BUILD_DIR)/$(BIN).bc
ELF_FILE := $(BUILD_DIR)/$(BIN)

# ---------------- Target (STM32F429, Cortex-M4F) ----------------
TARGET_TRIPLE := arm-none-eabi
ARCH_FLAGS := --target=$(TARGET_TRIPLE) -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16

DEFS := -DSTM32F429xx

INCLUDE_DIRS :=
INCLUDE_DIRS += -I$(PIECES)/partitioner/monitor/include
INCLUDE_DIRS += -I$(GCC_INSTALL_INCLUDE)
INCLUDE_DIRS += -I$(CHECKED_C_INSTALL_INCLUDE)
INCLUDE_DIRS += -I$(CMSIS_PATH)/Device/cmsis_device_f4/Include
INCLUDE_DIRS += -I$(CMSIS_PATH)/CMSIS/Core/Include
INCLUDE_DIRS += -I$(BSP_INSTALL)
INCLUDE_DIRS += -I$(HAL_INSTALL)/Inc

# ---------------- Compile flags ----------------
# Keep -flto=thin so .o contains LLVM bitcode; llvm-link can consume them.
COMMON_CFLAGS := $(ARCH_FLAGS) $(DEFS) $(INCLUDE_DIRS) -O0 -g -flto=thin -ffreestanding

CFLAGS   := $(COMMON_CFLAGS)
CXXFLAGS := $(COMMON_CFLAGS) -fno-exceptions -fno-rtti -fno-use-cxa-atexit

# ---------------- Link flags ----------------
LD_SCRIPT := ./scripts/stm32_flash.ld

# Use nano+nosys (OK even with syscalls.c; syscalls.c resolves the real symbols)
LDFLAGS  := $(ARCH_FLAGS) -T $(LD_SCRIPT) -Wl,-Map=$(BUILD_DIR)/output.map -Wl,--gc-sections
LDFLAGS  += -L$(GCCLIB) -L$(SYSLIB) -L./
LDFLAGS  += -specs=nano.specs -specs=nosys.specs

# Prevent clang++ from trying to link libc++/libc++abi/libunwind
CXXLINKFLAGS := -nostdlib++ -Wl,-Map=$(BUILD_DIR)/rtmk.map

# Explicitly link GCC C++ runtime + usual libs
LDLIBS := -lstdc++ -lsupc++ -lc -lm -lgcc

# ---------------- Rules ----------------
.PHONY: all clean bc exec

all: $(ELF_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# C++ objects
$(BUILD_DIR)/%.o: %.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@
	echo $(CXXFLAGS) > .cxxflags

# C objects
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -c $< -o $@
	echo $(CFLAGS) > .cflags

# Startup asm
$(STARTUP_OBJ): startup.s Makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Normal ELF link from objects
$(ELF_FILE): $(OBJ_FILES) $(STARTUP_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CXXLINKFLAGS) $^ $(LDLIBS) -o $@

# Merge into bitcode
bc: $(OBJ_FILES) | $(BUILD_DIR)
	$(warning Files are $(OBJ_FILES))
	$(LLVM_LINK) -v $^ -o $(BC_FILE)
	$(LLVM_DIS) $(BC_FILE)

# Compile .bc -> .o and link
exec: $(BC_FILE) $(STARTUP_OBJ)
	$(LLC) -filetype=obj $(BC_FILE) -o $(BUILD_DIR)/$(BIN)_from_bc.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CXXLINKFLAGS) \
		$(BUILD_DIR)/$(BIN)_from_bc.o $(STARTUP_OBJ) $(LDLIBS) -o $(ELF_FILE)

# Dependencies
-include $(CPP_OBJS:.o=.d)
-include $(C_OBJS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) .cflags .cxxflags
