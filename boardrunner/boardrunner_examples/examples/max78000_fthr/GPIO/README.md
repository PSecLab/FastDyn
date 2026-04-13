# GPIO — Set/Clear Pin (LED Blink)

**Firmware:** GPIO2 Pin 0 (P2.0) configured as output. The firmware toggles the red LED in a 500 ms ON / 500 ms OFF loop using `MXC_GPIO_OutSet` and `MXC_GPIO_OutClr`.

**Board:** MAX78000FTHR

**Required model goal:**

1. Set/Clear a pin (GPIO2 OUT_SET / OUT_CLR registers drive P2.0)

---

## Hardware Wiring

No external wiring required. The firmware uses only:

- **GPIO2** (on-chip) — output pin control for LED
- **UART** (on-chip, via `printf`) — console output
- **TMR** (on-chip, via `MXC_Delay`) — delay timing

| LED                                      | Meaning                                               |
| ---------------------------------------- | ----------------------------------------------------- |
| Red LED (P2.0) blinking                  | Firmware running correctly — pin toggles every 500 ms |
| Console prints "Pin set" / "Pin cleared" | UART semihosting output confirms state                |

---

## Compositional Model Split

This is a single-model example. Only GPIO2 needs an elder model; all other peripherals are handled by passthrough.

| Component   | Peripheral                         | Generated file                   | Compiled to   |
| ----------- | ---------------------------------- | -------------------------------- | ------------- |
| Model 1     | GPIO2                              | `generated_models/gpio2_model.c` | `model.so`    |
| Passthrough | UART, GCR, TMR (delay), all others | --                               | real hardware |

No inter-model signals, no external slave devices, no DMA.

---

## Prerequisites

### OpenOCD (required for passthrough)

Before running any `fastdyn run` command, start OpenOCD in a **separate terminal**:

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

Keep this terminal open for the duration of the passthrough or hybrid run.

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the GPIO2 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `gpio_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO/gpio_config.toml \
-s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 -- Generate LLM prompt

Encode the hardware trace for GPIO2. The encoder extracts init vs. steady-state patterns and computes per-register entropy.

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p GPIO2 \
  -mname "GPIO2" -ms "GPIO2" \
  -o ./fastdyn_work_gpio2
```

### Step 2 -- Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_gpio2 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 -- Run with the generated elder model

The TOML is already shipped in elder mode (elder `enabled = true`, passthrough `enabled = false` for GPIO2). Run directly:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO/gpio_config.toml \
-s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `io.log` (emulation trace)

Expected behavior: Red LED (P2.0) blinks at 1 Hz. Console prints "Pin set (LED ON)" and "Pin cleared (LED OFF)" alternating.

### Step 4 -- Verify against hardware trace

The Verifier diffs the emulation trace against the hardware ground truth, register by register.

> **Note:** Only GPIO2 is passed to `-p`. Do NOT include passthrough-only peripherals (UART, GCR, TMR, etc.) — they would produce false mismatches.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p GPIO2 \
  -mname GPIO2 \
  -d GPIO2:boardrunner/boardrunner_sdk/model/model.c
```

#### Apply LLM correction patches (if mismatches found)

If the Verifier writes `fastdyn_work/revised_prompt.txt`, send it back to the LLM:

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Repeat Steps 3-4 until the Verifier reports no mismatches. GPIO models are simple and typically converge in 1-2 iterations.

Once verified, copy the final model to the example's generated_models directory:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO/generated_models/gpio2_model.c
```

---

## Configuration

Platform configuration: [`gpio_config.toml`](gpio_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
