# SPI Ablation — NUCLEO-H753ZI

**Peripheral:** SPI1 (base `0x40013000`, NVIC IRQn 35)
**Firmware:** `boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/firmwares/SPI_Firmware.elf` — waits for blue button (PC13), performs a 2-byte full-duplex `HAL_SPI_TransmitReceive` against a BME280 (sends `{0xD0, 0x00}`, expects `0x60`). Hardware NSS on PA4 (`SPI_NSS_HARD_OUTPUT`). LED1 (PB0) on success, LED3 (PB14) on failure.
**TOML:** `boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml`
**Slave:** BME280 sensor model is fixed across all cells (`slave.so`); only the SPI1 master `model.so` is regenerated. No `BME280:` entry on the verifier `-d` line.

## Hardware

| BME280 pin | NUCLEO pin | Connector |
|---|---|---|
| VCC | 3.3 V | CN8-7 or CN6-4 |
| GND | GND | CN8-11 or CN6-6 |
| SCK | PA5 (D13) | CN9-2 (Arduino) |
| MISO | PA6 (D12) | CN9-4 (Arduino) |
| MOSI | PB5 (D11) | CN9-6 (Arduino) |
| CS / NSS | PA4 | **CN7-17 (Morpho)** — *not* Arduino D10 |

## Results

| Variant | Iters | Tokens | Latency (s) | Result |
|---|---|---|---|---|
| `full_medium` | 2 | 40,674 | 361.2 | PASS |
| `ablation_no_encoder` | 1 | 145,178 | 165.6 | PASS |
| `ablation_no_rca` | 1 | 19,181 | 194.3 | PASS |
| `ablation_no_vio` | 2 | 33,027 | 301.0 | PASS (oracles); chip ID `0x60` hardcoded in master at lines 241–247 of `generated_model.c` (memorization detected by source inspection — see paper) |
| `ablation_no_verifier` | 1 (single-shot) | 16,281 | 160.1 | FAIL (reused from `full_medium/001_*`; iter-1 model has 3 unfixed bugs) |

## Prerequisites

```bash
# Build the slave (BME280 register model — fixed across all cells)
cp boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/generated_models/bme280_slave.c \
   boardrunner/boardrunner_sdk/model/slave.c
( cd boardrunner/boardrunner_sdk && cmake --build build -j slave )

# Flash firmware
tests/idle_firmwares/flash_scripts/flash.sh \
  boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/firmwares/SPI_Firmware.elf

# For Step 0 only: also flash the idle firmware
tests/idle_firmwares/flash_scripts/flash.sh tests/idle_firmwares/prebuilt/idlefirmware_h7.axf
```

Edit `spi_config.toml` to switch `[Device.spi1]` between `passthrough` (Step 0) and `elder`.

## Step 0 — Passthrough trace

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml
```

Press the blue button; LED1 lights when the BME280 returns `0x60`. Ctrl-C. Output: `hardware_log/io.log`. Flip TOML to elder mode and re-flash the SPI firmware before continuing.

## `full_medium/`

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p SPI1 -mname SPI1 -ms SPI1 \
  -o ./fastdyn_work_spi

fastdyn llm -d fastdyn_work_spi \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml

fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p SPI1 -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c

# Correction loop (repeat run + verifier + llm until verifier passes or 10-call cap)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

## A1: `ablation_no_encoder/`

```bash
fastdyn generate --no-encoder -hw hardware_log/io.log -b STM32H753x \
  -p SPI1 -mname SPI1 -ms SPI1 \
  -o ./fastdyn_work_spi_a1

fastdyn llm -d fastdyn_work_spi_a1 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml

fastdyn verifier --no-encoder -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p SPI1 -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c
```

## A2: `ablation_no_rca/`

Seed the broken iter-1 model from `full_medium/`, then run the correction loop with `--no-rca`. The seed (`used_failure_model.c`) is the iter-1 model extracted from `full_medium/001_response_1.txt`.

```bash
# Stage seed at SDK build target
cp boardrunner/boardrunner_examples/examples/STM32H753zi/ablation_artifacts/SPI/ablation_no_rca/used_failure_model.c \
   boardrunner/boardrunner_sdk/model/model.c
( cd boardrunner/boardrunner_sdk && cmake --build build -j model )

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml

fastdyn verifier --no-rca -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p SPI1 -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c

fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate

# Repeat run + verifier + llm until verifier passes or 10-call cap.
```

## A3: `ablation_no_vio/`

```bash
fastdyn generate --no-vio -hw hardware_log/io.log -b STM32H753x \
  -p SPI1 -mname SPI1 -ms SPI1 \
  -o ./fastdyn_work_spi_a3

fastdyn llm -d fastdyn_work_spi_a3 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml

fastdyn verifier --no-vio -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p SPI1 -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c
```

## A4: `ablation_no_verifier/`

Single-shot: A4 = the iter-1 LLM call from `full_medium/`. The cell's `001_*` files and `metrics.jsonl` are direct copies of the corresponding `full_medium/` artifacts. The pass/fail is determined by running the iter-1 model once on hardware:

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p SPI1 -mname SPI1 -ms SPI1 \
  -o ./fastdyn_work_spi_a4

fastdyn llm -d fastdyn_work_spi_a4 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/SPI/spi_config.toml
fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p SPI1 -mname SPI1 \
  -d SPI1:boardrunner/boardrunner_sdk/model/model.c
```

## Pitfalls

- CS must be wired to PA4 on Morpho CN7-17, not Arduino D10. Wrong CS pin returns `0xFF` for every byte.
- The ablation flag must be passed on **every** `fastdyn verifier` call.
- Pre-build `slave.so` once before any cell runs; do not rebuild it between cells.
- `qemu_plugin_raise_irq` takes `NVIC IRQn + 16` (SPI1 → 51).
