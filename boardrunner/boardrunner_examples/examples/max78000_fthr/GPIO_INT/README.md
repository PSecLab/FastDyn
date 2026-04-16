# GPIO_INT — Pin Interrupt Callback (Virtual Button → Real LED)

**Firmware:** GPIO0 Pin 2 (P0.2 / SW1) configured as input with pull-up and falling-edge interrupt. When the button is pressed, GPIO0 IRQ 24 fires and the ISR toggles the red LED on GPIO2 Pin 0 (P2.0) via `MXC_GPIO_OutToggle`. The main loop prints a message each time the callback executes.

**Board:** MAX78000FTHR

**Required model goal:**
1. Execute callback after pin interrupt (GPIO0 INTFL set on falling edge, IRQ 24 fires, ISR toggles GPIO2 output on real hardware)

---

## Hardware Wiring

No external wiring required. The firmware uses only on-chip resources:
- **GPIO0** (on-chip) — input pin P0.2 connected to SW1 (user button, active low with pull-up)
- **GPIO2** (on-chip) — output pin P2.0 (red LED on the FTHR board)
- **UART** (on-chip, via `printf`) — console output

| Indicator | Meaning |
|-----------|---------|
| Red LED (P2.0) toggles on each virtual button press | Interrupt callback fired correctly |
| Console prints "Interrupt callback executed!" | ISR ran and set callback_flag |

**User interaction:** In elder mode, the virtual GPIO0 model simulates the button press. The ISR writes to GPIO2 via passthrough, so the **physical LED on the board toggles**.

---

## Firmware Check

The firmware achieves the required model goal:
- **Input config:** P0.2 configured as input with `MXC_GPIO_PAD_PULL_UP` and `MXC_GPIO_VSSEL_VDDIOH`
- **Interrupt setup:** `MXC_GPIO_IntConfig` with `MXC_GPIO_INT_FALLING`, then `MXC_GPIO_EnableInt` and `NVIC_EnableIRQ(IRQ 24)`
- **ISR:** `gpio_isr` calls `MXC_GPIO_OutToggle` on GPIO2 P2.0 and sets `callback_flag`
- **Main loop:** polls `callback_flag` and prints confirmation message
- No DMA, no timers, no external devices

---

## Compositional Model Split

Only GPIO0 needs an elder model (virtual button + interrupt). GPIO2 (LED) stays in passthrough so that `MXC_GPIO_OutToggle` writes to real hardware and the physical LED toggles on the board.

| Component | Peripheral | Model type | Reason |
|-----------|-----------|------------|--------|
| Model 1 | GPIO0 (input + interrupt) | Elder → `model.so` | Virtual pin — model generates IRQ 24 on simulated button press |
| Passthrough | GPIO2 (LED output) | Passthrough | Writes go to real hardware → physical LED toggles |
| Passthrough | GCR, UART (printf), all others | Passthrough | real hardware |

No inter-model signals required — the ISR is dispatched by the NVIC (handled by QEMU's core). GPIO0's model sets INTFL and raises IRQ 24; the NVIC vectors to the ISR; the ISR writes to GPIO2 via passthrough to the real board.

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

### Monitor firmware (required for passthrough interrupt forwarding)

The real MCU must run the monitor firmware (not the user firmware) during passthrough. The monitor catches hardware interrupts via BKPT and forwards them to QEMU.

Flash the monitor before running passthrough:

```bash
boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/flash.sh \
  tests/idle_firmwares/prebuilt/idle_firmware_max78000.elf
```

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the GPIO0 elder handler to `enabled = false` and its passthrough handler to `enabled = true` in `gpio_int_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/gpio_int_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Press SW1 several times during the run to capture interrupt events in the trace.

Output: `hardware_log/io.log`

### Step 1 -- Generate LLM prompt

Encode the hardware trace for GPIO0. The `-p` flags include both GPIO0 and GPIO2 so the Encoder captures the cross-peripheral interaction: GPIO0's INTFL clear happens right before GPIO2's OUT toggle in the ISR. The `-ms` flag selects only GPIO0 as the peripheral this model is responsible for emulating.

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p GPIO0 -p GPIO2 \
  -mname "GPIO0" -ms "GPIO0" \
  -o ./fastdyn_work_gpio0
```

### Step 2 -- Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_gpio0 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 -- Run with the generated elder model

The TOML is already shipped in elder mode (elder `enabled = true`, passthrough `enabled = false` for GPIO0). GPIO2 remains passthrough. Run directly:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/gpio_int_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `io.log` (emulation trace)

Expected behavior: Virtual button press triggers IRQ 24 → ISR toggles GPIO2 via passthrough → **physical red LED on the board toggles**.

#### Triggering the virtual button

The GPIO0 elder model exposes a FIFO at `/tmp/max78000_gpio0_pin2`. Send commands from a separate terminal to simulate button presses:

```bash
# Simulate falling edge (button press) — toggles LED
echo -n "1" > /tmp/max78000_gpio0_pin2   # drive pin high (release)
echo -n "0" > /tmp/max78000_gpio0_pin2   # drive pin low (press) → falling edge → IRQ

# Or toggle (alternates high/low each call)
echo -n "t" > /tmp/max78000_gpio0_pin2

# Continuous toggling for trace collection (press Ctrl+C to stop)
while true; do echo -n "t" > /tmp/max78000_gpio0_pin2; sleep 0.5; done
```

> **Note:** The first `echo "t"` after startup drives the pin from its initial state. Since the model initializes pin 2 high (pull-up resting state), the first `"t"` drives it low (falling edge → interrupt). Subsequent `"t"` commands alternate.

### Step 4 -- Verify against hardware trace

The Verifier diffs the emulation trace against the hardware ground truth, register by register.

> **Note:** Only GPIO0 is passed to `-p`. Do NOT include GPIO2 or other passthrough-only peripherals — they would produce false mismatches.

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

If the Verifier writes `fastdyn_work/revised_prompt.txt`, send it back to the LLM:

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Repeat Steps 3-4 until the Verifier reports no mismatches.

Once verified, copy the final model to the example's generated_model directory:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/GPIO_INT/generated_model/gpio0_model.c
```

---

## Configuration

Platform configuration: [`gpio_int_config.toml`](gpio_int_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
