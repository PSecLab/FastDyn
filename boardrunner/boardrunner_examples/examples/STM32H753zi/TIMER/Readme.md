# TIMER — TIM3 Time Base with Interrupt

**Firmware:** TIM3 configured in time base mode (1-second period at 10 KHz counter clock). The update interrupt fires `HAL_TIM_PeriodElapsedCallback`, and the counter register is read to verify it advances.

**Board:** NUCLEO-H753ZI (STM32H753ZI)

**Required model goals:**
1. Execute callback after interrupt (TIM3 update IRQ 29)
2. Read counter value (TIM3 CNT register returns advancing values)

---

## Hardware Wiring

No external wiring required. The firmware uses only:
- **TIM3** (on-chip, APB1 bus) — timer peripheral
- **LED1** (PB0), **LED2** (PE1), **LED3** (PB14) — test pass/fail indicators

| LED | Meaning |
|-----|---------|
| LED1 on | Test 1 PASS — period-elapsed callback fired |
| LED2 on | Test 2 PASS — counter value advanced between two reads |
| LED3 on | Any test FAIL or error |

---

## Compositional Model Split

This is a single-model example. Only TIM3 needs an elder model; all other peripherals are handled by passthrough.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | TIM3 | `generated_models/tim3_model.c` | `model.so` |
| Passthrough | RCC, PWR, GPIOB (LED1/LED3), GPIOE (LED2), SYSCFG, ... | -- | real hardware |

No inter-model signals, no external slave devices, no DMA.

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the TIM3 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `timer_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/TIMER/timer_config.toml
```

Output: `hardware_log/io.log`

### Step 1 -- Generate LLM prompt

Encode the hardware trace for TIM3. The encoder extracts init vs. steady-state patterns, computes per-register entropy, and detects the TIM3 update interrupt handler.

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p TIM3 \
  -mname "TIM3" -ms "TIM3" \
  -o ./fastdyn_work_tim3
```

### Step 2 -- Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_tim3 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 -- Run with the generated elder model

The TOML is already shipped in elder mode (elder `enabled = true`, passthrough `enabled = false` for TIM3). Run directly:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/TIMER/timer_config.toml
```

Output: `io.log` (emulation trace)

Expected behavior: LED1 and LED2 turn on (both tests pass). LED3 stays off.

### Step 4 -- Verify against hardware trace

The Verifier diffs the emulation trace against the hardware ground truth, register by register.

> **Note:** Only TIM3 is passed to `-p`. Do NOT include passthrough-only peripherals (GPIOB, RCC, etc.) — they would produce false mismatches.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p TIM3 \
  -mname TIM3 \
  -d TIM3:boardrunner/boardrunner_sdk/model/model.c
```

#### Apply LLM correction patches (if mismatches found)

If the Verifier writes `fastdyn_work/revised_prompt.txt`, send it back to the LLM:

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Repeat Steps 3-4 until the Verifier reports no mismatches. Timer models typically converge in 1-3 iterations.

---

## Configuration

Platform configuration: [`timer_config.toml`](timer_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
