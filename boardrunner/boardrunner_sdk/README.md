# BoardRunner SDK

This directory builds:

- **`libboardrunner_vio.so`**: the shared “VIO / API” library (I2C, SPI, DMA, PTY helpers, etc.)
- **`model.so`**: a single **device model** compiled from `model/model.c`

The idea is simple: you keep **one** reusable VIO/API library, and you repeatedly rebuild **one** model (`model.so`) from `model.c` for different peripherals/boards/firmwares, then rename it to what your runtime expects (e.g., `uart.so`, `spi.so`, etc.).

---

## Layout
```bash
boardrunner_sdk/
├── boardrunner_vio/ # shared API/VIO implementation
│ └── src/
│ ├── dma/
│ ├── i2c/
│ ├── pty/
│ └── spi/
├── include/boardrunner/ # public headers for model writers
│ ├── vio.h # umbrella header (includes the per-VIO headers)
│ ├── dma.h
│ ├── i2c.h
│ ├── pty.h
│ └── spi.h
├── model/
│ └── model.c # user-edited model source; builds into model.so
├── CMakeLists.txt
└── build/ # out-of-tree build dir (generated)
│ └── libboardrunner_vio.so
│ └── model.so
```

## Prerequisites

- CMake (>= 3.16 recommended)
- GCC/Clang toolchain
- `glib-2.0` development headers (CMake uses `pkg-config` to find it)
- FastDyn shared include dir that provides **`device.h`**
- QEMU include dir (for QEMU-side headers you may include)

## Build

From `boardrunner/boardrunner_sdk`:

```bash
rm -rf build

cmake -S . -B build \
  -DFASTDYN_INCLUDE_DIR=/scratch/Fastdyn/FastDyn/include \
  -DQEMU_INCLUDE_DIR=/scratch/Fastdyn/qemu/include

cmake --build build -j
```

## Writing a Model
Your model code lives in:
    - model/model.c

Models typically include the VIO umbrella header:
```c
#include <boardrunner/vio.h>
```

## Producing Multiple Models (Rename Workflow)
This SDK intentionally builds one model target called model.so.

To create multiple models (e.g., UART and SPI) you do:

1. Edit model/model.c for UART
2. Build
3. Rename the output
4. Edit model/model.c for SPI
5. Build again
6. Rename again

For example,
```bash
# 1) Build UART model
cmake --build build -j
cp build/model.so build/uart.so

# 2) Modify model/model.c to implement SPI model
cmake --build build -j
cp build/model.so build/spi.so
```

Now you can pass both `uart.so` and `spi.so` to your runtime in the same run, and they will both reuse the same `libboardrunner_vio.so`.

> Tip: keep per-model outputs under a stable folder (e.g., build/models/uart.so, build/models/spi.so) if you want to archive multiple versions.

## Runtime Loading: Shared Library Path (RPATH vs LD_LIBRARY_PATH)
`model.so` is linked against `libboardrunner_vio.so`.

The typical setup is:

Keep `model.so` (or your renamed `uart.so`, `spi.so`) in the same directory as `libboardrunner_vio.so`.

The model uses an RPATH like `$ORIGIN` so the loader resolves the dependency next to the model.

That means if you move `uart.so` somewhere else, you must do one of the following:

#### Option A (recommended): move the VIO library with it

Put both files in the same directory:
```bash
somewhere/
  uart.so
  spi.so
  libboardrunner_vio.so
```

#### Option B: set LD_LIBRARY_PATH
If you keep `libboardrunner_vio.so` elsewhere:
```bash
export LD_LIBRARY_PATH=/path/to/dir/containing/libboardrunner_vio.so:$LD_LIBRARY_PATH
```

## Quick Troubleshooting
`libboardrunner_vio.so`: cannot open shared object file

Ensure `libboardrunner_vio.so` is in the same directory as your model ($ORIGIN layout), or

export LD_LIBRARY_PATH to include the directory containing the library.