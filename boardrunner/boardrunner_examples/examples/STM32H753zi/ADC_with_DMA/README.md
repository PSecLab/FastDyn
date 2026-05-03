# ADC_with_DMA — Read ADC + DMA Peripheral-to-RAM Transfer

**Board:** NUCLEO-H753ZI

The firmware runs ADC1/ADC2 in dual mode with DMA1+DMAMUX1, streaming continuous conversions directly to a RAM buffer and handling the transfer-complete interrupt.

## Hardware Connections

None. Internal ADC channels (typically Vrefint/Vbat/temperature) used as inputs.

## Expected Output

- Pass-indicator LED on if both ADC and DMA reach a steady state and the transfer-complete callback fires

## Test Scope

- Read an analog-to-digital conversion
- Transfer from Peripheral MMIO register to RAM

## Compositional Model Split

Two peripheral models (no slave). The verifier loads both `.c` files via `-d`.

| Component   | Peripheral                       | Compiled to     |
| ----------- | -------------------------------- | --------------- |
| Elder model | DMA1 + DMAMUX1                   | `model.so`      |
| Elder model | ADC1 + ADC2 + ADC12_Common       | `model2.so`     |
| Passthrough | RCC, PWR, GPIO, all others       | real hardware   |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set both DMA and ADC elder handlers to `enabled = false` and passthrough handlers to `enabled = true` in `adc_with_dma.toml`. Restore elder mode before Step 3.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompts (one per model)

```bash
# DMA model
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname "DMA_with_DMAMUX1" -ms "DMA1" -ms "DMAMUX1" \
  -o ./fastdyn_work_dma

# ADC model
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname "ADC" -ms "ADC1" -ms "ADC2" -ms "ADC12_Common" \
  -o ./fastdyn_work_adc
```

### Step 2 — Send prompts to LLM and compile both models

```bash
# Model 1 — DMA → dma_model.c → model.so
boardrunner llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium

# Model 2 — ADC → adc_model.c → model2.so
boardrunner llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with the generated elder models

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
```

### Step 4 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC \
  -d ADC:boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c
```

#### Apply LLM correction patches (if mismatches found)

```bash
boardrunner llm -d fastdyn_work \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

The LLM `-o` writes the snapshots directly into `generated_models/` — no separate `cp` needed.
