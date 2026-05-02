# GPIO_INT — Execute Callback After Pin Interrupt

**Board:** MAX78000FTHR

The firmware configures GPIO0 P0.2 (SW1) as a falling-edge interrupt input. When the pin goes low, the ISR toggles the red LED on GPIO2 P2.0.

## Hardware Connections

- **SW1** (on-board user button on P0.2) — press to trigger the interrupt during passthrough trace collection
- **Red LED** (on-board, P2.0) — toggles on each interrupt

## Expected Output

- Red LED toggles on each button press (passthrough) or each virtual-button command (elder)
- Console prints `Interrupt callback executed!` per event

## Test Scope

- Execute callback after pin interrupt

## Compositional Model Split

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | GPIO0 (input + IRQ)    | `model.so`      |
| Passthrough | GPIO2 (LED), UART, GCR, all others | real hardware |

GPIO2 stays on passthrough so the ISR's `MXC_GPIO_OutToggle` writes to the real chip and the physical LED on the board actually toggles.

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

> **TOML state:** Set GPIO0 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `gpio_int_config.toml`. Restore elder mode before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/gpio_int_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Press **SW1** several times during the run to capture interrupt events.

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p GPIO0 -p GPIO2 \
  -mname "GPIO0" -ms "GPIO0" \
  -o ./fastdyn_work_gpio0
```

### Step 2 — Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_gpio0 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Run with the generated elder model

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/gpio_int_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Trigger virtual button presses from a separate terminal:

```bash
# Continuous toggling (Ctrl+C to stop)
while true; do echo -n "t" > /tmp/max78000_gpio0_pin2; sleep 0.5; done
```

Each `t` flips the pin level — the falling edges fire IRQ 24 and toggle the physical LED via passthrough.

### Step 4 — Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p GPIO0 \
  -mname GPIO0 \
  -d GPIO0:boardrunner/boardrunner_sdk/model/model.c
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
   boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/generated_models/gpio0_model.c
```
