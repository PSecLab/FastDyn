# I2C — Read/Write Byte to Slave (BME280)

**Board:** NUCLEO-H753ZI

The firmware uses I2C1 in master polling mode. After a user-button press it scans the bus for a BME280 at address `0x76`, writes the soft-reset command (`0xB6`) to register `0xE0`, then reads the chip ID from register `0xD0` (expects `0x60`).

## Hardware Connections

**Sensor:** BME280 (I2C mode; SDO/ADDR pin tied low → 7-bit address `0x76`).

| BME280 pin | NUCLEO-H753ZI pin                | Connector | Notes                    |
| ---------- | -------------------------------- | --------- | ------------------------ |
| VCC / VDD  | 3.3V                             | CN8 pin 7 | 3.3 V supply only        |
| GND        | GND                              | CN8 pin 11 |                         |
| SCK / SCL  | **D15** (PB8, I2C1_SCL, AF4)     | CN9 pin 3 |                          |
| SDA        | **D14** (PB9, I2C1_SDA, AF4)     | CN9 pin 5 |                          |
| SDO / ADDR | GND                              | —         | Sets I2C address to 0x76 |
| CSB        | VCC                              | —         | Forces I2C mode          |

> Add 4.7 kΩ pull-ups from SCL and SDA to 3.3 V if your BME280 breakout doesn't already include them.

Press the user button (PC13) to start the test.

## Expected Output

- LED1 (PB0) on after the soft-reset write succeeds
- LED2 (PE1) on after the chip ID read returns `0x60`
- LED3 (PB14) on for any failure

## Test Scope

- Read a byte from a slave
- Write a byte to a slave

## Compositional Model Split

| Component   | Peripheral                       | Compiled to     |
| ----------- | -------------------------------- | --------------- |
| Elder model | I2C1 (master)                    | `model.so`      |
| Elder slave | BME280 (address 0x76)            | `slave.so`      |
| Passthrough | RCC, GPIO (button + LEDs), all others | real hardware |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set I2C1 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `i2c_config.toml`. Restore elder mode before Step 5.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/i2c_config.toml
```

Press the user button when prompted by the LEDs.

Output: `hardware_log/io.log`

### Step 1 — Generate slave model prompt

```bash
boardrunner generate --slave-model \
  -b STM32H753x \
  -p I2C1 \
  -fc I2C_Firmwrae_Src_tmp/Src/main.c \
  -rm boardrunner/boardrunner_examples/examples/Nucleo-F103RB/I2C/generated_model/slave.c \
  -o ./fastdyn_work_bme280_i2c
```

### Step 2 — Compile slave model

```bash
boardrunner llm -d fastdyn_work_bme280_i2c \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

> The generated slave must export `bme280_send(uint8_t data)`, `bme280_receive(void)`, and `bme280_event(enum i2c_event event)`.

### Step 3 — Generate master model prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p I2C1 \
  -mname "I2C1" -ms "I2C1" \
  -o ./fastdyn_work_i2c
```

### Step 4 — Compile master model

```bash
boardrunner llm -d fastdyn_work_i2c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 5 — Run with the generated elder models

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/i2c_config.toml
```

### Step 6 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p I2C1 \
  -mname I2C1 \
  -d I2C1:boardrunner/boardrunner_sdk/model/model.c \
  -d BME280:boardrunner/boardrunner_sdk/model/slave.c
```

#### Apply LLM correction patches (if mismatches found)

```bash
boardrunner llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Once verified, snapshot both models:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/generated_models/model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/I2C/generated_models/slave.c
```
