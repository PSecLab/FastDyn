# SPI — Transmit/Receive Byte (BME280, Full-Duplex)

**Board:** NUCLEO-H753ZI

The firmware uses SPI1 in master polling mode. After a user-button press it performs a 2-byte full-duplex transfer with a BME280, reading the chip ID register `0xD0` (expects `0x60`). Hardware NSS on PA4.

## Hardware Connections

**Sensor:** BME280 (SPI mode).

| BME280 pin | NUCLEO-H753ZI pin                   | Connector       | Notes                          |
| ---------- | ----------------------------------- | --------------- | ------------------------------ |
| VCC / VDD  | 3.3V                                | CN8 pin 7       | 3.3 V only                     |
| GND        | GND                                 | CN8 pin 11      |                                |
| SCK / SCL  | **D13** (PA5, SPI1_SCK, AF5)        | CN9 pin 2       |                                |
| SDO / MISO | **D12** (PA6, SPI1_MISO, AF5)       | CN9 pin 4       |                                |
| SDI / MOSI | **D11** (PB5, SPI1_MOSI, AF5)       | CN9 pin 6       |                                |
| CSB / CS   | **CN7 pin 17** (PA4, SPI1_NSS, AF5) | Morpho header   | **Not Arduino D10** — uses HW NSS |

> CS must go to PA4 (CN7 pin 17), not Arduino D10. The firmware uses `SPI_NSS_HARD_OUTPUT` so the SPI peripheral drives PA4 directly.

Press the user button (PC13) to start the transaction.

## Expected Output

- LED1 (PB0) on if the chip ID `0x60` was received correctly
- LED3 (PB14) on if the transfer failed or returned an unexpected value

## Test Scope

- Transmit a byte
- Receive a byte

## Compositional Model Split

| Component   | Peripheral                       | Compiled to     |
| ----------- | -------------------------------- | --------------- |
| Elder model | SPI1 (master + hardware NSS)     | `model.so`      |
| Elder slave | BME280                           | `slave.so`      |
| Passthrough | RCC, GPIO (button + LEDs), all others | real hardware |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set SPI1 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `spi_config.toml`. Restore elder mode before Step 5.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml
```

Press the user button when prompted by the LEDs.

Output: `hardware_log/io.log`

### Step 1 — Generate slave model prompt

```bash
boardrunner generate --slave-model \
  -b STM32H753x \
  -p SPI1 \
  -fc SPI_FIrmware_tmp/Src/main.c \
  -rm boardrunner/boardrunner_examples/examples/Nucleo-F103RB/SPI/generated_model/slave.c \
  -o ./fastdyn_work_bme280
```

### Step 2 — Compile slave model

```bash
boardrunner llm -d fastdyn_work_bme280 \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

> The generated slave must export `slave_spi_transfer` and `slave_spi_set_cs`.

### Step 3 — Generate master model prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p SPI1 \
  -mname "SPI1" -ms "SPI1" \
  -o ./fastdyn_work_spi
```

### Step 4 — Compile master model

```bash
boardrunner llm -d fastdyn_work_spi \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 5 — Run with the generated elder models

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml
```

### Step 6 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p SPI1 \
  -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c \
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
   boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/generated_models/spi_model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/generated_models/bme280_slave.c
```
