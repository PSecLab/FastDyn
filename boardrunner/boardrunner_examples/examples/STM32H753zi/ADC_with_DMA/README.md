# ADC with DMA

**Firmware:** Continuous ADC sampling using DMA to transfer conversion results directly to RAM. Exercises ADC1/ADC2 in dual-mode with DMA1/DMAMUX1, including transfer-complete interrupts.

**Board:** NUCLEO-H753ZI (STM32H753ZI)

---

## Compositional Model Split

Two independently generated VIO models cover the DMA subsystem and the ADC subsystem. Because DMA and ADC have a tight dependency (the DMA destination buffer pointer is written via ADC MMIO registers), the prompt generation step passes all relevant peripheral names to both model contexts so the LLM understands the cross-peripheral relationship.

| Component | Peripherals | Generated file | Compiled to |
|-----------|------------|----------------|-------------|
| Model 1 | DMA1 + DMAMUX1 | `generated_models/dma_model.c` | `model.so` |
| Model 2 | ADC1 + ADC2 + ADC12_Common | `generated_models/adc_model.c` | `model2.so` |
| Passthrough | RCC, PWR, SYSCFG, GPIO, … | — | real hardware |

---

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

Run the firmware in passthrough mode. FastDyn records all peripheral MMIO accesses to `hardware_log/io.log`. For DMA-heavy workloads BoardRunner also uses Synchronized Memory to keep the emulated RAM and hardware RAM coherent so DMA transfers replicate correctly.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
```

The `adc_with_dma.toml` configuration controls which peripherals use elder, passthrough, or other backends. Switch a device's `enabled` flag to change its backend without touching any other file.

### Step 1 — Generate LLM prompts

BoardRunner-Learner Encoder distills `io.log` into a compact timed automaton per peripheral group, annotates each MMIO address with its symbolic name (via the STM32H7 SVD), identifies DMA buffer pointers, and produces a structured prompt.

```bash
# Model 1: DMA1 + DMAMUX1
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname "DMA_with_DMAMUX1" -ms "DMA1" -ms "DMAMUX1" \
  -o ./fastdyn_work_dma

# Model 2: ADC1 + ADC2 + ADC12_Common
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname "ADC" -ms "ADC1" -ms "ADC2" -ms "ADC12_Common" \
  -o ./fastdyn_work_adc
```

> Both commands include all five peripheral names (`-p`) so the Encoder captures cross-peripheral interactions in the trace. The `-ms` flags select which peripherals *this specific model* is responsible for emulating.

### Step 2 — Send prompts to LLM and compile models

CodeGen sends the encoded automaton to the LLM. The LLM selects the appropriate FastDyn VIO APIs (including DMA buffer APIs for the DMA model) and synthesizes a C device model, which is compiled to a `.so` shared library.

```bash
# Generate and compile Model 1 — DMA (→ dma_model.c → model.so)
fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium

# Generate and compile Model 2 — ADC (→ adc_model.c → model2.so)
fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with generated elder models

Rerun the firmware with FastDyn loading the learned DMA and ADC models. The remaining peripherals continue in passthrough mode.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
```

### Step 4 — Verify emulated models against hardware trace

The Verifier replays `io.log` through both models, compares register-level responses against the hardware ground truth, and reports any mismatches. Entropy analysis flags high-entropy registers (sensor data, DMA counters) that are data-dependent and are not expected to match exactly.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC \
  -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c
```

#### Apply LLM correction patches (if verification finds mismatches)

The Verifier writes a unified `fastdyn_work/revised_prompt.txt` describing all detected mismatches. Send it to the LLM to get SEARCH/REPLACE patches applied atomically to both model files.

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/dma_model.c \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/generated_models/adc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Repeat Steps 3–4 until the Verifier reports no mismatches.

---

## Single-Peripheral Reference (GPIOB)

For a simpler single-peripheral baseline (e.g. to test just a GPIO output), the workflow reduces to three commands:

```bash
# 1. Collect trace
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO/gpio_config.toml

# 2. Generate prompt and model
fastdyn generate -hw hardware_log/io.log -b STM32H753x -p GPIOB -o ./fastdyn_work_gpio
fastdyn llm -d fastdyn_work_gpio -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless

# 3. Verify
fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p GPIOB -mname "GPIOB" \
  -d GPIOB:boardrunner/boardrunner_sdk/model/model.c
```

---

## Configuration

Platform configuration: [`adc_with_dma.toml`](adc_with_dma.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
