# I2C — Read/Write Byte to Slave (BME280)

**Board:** MAX78000FTHR

The firmware uses I2C1 in master polling mode to talk to a BME280 sensor at address `0x76`. It writes the soft-reset command (`0xB6`) to register `0xE0`, then reads the chip ID from register `0xD0` (expects `0x60`).

## Hardware Connections

**Sensor:** BME280 (I2C mode; SDO/ADDR pin tied low → 7-bit address `0x76`).

| BME280 pin | MAX78000FTHR pin            | Notes                    |
| ---------- | --------------------------- | ------------------------ |
| VCC / VDD  | 3.3V                        | 3.3 V supply only        |
| GND        | GND                         |                          |
| SCK / SCL  | **P0.16** (I2C1_SCL, AF1)   |                          |
| SDA        | **P0.17** (I2C1_SDA, AF1)   |                          |
| SDO / ADDR | GND                         | Sets I2C address to 0x76 |
| CSB        | VCC                         | Forces I2C mode          |

> Add 4.7 kΩ pull-ups from SCL and SDA to 3.3 V if your BME280 breakout doesn't already include them.

## Expected Output

- Console prints chip ID `0x60`, then `PASSED`
- Green LED on

## Test Scope

- Read a byte from a slave
- Write a byte to a slave

## Compositional Model Split

| Component   | Peripheral                 | Compiled to     |
| ----------- | -------------------------- | --------------- |
| Elder model | I2C1 (master)              | `model.so`      |
| Elder slave | BME280 (address 0x76)      | `slave.so`      |
| Passthrough | UART, GCR, GPIO, all others | real hardware  |

## Commands

### Start OpenOCD (separate terminal)

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

### Flash the idle firmware

```bash
boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/flash.sh \
  tests/idle_firmwares/prebuilt/idle_firmware_max78000.elf
```

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set I2C1 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `i2c_config.toml`. Restore elder mode before Step 5.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/i2c_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 — Generate slave model prompt

```bash
fastdyn generate --slave-model \
  -b Max78000 \
  -p I2C1 \
  -fc ../saved_maxim_examples/Examples/MAX78000/I2C_Sensor/main.c \
  -rm boardrunner/boardrunner_examples/examples/Nucleo-F103RB/I2C/generated_model/slave.c \
  -o ./fastdyn_work_bme280_i2c
```

### Step 2 — Compile slave model

```bash
fastdyn llm -d fastdyn_work_bme280_i2c \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Generate master model prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p I2C1 \
  -mname "I2C1" -ms "I2C1" \
  -o ./fastdyn_work_i2c
```

### Step 4 — Compile master model

```bash
fastdyn llm -d fastdyn_work_i2c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 5 — Run with the generated elder models

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/i2c_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

### Step 6 — Verify against hardware trace

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

#### Apply LLM correction patches (if mismatches found)

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Once verified, snapshot both models:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/generated_models/i2c1_model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/I2C/generated_models/bme280_slave.c
```
