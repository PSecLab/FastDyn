# ADC + DMA Ablation — NUCLEO-H753ZI

**Peripherals:** ADC1 (`0x40022000`), ADC2, ADC12_Common, DMA1 (`0x40020000`), DMAMUX1 (`0x40020800`).
**Firmware:** `boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/firmwares/ADC_DMA_Transfer.elf` — continuous ADC1/ADC2 dual-mode sampling, DMA1 streams conversion results to RAM, transfer-complete IRQs.
**TOML:** `boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml`

## Compositional model split

The workload is rehosted by **two independently generated models** evaluated jointly. Either model failing fails the whole run.

| Model | `-mname` | `-ms` peripherals | LLM `-o` / verifier `-d` | Compiled to | TOML block |
|---|---|---|---|---|---|
| 1 (DMA) | `DMA_with_DMAMUX1` | `DMA1`, `DMAMUX1` | `boardrunner_sdk/model/model2.c` | `model2.so` | `[Device.dma_with_dmamux1]` |
| 2 (ADC) | `ADC` | `ADC1`, `ADC2`, `ADC12_Common` | `boardrunner_sdk/model/model.c` | `model.so` | `[Device.adc]` |

Both prompts pass all five peripheral names with `-p` so the Encoder captures the cross-peripheral coupling (ADC writes the DMA destination pointer through ADC MMIO).

**Iters = max across the two models. Tokens = sum across the two models.**

## Results

| Variant | Iters (max) | Tokens (sum) | Latency (s) | Result |
|---|---|---|---|---|
| `full_medium` | 7 | 221,101 | 1,287.6 | PASS |
| `ablation_no_encoder` | 2 | 1,066,491 | 588.0 | FAIL — joint revision prompt at iter 3 (`1,095,515` tokens) exceeds the 922,000-token API limit; correction loop cannot start |
| `ablation_no_rca` | 10 | 257,672 | 1,640.5 | FAIL — 10-call cap; final state has ADC1, ADC2 mismatched |
| `ablation_no_vio` | 10 | 282,914 | 1,828.0 | FAIL — 10-call cap; all 5 peripherals mismatched in final state |
| `ablation_no_verifier` | — (single-shot) | 55,724 | 538.8 | FAIL — reused from `full_medium/` iter 1 + iter 2 |

## Prerequisites

```bash
# Flash firmware
tests/idle_firmwares/flash_scripts/flash.sh \
  boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/firmwares/ADC_DMA_Transfer.elf

# For Step 0 only: also flash the idle firmware
tests/idle_firmwares/flash_scripts/flash.sh tests/idle_firmwares/prebuilt/idlefirmware_h7.axf
```

The TOML declares synchronized memory backends (`/dev/shm/my_m4_ram3`, `/dev/shm/my_m4_ram`) — required for DMA RAM writes during emulation. Switch both `[Device.dma_with_dmamux1]` and `[Device.adc]` blocks together between `passthrough` (Step 0) and `elder` (everything else).

## Step 0 — Passthrough trace

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
```

Let the firmware run long enough to capture multiple DMA transfer-complete cycles. Ctrl-C. Output: `hardware_log/io.log`. Flip both TOML blocks to elder mode and re-flash the ADC firmware before continuing.

## `full_medium/`

```bash
# Generate Model 1 (DMA)
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname DMA_with_DMAMUX1 -ms DMA1 -ms DMAMUX1 \
  -o ./fastdyn_work_dma

fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

# Generate Model 2 (ADC)
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -ms ADC1 -ms ADC2 -ms ADC12_Common \
  -o ./fastdyn_work_adc

fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

# Joint elder run + verifier + correction
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml

fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_sdk/model/model2.c

fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate

# Repeat run + verifier + llm until verifier passes or 10-call cap.
```

## A1: `ablation_no_encoder/`

Same workflow as baseline with `--no-encoder` on every `fastdyn generate` and every `fastdyn verifier`. On this workload the joint revision prompt exceeds the API context limit — the loop is expected to terminate at the first correction call.

```bash
fastdyn generate --no-encoder -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname DMA_with_DMAMUX1 -ms DMA1 -ms DMAMUX1 \
  -o ./fastdyn_work_dma

fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn generate --no-encoder -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -ms ADC1 -ms ADC2 -ms ADC12_Common \
  -o ./fastdyn_work_adc

fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml

fastdyn verifier --no-encoder -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_sdk/model/model2.c

# This call is expected to fail with context_length_exceeded on this workload
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

## A2: `ablation_no_rca/`

Seed both broken iter-1 models from `full_medium/`, then run the correction loop with `--no-rca` and `--ms-map`. With `--ms-map` the verifier writes one independent correction prompt per `-mname` under `fastdyn_work_<mname>/`, so each LLM call sees only its own model's source and only its peripherals' diffs.

```bash
# Stage seeds at SDK build targets
cp boardrunner/boardrunner_examples/examples/STM32H753zi/ablation_artifacts/ADC_with_DMA/ablation_no_rca/dma_used_failure_model.c \
   boardrunner/boardrunner_sdk/model/model2.c
cp boardrunner/boardrunner_examples/examples/STM32H753zi/ablation_artifacts/ADC_with_DMA/ablation_no_rca/adc_used_failure_model.c \
   boardrunner/boardrunner_sdk/model/model.c
( cd boardrunner/boardrunner_sdk && cmake --build build -j )

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml

fastdyn verifier --no-rca -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_sdk/model/model2.c \
  --ms-map "ADC=ADC1,ADC2,ADC12_Common" \
  --ms-map "DMA_with_DMAMUX1=DMA1,DMAMUX1"

# Per-model corrections (skip a model if its prompt was not written this iter)
fastdyn llm -d fastdyn_work_ADC \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate

fastdyn llm -d fastdyn_work_DMA_with_DMAMUX1 \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate

# Repeat run + verifier + per-model llm until verifier passes or 10-call cap.
```

Per-iter cost = sum of the two `fastdyn llm` calls.

## A3: `ablation_no_vio/`

```bash
fastdyn generate --no-vio -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname DMA_with_DMAMUX1 -ms DMA1 -ms DMAMUX1 \
  -o ./fastdyn_work_dma

fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn generate --no-vio -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -ms ADC1 -ms ADC2 -ms ADC12_Common \
  -o ./fastdyn_work_adc

fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml

fastdyn verifier --no-vio -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_sdk/model/model2.c

fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

## A4: `ablation_no_verifier/`

Single-shot: A4 = the two initial LLM calls from `full_medium/` (iter 1 + iter 2). The cell's prompts, responses, and metrics are direct copies of those baseline artifacts. The pass/fail is determined by running both iter-1/iter-2 models once on hardware:

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p DMA1 -p DMAMUX1 -p ADC1 -p ADC2 -p ADC12_Common \
  -mname DMA_with_DMAMUX1 -ms DMA1 -ms DMAMUX1 \
  -o ./fastdyn_work_dma

fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_sdk/model/model2.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -ms ADC1 -ms ADC2 -ms ADC12_Common \
  -o ./fastdyn_work_adc

fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/ADC_with_DMA/adc_with_dma.toml
fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p ADC1 -p ADC2 -p ADC12_Common -p DMA1 -p DMAMUX1 \
  -mname ADC -mname DMA_with_DMAMUX1 \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c \
  -d DMA_with_DMAMUX1:boardrunner/boardrunner_sdk/model/model2.c
```

## Pitfalls

- The ablation flag (`--no-encoder` / `--no-rca` / `--no-vio`) must be passed on **every** `fastdyn verifier` call, not just iter 1.
- The verifier `-d` path determines the prompt's `// FILE:` header; the LLM patch router matches by basename. Keep `-o` and `-d` paths matched (always `boardrunner_sdk/model/{model.c,model2.c}`).
- Both TOML blocks (`Device.dma_with_dmamux1`, `Device.adc`) must be in the same mode. Mixed modes break Synchronized Memory invariants.
- Re-flash the ADC firmware before every elder-mode `fastdyn run`.
- Iter count is the **max** across the two models, not the sum.
