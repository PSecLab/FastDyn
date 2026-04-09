# GPIO_INT — External Interrupt with Button and LED

**Firmware:** PC13 push button (NUCLEO blue button) triggers the EXTI15_10 interrupt. Each press cycles the blink rate of the onboard LEDs (LD1/LD2/LD3).

**Board:** NUCLEO-H753ZI (STM32H753ZI)

---

## Compositional Model Split

This example uses two independently generated VIO models connected through the BoardRunner signals API. All LED output pins are left in passthrough so they blink on the physical board without needing a model.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | GPIOC | `generated_models/gpioc_model.c` | `model.so` |
| Model 2 | EXTI | `generated_models/exti_model.c` | `model2.so` |
| Passthrough | SYSCFG, GPIOB (LED1/LED3), GPIOE (LED2), RCC, PWR, … | — | real hardware |

**Inter-model communication:** The GPIOC model publishes the PC13 pin level via `api_signal_set(13, level)`. The EXTI model subscribes via `api_signal_register(13, handler, opaque)`, performs rising-edge detection per the RTSR1 register configuration, and raises EXTI15_10_IRQn (IRQ 40) to the CPU via `qemu_plugin_raise_irq(56, false)`.

SYSCFG is intentionally left in passthrough — it handles I/O compensation (`SYSCFG_CCCSR`, `SYSCFG_PMCR`) and the EXTI routing init (`SYSCFG_EXTICR4`), which only needs to reach real hardware once and is not required by the EXTI model.

---

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`. This trace is the ground truth for all later steps.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/gpio_int_config.toml
```

### Step 1 — Generate LLM prompts

The `fastdyn generate` command (BoardRunner-Learner Encoder) processes `io.log`, extracts the initialization sequence and the repeating steady-state pattern for each peripheral, computes per-register Shannon entropy, identifies interrupt handlers, and writes a compact prompt file to the work directory.

```bash
# Model 1: GPIOC — PC13 button input, PTY, signal publisher
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p GPIOC \
  -mname "GPIOC" -ms "GPIOC" \
  -o ./fastdyn_work_gpioc

# Model 2: EXTI — interrupt controller (SYSCFG excluded, stays in passthrough)
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p EXTI \
  -mname "EXTI" -ms "EXTI" \
  -o ./fastdyn_work_exti
```

### Step 2 — Send prompts to LLM and compile models

BoardRunner-Learner CodeGen sends each prompt to the LLM, which synthesizes a C device model against the FastDyn VIO API. The result is compiled to a shared library (`.so`) that FastDyn loads at runtime.

```bash
# Generate and compile Model 1 — GPIOC (→ gpioc_model.c → model.so)
fastdyn llm -d fastdyn_work_gpioc \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium

# Generate and compile Model 2 — EXTI (→ exti_model.c → model2.so)
fastdyn llm -d fastdyn_work_exti \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with generated elder models

Rerun the firmware with FastDyn now using the learned models for GPIOC and EXTI. The LEDs are still driven through passthrough to real hardware, so you will see them blink on the board.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/gpio_int_config.toml
```

#### Simulate a button press

The GPIOC model exposes PC13 as a virtual PTY button. Writing `1` then `0` to the pipe simulates a full press-and-release. This generates a rising edge on EXTI line 13, fires EXTI15_10_IRQn, and the ISR cycles the LED blink rate — **you will see the physical LEDs change their blink pattern on the board**.

```bash
echo 1 > /tmp/gpioc_pc13; echo 0 > /tmp/gpioc_pc13
```

### Step 4 — Verify emulated models against hardware trace

The BoardRunner Verifier replays `io.log` through the emulated models and diffs register-level behaviour against the hardware ground truth. Mismatches are reported with entropy analysis and a correction prompt is written to `fastdyn_work/revised_prompt.txt`.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p GPIOC -p EXTI \
  -mname GPIOC \
  -mname EXTI \
  -d GPIOC:boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  -d EXTI:boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c
```

#### Apply LLM correction patches (if verification finds mismatches)

If the Verifier detects mismatches it writes a unified `revised_prompt.txt`. Send it to the LLM to get SEARCH/REPLACE patches, which are applied atomically to both model files before recompilation.

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Repeat Steps 3–4 until the Verifier reports no mismatches.

---

## Configuration

Platform configuration: [`gpio_int_config.toml`](gpio_int_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
