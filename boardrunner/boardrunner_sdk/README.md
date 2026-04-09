# BoardRunner SDK

This directory builds:

- **`libboardrunner_vio.so`**: the shared “VIO / API” library (I2C, SPI, DMA, signals, PTY helpers, etc.)
- **`*.so`**: one **device model** per `model/*.c` (e.g., `model.c` → `model.so`, `slave.c` → `slave.so`)

The idea is simple: you keep **one** reusable VIO/API library, and you can build **multiple** models at once by placing sources in `model/`. Each `.c` file becomes its own `.so` with the same base name.

---

## Layout
```bash
boardrunner_sdk/
├── boardrunner_vio/ # shared API/VIO implementation
│ └── src/
│ ├── bus/
│ ├── dma/
│ ├── i2c/
│ ├── pty/
│ └── spi/
├── include/boardrunner/ # public headers for model writers
│ ├── vio.h # umbrella header (includes the per-VIO headers)
│ ├── dma.h
│ ├── i2c.h
│ ├── signals.h
│ ├── pty.h
│ └── spi.h
├── model/
│ ├── model.c # user-edited model source; builds into model.so
│ └── slave.c # example model; builds into slave.so
├── tests/
│ └── test_signals.c
├── CMakeLists.txt
└── build/ # out-of-tree build dir (generated)
│ └── libboardrunner_vio.so
│ ├── model.so
│ └── slave.so
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

# Update to your paths
cmake -S . -B build \
  -DFASTDYN_LIB=/data/Code/rehosting/FastDyn/build/libfastdyn.so \
  -DFASTDYN_INCLUDE_DIR=/data/Code/rehosting/FastDyn/include \
  -DQEMU_INCLUDE_DIR=/data/Code/rehosting/qemu/include

cmake --build build -j
```

### Build Configuration File (`build_config.env`)

When using `fastdyn llm --compile`, the build paths are read from a
`build_config.env` file in this directory. Fill it in before using the compile flag:

```bash
# boardrunner/boardrunner_sdk/build_config.env
FASTDYN_INCLUDE_DIR=/path/to/FastDyn/include
QEMU_INCLUDE_DIR=/path/to/qemu/include
```

This file is gitignored because it contains machine-specific paths.

## Writing a Model
Your model code lives in:
    - model/*.c (one file per model)


Models typically include the VIO umbrella header:
```c
#include <boardrunner/vio.h>
```

If a model only needs the shared inter-peripheral signal API, it may include:
```c
#include <boardrunner/signals.h>
```

For composed peripheral relationships, use the signals API as a level-based bridge:
- the source model publishes a logical line level with `api_signal_set(signal_id, level)`
- the sink model registers with `api_signal_register(signal_id, handler, opaque)`
- the sink model owns edge detection, pending-bit updates, and IRQ raising

## Producing Multiple Models (One File Each)
This SDK builds one `.so` per `.c` file in `model/`.

To create multiple models (e.g., UART and SPI) you do:

1. Create `model/uart.c` and `model/spi.c`
2. Build once

```bash
cmake --build build -j
```

Outputs will be:
```bash
build/uart.so
build/spi.so
```

Now you can pass both `uart.so` and `spi.so` to your runtime in the same run, and they will both reuse the same `libboardrunner_vio.so`.

> Tip: keep per-model outputs under a stable folder (e.g., build/models/uart.so, build/models/spi.so) if you want to archive multiple versions.

## Runtime Loading: Shared Library Path (RPATH vs LD_LIBRARY_PATH)
Each model `.so` is linked against `libboardrunner_vio.so`.

The typical setup is:

Keep your model `.so` files in the same directory as `libboardrunner_vio.so`.

The model uses an RPATH like `$ORIGIN` so the loader resolves the dependency next to the model.

That means if you move a model `.so` somewhere else, you must do one of the following:

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

## API Tests
API tests are opt-in and are not built as part of the normal model build.
They can be run completely independently of the FastDyn/QEMU runtime because
`test_signals` compiles the signals API directly instead of linking the full
`libboardrunner_vio.so` stack.

Build and run only the standalone API tests:

```bash
cmake -S . -B build_api_tests \
  -DBOARDRUNNER_BUILD_RUNTIME=OFF \
  -DBOARDRUNNER_BUILD_API_TESTS=ON

cmake --build build_api_tests -j --target test_signals
ctest --test-dir build_api_tests -V -R boardrunner_test_signals
```

Build both the normal runtime artifacts and the opt-in API tests:

```bash
cmake -S . -B build \
  -DFASTDYN_INCLUDE_DIR=/path/to/FastDyn/include \
  -DQEMU_INCLUDE_DIR=/path/to/qemu/include \
  -DBOARDRUNNER_BUILD_API_TESTS=ON

cmake --build build -j
#run the tests using
ctest --test-dir build -V -R boardrunner_test_signals
```

If you do not pass `-DBOARDRUNNER_BUILD_API_TESTS=ON`, no API test executables are
built and the normal `.so` model flow is unchanged. If you set
`-DBOARDRUNNER_BUILD_RUNTIME=OFF`, only the standalone API tests are configured.

## Quick Troubleshooting
`libboardrunner_vio.so`: cannot open shared object file

Ensure `libboardrunner_vio.so` is in the same directory as your model ($ORIGIN layout), or

export LD_LIBRARY_PATH to include the directory containing the library.
