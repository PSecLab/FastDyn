# SPI — Transmit/Receive Byte (Loopback)

**Firmware:** SPI0 master polling mode. Transmits 16 bytes of `0xA5` at 100 kHz (8-bit width) and receives them back via an external MOSI-to-MISO loopback wire. Verifies all 16 received bytes match the transmitted data. LED green = pass, LED red = fail.

**Board:** MAX78000FTHR

**Required model goal:**

1. Transmit a byte
2. Receive a byte

---

## Hardware Wiring

| Connection | Pin | Header | Notes |
|-----------|-----|--------|-------|
| MOSI | P0.5 | J8 pin 12 | Wire to MISO |
| MISO | P0.6 | J8 pin 13 | Wire from MOSI |

Connect MOSI to MISO with a jumper wire to create the loopback path.

**LED indicators (on-board):**

| LED | Meaning |
|-----|---------|
| Green LED | Data verified — test passed |
| Red LED | Data mismatch — test failed |
| Console prints "Test PASSED" / "Test FAILED" | UART semihosting output |

---

## Compositional Model Split

Two models: SPI0 master controller + loopback slave. All other peripherals are passthrough.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | SPI0 (master controller) | `generated_models/spi0_model.c` | `model.so` |
| Slave model | Loopback wire | `generated_models/loopback_slave.c` | `slave.so` |
| Passthrough | GPIO0 (SPI AF pins), GPIO2 (LEDs), GCR, UART, TMR, all others | -- | real hardware |

**CS control:** SPI0's model infers CS from the SPI register trace. The slave exports the fixed symbols `slave_spi_transfer` and `slave_spi_set_cs`.

**Loopback slave behavior:** `slave_spi_transfer(byte)` returns the same byte it receives. `slave_spi_set_cs(level)` is a no-op (wire has no CS logic).

---

## Prerequisites

### OpenOCD (required for passthrough)

Before running any `fastdyn run` command, start OpenOCD in a **separate terminal**:

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

Keep this terminal open for the duration of the passthrough or hybrid run.

### Idle firmware

Ensure the idle firmware is flashed to the board:

```bash
boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/flash.sh \
  tests/idle_firmwares/prebuilt/idle_firmware_max78000.elf
```

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the SPI0 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `spi_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/spi_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1a -- Generate loopback slave model

Generate the slave model **first** -- the SPI0 model depends on it at runtime (the slave is loaded by `api_spi_init_bus` inside the SPI0 model).

The loopback slave is trivial: it echoes back whatever byte the master sends. Two inputs drive slave model generation:
- `--firmware-code (-fc)`: the master's `main.c` -- shows the transaction structure and expected loopback behavior
- `--reference-model (-rm)`: the STM32 BME280 `slave.c` -- structural reference for the SPI slave callback pattern

```bash
fastdyn generate --slave-model \
  -b Max78000 \
  -p SPI0 \
  -fc /scratch/Fastdyn/saved_maxim_examples/Examples/MAX78000/SPI/main.c \
  -rm boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/generated_models/bme280_slave.c \
  -o ./fastdyn_work_loopback
```

Send the generated slave prompt to the LLM and compile:

```bash
fastdyn llm -d fastdyn_work_loopback \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

> The generated slave must export exactly `slave_spi_transfer` and `slave_spi_set_cs` -- the framework resolves these fixed symbols via `dlsym` when loading `slave.so`.

### Step 1b -- Generate LLM prompt for SPI0 master model

Encode the hardware trace for SPI0. The Encoder extracts init vs. steady-state patterns and computes per-register entropy.

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p SPI0 \
  -mname "SPI0" -ms "SPI0" \
  -o ./fastdyn_work_spi0
```

### Step 2 -- Synthesize and compile the SPI0 model

```bash
fastdyn llm -d fastdyn_work_spi0 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 -- Run with both models (elder mode)

> **TOML state:** The TOML ships in elder mode (elder `enabled = true`, passthrough `enabled = false` for SPI0). If you changed it for Step 0, restore it now.

Both `model.so` (SPI0) and `slave.so` (loopback) must be compiled before running. The SPI0 model loads the slave automatically at init via `api_spi_init_bus`.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/spi_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `io.log` (emulation trace)

Expected behavior: Console prints "Test PASSED" and green LED lights.

### Step 4 -- Verify against hardware trace

> **Note:** Only SPI0 is passed to `-p`. Do NOT include passthrough-only peripherals (GPIO, GCR, UART, TMR, etc.) -- they would produce false mismatches.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p SPI0 \
  -mname SPI0 \
  -d SPI0:boardrunner/boardrunner_sdk/model/model.c \
  -d loopback:boardrunner/boardrunner_sdk/model/slave.c
```

#### Apply LLM correction patches (if mismatches found)

If the Verifier writes `fastdyn_work/revised_prompt.txt`, send it back to the LLM:

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Repeat Steps 3-4 until the Verifier reports no mismatches. Capped at 6 iterations for the ablation study.

Once verified, copy the final models to the example's generated_models directory:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/generated_models/spi0_model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/generated_models/loopback_slave.c
```

---

## Configuration

Platform configuration: [`spi_config.toml`](spi_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
