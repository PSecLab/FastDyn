# GPIO_INT — Execute Callback After Pin Interrupt

**Board:** NUCLEO-H753ZI

The firmware uses the user button (PC13) as a falling-edge external interrupt (EXTI15_10). Each press cycles the blink rate of the on-board LEDs.

## Hardware Connections

- **User button** (blue, PC13) — press to fire EXTI interrupt
- **LEDs** (LD1/LD2/LD3) — blink rate cycles per button press

## Expected Output

- LED blink pattern changes each time the user button is pressed

## Test Scope

- Execute callback after pin interrupt

## Compositional Model Split

Two peripheral models (no slave). The verifier loads both `.c` files via `-d`.

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | GPIOC (input + button) | `model.so`      |
| Elder model | EXTI                   | `model2.so`     |
| Passthrough | GPIOB/GPIOE (LEDs), RCC, all others | real hardware |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set GPIOC and EXTI elder handlers to `enabled = false` and passthrough handlers to `enabled = true` in `gpio_int_config.toml`. Restore elder mode before Step 3.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/gpio_int_config.toml
```

Press the user button several times during the run to capture interrupt events.

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompts (one per peripheral)

```bash
# GPIOC
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p GPIOC \
  -mname "GPIOC" -ms "GPIOC" \
  -o ./fastdyn_work_gpioc

# EXTI
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p EXTI \
  -mname "EXTI" -ms "EXTI" \
  -o ./fastdyn_work_exti
```

### Step 2 — Send prompts to LLM and compile both models

```bash
# Model 1 — GPIOC → gpioc_model.c → model.so
boardrunner llm -d fastdyn_work_gpioc \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium

# Model 2 — EXTI → exti_model.c → model2.so
boardrunner llm -d fastdyn_work_exti \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with the generated elder models

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/gpio_int_config.toml
```

### Step 4 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p GPIOC -p EXTI \
  -mname GPIOC \
  -d GPIOC:boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  -d EXTI:boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c
```

#### Apply LLM correction patches (if mismatches found)

```bash
boardrunner llm -d fastdyn_work \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/gpioc_model.c \
  -o boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO_INT/generated_models/exti_model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

The LLM `-o` writes the snapshots directly into `generated_models/` — no separate `cp` needed.
