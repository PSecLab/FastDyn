# SPI — Transmit/Receive Byte (Loopback)

**Board:** MAX78000FTHR

The firmware uses SPI0 in master polling mode to transmit 16 bytes of `0xA5` and receive them back over an external MOSI-to-MISO loopback wire. PASS if all 16 received bytes match.

## Hardware Connections

| Connection | Pin                | Header     | Notes              |
| ---------- | ------------------ | ---------- | ------------------ |
| MOSI       | P0.5               | J8 pin 12  | Wire to MISO       |
| MISO       | P0.6               | J8 pin 13  | Wire from MOSI     |

Connect MOSI to MISO with a jumper wire to create the loopback path.

## Expected Output

- Console prints transmitted and received byte sequence, then `PASSED`
- Green LED on

## Test Scope

- Transmit a byte
- Receive a byte

## Compositional Model Split

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | SPI0 (master)          | `model.so`      |
| Elder slave | loopback (CS 0)        | `slave.so`      |
| Passthrough | UART, GCR, GPIO, all others | real hardware |

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

> **TOML state:** Set SPI0 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `spi_config.toml`. Restore elder mode before Step 5.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/spi_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 — Generate slave model prompt

```bash
fastdyn generate --slave-model \
  -b Max78000 \
  -p SPI0 \
  -fc ../saved_maxim_examples/Examples/MAX78000/SPI/main.c \
  -rm boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/generated_models/bme280_slave.c \
  -o ./fastdyn_work_loopback
```

### Step 2 — Compile slave model

```bash
fastdyn llm -d fastdyn_work_loopback \
  -o boardrunner/boardrunner_sdk/model/slave.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Generate master model prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p SPI0 \
  -mname "SPI0" -ms "SPI0" \
  -o ./fastdyn_work_spi0
```

### Step 4 — Compile master model

```bash
fastdyn llm -d fastdyn_work_spi0 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 5 — Run with the generated elder models

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/spi_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

### Step 6 — Verify against hardware trace

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

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Once verified, snapshot both models:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/generated_models/spi0_model.c

cp boardrunner/boardrunner_sdk/model/slave.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/SPI/generated_models/loopback_slave.c
```
