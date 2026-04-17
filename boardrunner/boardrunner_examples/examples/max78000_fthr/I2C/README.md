# I2C — Polling Read/Write Byte with BME280

**Firmware:** I2C1 master, polling mode with fixed delays. Initializes I2C1 at 100 kHz, then performs two transactions against a BME280 sensor at address 0x76: (TC1) writes the soft-reset command (0xB6) to register 0xE0, and (TC2) reads the chip ID from register 0xD0 (expects 0x60). LEDs indicate pass/fail.

**Board:** MAX78000FTHR

**Required model goal:**

1. Write a byte to a slave register
2. Read a byte from a slave register

---

## Hardware Connections

**Sensor:** BME280 (Bosch environmental sensor -- I2C mode. SDO/ADDR pin pulled low -> 7-bit address 0x76.)

| BME280 pin | MAX78000FTHR pin | Notes |
|-----------|------------------|-------|
| VCC / VDD | 3.3V | 3.3 V supply only |
| GND | GND | |
| SCK / SCL | **P0.16** (I2C1_SCL, AF1) | |
| SDA | **P0.17** (I2C1_SDA, AF1) | |
| SDO / ADDR | GND | Sets I2C address to 0x76 |
| CSB | VCC | Forces I2C mode (CSB high) |

> **Pull-up resistors required:** I2C1 runs in open-drain mode. Add 4.7 k pull-ups from SCL and SDA to 3.3V if your BME280 breakout board does not already include them.

**LED indicators:**

| LED | Meaning |
|-----|---------|
| Green LED | All tests PASSED |
| Red LED | Any failure -- check wiring or model |

---

## Firmware Check

The firmware achieves the required model goals:
- **TC1 Write:** Direct register access -- loads slave address (0xEC) into FIFO, issues START via MSTCTRL, then loads register address (0xE0) and data (0xB6) into FIFO, issues STOP
- **TC2 Read:** Two separate transactions (no repeated START). Transaction 1 writes register address (0xD0). Transaction 2 reads 1 byte (chip ID 0x60) from RX FIFO
- Uses **polling mode with fixed delays** (`MXC_Delay`) -- no DMA, no I2C interrupts
- No CS line -- I2C uses address-based device selection (7-bit address 0x76)

---

## Compositional Model Split

Two models are required. I2C uses address-based selection -- no GPIO CS model is needed.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | I2C1 (master controller) | `generated_model/i2c1_model.c` | `model.so` |
| Slave model | BME280 sensor | `generated_model/slave.c` | `slave.so` |
| Passthrough | GPIO0 (I2C pins), GPIO2 (LEDs), GCR, UART (printf), TMR (delay), all others | -- | real hardware |

**Slave calling convention:** The framework loads `slave.so` and resolves symbols by appending `_send`, `_receive`, and `_event` to the device name declared in the config (`device = "bme280"`). The slave must export exactly `bme280_send`, `bme280_receive`, and `bme280_event`.

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

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the I2C1 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `i2c_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/i2c_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 -- Generate BME280 I2C slave model

Generate the slave model **first** -- the I2C1 master model depends on it at runtime (the slave is loaded by `api_i2c_init_bus` inside the I2C1 model).

> **Why slave models are not generated from traces:** BoardRunner's HITL trace captures MMIO accesses to on-chip MAX78000 peripherals only. The BME280 is an external device on the I2C bus -- its internal register behavior never appears in `io.log`. Slave models are written once per device type and reused across firmwares.

Two inputs drive slave model generation:
- `--firmware-code (-fc)`: the firmware's `main.c` -- shows what registers are accessed (0xD0 chip ID, 0xE0 reset) and what values are expected (0x60 chip ID, 0xB6 reset command)
- `--reference-model (-rm)`: an existing I2C slave `.c` -- provides the structural pattern (event handler, send/receive callbacks, register file)

```bash
fastdyn generate --slave-model \
  -b Max78000 \
  -p I2C1 \
  -fc /scratch/Fastdyn/saved_maxim_examples/Examples/MAX78000/I2C_Sensor/main.c \
  -rm boardrunner/boardrunner_examples/examples/Nucleo-F103RB/I2C/generated_model/slave.c \
  -o ./fastdyn_work_bme280_i2c
```

Send the generated slave prompt to the LLM and compile:

```bash
fastdyn llm -d fastdyn_work_bme280_i2c \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

> The generated slave must export `bme280_send(uint8_t data)`, `bme280_receive(void)`, and `bme280_event(enum i2c_event event)` -- the framework resolves these symbols via `dlsym` when loading `slave.so`.

### Step 2 -- Generate LLM prompt for I2C1 master model

The Encoder processes `io.log` for I2C1. The LLM models the full I2C transaction state machine: START -> address phase -> register byte -> data bytes -> STOP, using `api_i2c_start_transfer`, `api_i2c_send`, `api_i2c_recv`, and `api_i2c_end_transfer`.

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p I2C1 \
  -mname "I2C1" -ms "I2C1" \
  -o ./fastdyn_work_i2c
```

### Step 3 -- Synthesize and compile the I2C1 master model

```bash
fastdyn llm -d fastdyn_work_i2c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 4 -- Run with the generated elder model

The TOML is already shipped in elder mode (elder `enabled = true`, passthrough `enabled = false` for I2C1). Run directly:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/i2c_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `io.log` (emulation trace)

Expected behavior: Console prints test results. Green LED = all tests passed. Red LED = failure.

### Step 5 -- Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p I2C1 \
  -mname I2C1 \
  -d I2C1:boardrunner/boardrunner_sdk/model/model.c \
  -d BME280:boardrunner/boardrunner_sdk/model/slave.c
```

`-d BME280:...` provides the slave source as context for the LLM correction prompt so it can inspect the full transaction when generating patches.

> **Note:** Do not add `BME280` or `passthrough_space` to the `-p` flag -- only peripherals with an actual elder model are verified. Passthrough-handled peripherals produce false MISMATCHes.

#### Apply LLM correction patches (if mismatches found)

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Repeat Steps 4-5 until the Verifier reports no mismatches. I2C typically converges in 4-7 iterations due to the multi-phase transaction state machine.

Once verified, copy the final models to the example's generated_model directory:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/generated_model/i2c1_model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/generated_model/slave.c
```

---

## Configuration

Platform configuration: [`i2c_config.toml`](i2c_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
