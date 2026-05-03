# GPIO — Set/Clear Pin

**Board:** NUCLEO-H753ZI

The firmware blinks LED1 on GPIOB. Pressing the user button cycles which LED is active (LED1 / LED2 / LED3); for this test only the GPIOB pattern is modeled.

## Hardware Connections

None. On-board LEDs and user button.

## Expected Output

- LED1 (PB0) blinks at a steady rate

## Test Scope

- Set/Clear a pin

## Compositional Model Split

| Component   | Peripheral                  | Compiled to     |
| ----------- | --------------------------- | --------------- |
| Elder model | GPIOB                       | `model.so`      |
| Passthrough | RCC, PWR, GPIOC (button), all others | real hardware |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set GPIOB elder handler to `enabled = false` and passthrough handler to `enabled = true` in `gpio_config.toml`. Restore elder mode before Step 3.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO/gpio_config.toml
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x -p GPIOB -o ./fastdyn_work_gpio
```

### Step 2 — Send prompt to LLM and compile model

```bash
boardrunner llm -d fastdyn_work_gpio \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with the generated elder model

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO/gpio_config.toml
```

### Step 4 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p GPIOB -mname "GPIOB" \
  -d GPIOB:boardrunner/boardrunner_sdk/model/model.c
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
   boardrunner/boardrunner_examples/examples/STM32H753zi/GPIO/generated_models/model.c
```
