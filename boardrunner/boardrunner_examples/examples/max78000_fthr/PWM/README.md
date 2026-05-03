# PWM — Configure PWM as an Autonomous Peripheral

**Board:** MAX78000FTHR

The firmware configures TMR2 in PWM mode at 1 kHz / 50% duty, starts it, and idles. The PWM output continues running on the alt-function pin without further firmware involvement.

## Hardware Connections

None. PWM output is on TMR2's alt-function pin (not probed in this test).

## Expected Output

- Console prints `Period: <n> ticks`, `Duty: <n> ticks (50%)`, then `PASSED`
- Green LED on

## Test Scope

- Configure PWM as an autonomous peripheral

## Compositional Model Split

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | TMR2                   | `model.so`      |
| Passthrough | UART, GCR, GPIO, all others | real hardware |

## Commands

### Start OpenOCD (separate terminal)

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

### Flash the idle firmware

```bash
boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/flash.sh \
  tests/idle_firmwares/prebuilt/idle_firmware_max78000.elf
```

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set TMR2 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `pwm_config.toml`. Restore elder mode before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/PWM/pwm_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p TMR2 \
  -mname "TMR2" -ms "TMR2" \
  -o ./fastdyn_work_tmr2
```

### Step 2 — Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_tmr2 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Run with the generated elder model

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/PWM/pwm_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

### Step 4 — Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p TMR2 \
  -mname TMR2 \
  -d TMR2:boardrunner/boardrunner_sdk/model/model.c
```

#### Apply LLM correction patches (if mismatches found)

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Once verified, snapshot the model:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/PWM/generated_models/tmr2_model.c
```
