# PWM — TIM3 PWM Output (Autonomous, No Interrupts)

**Firmware:** TIM3 configured in PWM mode on Channel 1 (PA6). 50% duty cycle (Pulse=5000, ARR=9999) at 10 KHz counter clock. PWM runs autonomously after `HAL_TIM_PWM_Start` — no interrupts are used.

**Board:** NUCLEO-H753ZI (STM32H753ZI)

**Required model goal:**
- Configure PWM as an autonomous peripheral (counter runs freely after start)

---

## Hardware Wiring

No external wiring required beyond the NUCLEO board. The firmware uses:
- **TIM3 Channel 1** (on-chip, APB1 bus) — PWM output on PA6
- **GPIOA** (PA6) — alternate function for TIM3_CH1 (handled by passthrough)
- **LED1** (PB0) — test pass indicator

| LED | Meaning |
|-----|---------|
| LED1 on | Test PASS — PWM counter is advancing autonomously |
| LED3 on | Test FAIL or error |

---

## Compositional Model Split

Single-model example. Only TIM3 needs an elder model; all other peripherals are handled by passthrough. No interrupts are used by this firmware.

| Component | Peripheral | Generated file | Compiled to |
|-----------|-----------|----------------|-------------|
| Model 1 | TIM3 | `generated_models/tim3_pwm_model.c` | `model.so` |
| Passthrough | RCC, PWR, GPIOA (PA6 AF), GPIOB (LED1/LED3), GPIOE (LED2), SYSCFG, ... | -- | real hardware |

No inter-model signals, no external slave devices, no DMA, no interrupts.

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the TIM3 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `pwm_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/PWM/pwm_config.toml
```

Output: `hardware_log/io.log`

### Step 1 -- Generate LLM prompt

Encode the hardware trace for TIM3. The encoder extracts init vs. steady-state patterns, computes per-register entropy, and identifies PWM-related register accesses (CCMR1, CCR1, CCER in addition to CR1, PSC, ARR, CNT).

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
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/PWM/pwm_config.toml
```

Output: `io.log` (emulation trace)

Expected behavior: LED1 turns on (test passes — counter is advancing). LED3 stays off.

### Step 4 -- Verify against hardware trace

The Verifier diffs the emulation trace against the hardware ground truth, register by register.

> **Note:** Only TIM3 is passed to `-p`. Do NOT include passthrough-only peripherals (GPIOA, GPIOB, RCC, etc.) — they would produce false mismatches.

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

Repeat Steps 3-4 until the Verifier reports no mismatches. PWM models (timer-based, no interrupts) typically converge in 1-2 iterations.

---

## Configuration

Platform configuration: [`pwm_config.toml`](pwm_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
