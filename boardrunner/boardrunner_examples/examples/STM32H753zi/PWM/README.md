# PWM — Configure PWM as an Autonomous Peripheral

**Board:** NUCLEO-H753ZI

The firmware configures TIM3 in PWM mode on Channel 1 (PA6) at 50% duty / 10 kHz, then idles. PWM runs autonomously after start with no further firmware involvement.

## Hardware Connections

None. PWM output is on PA6 (TIM3_CH1, Arduino D13 area).

## Expected Output

- LED1 (PB0) on if the PWM counter is advancing
- LED3 (PB14) on if the test failed

## Test Scope

- Configure PWM as an autonomous peripheral

## Compositional Model Split

| Component   | Peripheral                  | Compiled to     |
| ----------- | --------------------------- | --------------- |
| Elder model | TIM3                        | `model.so`      |
| Passthrough | RCC, PWR, GPIO, all others  | real hardware   |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set TIM3 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `pwm_config.toml`. Restore elder mode before Step 3.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/PWM/pwm_config.toml
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p TIM3 \
  -mname "TIM3" -ms "TIM3" \
  -o ./fastdyn_work_tim3
```

### Step 2 — Send prompt to LLM and compile model

```bash
boardrunner llm -d fastdyn_work_tim3 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with the generated elder model

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/PWM/pwm_config.toml
```

### Step 4 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p TIM3 \
  -mname TIM3 \
  -d TIM3:boardrunner/boardrunner_sdk/model/model.c
```

#### Apply LLM correction patches (if mismatches found)

```bash
boardrunner llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless
```

Once verified, snapshot the model:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/PWM/generated_models/pwm_model.c
```
