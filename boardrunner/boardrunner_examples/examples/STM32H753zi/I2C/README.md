# I2C — Polling Read/Write with BME280

**Firmware:** I2C1 master polling mode. Waits for a button press, scans the bus for a BME280 at address 0x76 (fallback 0x77), then performs two transactions: (TC1) writes the soft-reset command (0xB6) to register 0xE0, and (TC2) reads the chip ID from register 0xD0 (expects 0x60). LEDs indicate pass/fail.

**Board:** NUCLEO-H753ZI (STM32H753ZI)

**Model goal:** Write a byte to a slave register; read a byte from a slave register — both over I2C in polling mode with correct START/STOP framing.

---

## Hardware Connections

**Sensor:** BME280 (Bosch environmental sensor — I2C mode. SDO/ADDR pin pulled low → 7-bit address 0x76.)

| BME280 pin | NUCLEO-H753ZI pin | Connector | Notes |
|-----------|-------------------|-----------|-------|
| VCC / VDD | 3.3V | CN8 pin 7 or CN6 pin 4 | 3.3 V supply only |
| GND | GND | CN8 pin 11 or CN6 pin 6 | |
| SCK / SCL | **D15** (PB8, I2C1_SCL, AF4) | CN9 pin 3 | Arduino header |
| SDA | **D14** (PB9, I2C1_SDA, AF4) | CN9 pin 5 | Arduino header |
| SDO / ADDR | GND | — | Sets I2C address to 0x76 |
| CSB | VCC | — | Forces I2C mode (CSB high) |

> **Pull-up resistors required:** I2C1 runs in open-drain mode. Add 4.7 kΩ pull-ups from SCL and SDA to 3.3V if your BME280 breakout board does not already include them.

**User interaction:** Press the **blue button (PC13)** on the NUCLEO board after power-on to trigger the I2C transactions.

**LED indicators (on-board):**
| LED | Pin | Meaning |
|-----|-----|---------|
| LED1 (green) | PB0 | Waiting for button (on) / TC1 write PASS (stays on) |
| LED2 (yellow) | PE1 | TC2 read PASS (chip ID == 0x60) |
| LED3 (red) | PB14 | Any failure — check wiring or model |

---

## Firmware Check

The firmware achieves the required model goals:
- **TC1 Write:** `HAL_I2C_Mem_Write` → sends START, address+W (0xEC), register 0xE0, data 0xB6, STOP
- **TC2 Read:** `HAL_I2C_Mem_Read` → sends START, address+W (0xEC), register 0xD0, repeated START, address+R (0xED), receives 0x60, STOP
- Uses **polling mode** — no DMA, no I2C interrupts
- No CS line — I2C uses address-based device selection (address 0x76, HAL 8-bit form 0xEC)

---

## Model Split

Two models are required. I2C uses address-based selection — no GPIO CS model is needed.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | I2C1 (master controller) | `generated_models/model.c` | `model.so` |
| Slave model | BME280 sensor | `generated_models/slave.c` | `slave.so` |
| Passthrough | GPIOB (PB8/SCL, PB9/SDA, PB0/LED1, PB14/LED3), GPIOE (PE1/LED2), GPIOC (PC13/button), RCC, PWR, SYSCFG | — | real hardware |

**Slave calling convention:** The framework loads `slave.so` and resolves symbols by appending `_send`, `_receive`, and `_event` to the device name declared in the config (`device = "bme280"`). The slave must export exactly `bme280_send`, `bme280_receive`, and `bme280_event`.

> **Note:** During the emulation run, GPIOC PC13 (user button) is in passthrough. The firmware waits for a button press before starting the I2C transactions. Press the physical button on the board to proceed, or use a PTY-based GPIOC model (similar to the GPIO_INT example) to simulate it in software.

---

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **Config state for this step:** The TOML ships with the elder backend enabled (for the elder run in Step 4). Before running the passthrough collection, set the `[Device.i2c1]` elder handler to `enabled = false` and the passthrough handler to `enabled = true`. Restore to elder mode before Step 4.

Run the firmware in passthrough mode with real hardware. FastDyn logs every I2C1 register access to `hardware_log/io.log`.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/i2c_config.toml
```

Press the blue user button on the NUCLEO board when LED1 lights up to trigger the I2C transactions.

### Step 1 — Generate BME280 I2C slave model

Generate the slave model **first** — the I2C1 model depends on it at runtime (the slave is loaded by `api_i2c_init_bus` inside the I2C1 model).

> **Why slave models are not generated from traces:** BoardRunner's HITL trace captures MMIO accesses to on-chip STM32 peripherals only. The BME280 is an external device on the I2C bus — its internal register behavior never appears in `io.log`. Slave models are written once per device type and reused across firmwares.

Two inputs drive slave model generation:
- `--firmware-code (-fc)`: the master's `main.c` — shows what registers are accessed (0xD0 chip ID, 0xE0 reset) and what values are expected (0x60 chip ID, 0xB6 reset command)
- `--reference-model (-rm)`: an existing I2C slave `.c` — provides the structural pattern (event handler, send/receive callbacks, register file)

```bash
fastdyn generate --slave-model \
  -b STM32H753x \
  -p I2C1 \
  -fc I2C_Firmwrae_Src_tmp/Src/main.c \
  -rm boardrunner/boardrunner_examples/examples/Nucleo-F103RB/I2C/generated_model/slave.c \
  -o ./fastdyn_work_bme280_i2c
```

Send the generated slave prompt to the LLM and compile:

```bash
fastdyn llm -d fastdyn_work_bme280_i2c \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

> The generated slave must export `bme280_send(uint8_t data)`, `bme280_receive(void)`, and `bme280_event(enum i2c_event event)` — the framework resolves these fixed symbols via `dlsym` when loading `slave.so`.

### Step 2 — Generate LLM prompt for I2C1 master model

The Encoder processes `io.log` for I2C1. The LLM models the full I2C transaction state machine: START → address phase → register byte → data bytes → STOP, using `api_i2c_start_transfer`, `api_i2c_send`, `api_i2c_recv`, and `api_i2c_end_transfer`.

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p I2C1 \
  -mname "I2C1" -ms "I2C1" \
  -o ./fastdyn_work_i2c
```

### Step 3 — Synthesize and compile the I2C1 model

```bash
fastdyn llm -d fastdyn_work_i2c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 4 — Run with the elder backend

The TOML is already configured with the elder handler enabled and passthrough disabled for `[Device.i2c1]`. Run:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/i2c_config.toml
```

Press the blue button. LED1 + LED2 solid = all tests passed; LED3 = failure.

Output: `io.log` (emulation trace for verification).

### Step 5 — Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p I2C1 \
  -mname I2C1 \
  -d I2C1:boardrunner/boardrunner_sdk/model/model.c \
  -d BME280:boardrunner/boardrunner_sdk/model/slave.c
```

`-d BME280:...` provides the slave source as context for the LLM correction prompt so it can inspect the full transaction when generating patches.

> **Note:** Do not add `BME280` or `remaining_space` to the `-p` flag — only peripherals with an actual elder model are verified. Passthrough-handled peripherals produce false MISMATCHes.

#### Apply LLM correction patches (if verification finds mismatches)

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Repeat Steps 4–5 until the Verifier reports no mismatches. I2C typically converges in 4–7 iterations due to the multi-phase transaction state machine.

---

## Configuration

Platform configuration: [`i2c_config.toml`](i2c_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
