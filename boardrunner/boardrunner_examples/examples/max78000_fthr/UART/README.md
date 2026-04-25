# UART — Transmit/Receive Byte

**Firmware:** UART0 console UART. The firmware transmits a prompt via `printf` (TX), then receives 5 characters one byte at a time using `MXC_UART_ReadCharacter` (RX), echoes each byte back with `MXC_UART_WriteCharacter`, and verifies the received data matches `"Hello"`. On success, the green LED turns on; on failure, the red LED turns on.

**Board:** MAX78000FTHR

**Required model goal:**

1. Transmit a byte (TX via FIFO register)
2. Receive a byte (RX via FIFO register)

---

## Hardware Wiring

No external wiring required. The firmware uses only:

- **UART0** (on-chip) — console UART for TX/RX
- **GPIO2** (on-chip) — LED indicators (pass/fail)
- **TMR** (on-chip, via `MXC_Delay`) — delay timing (if any)

| Output                                           | Meaning                                    |
| ------------------------------------------------ | ------------------------------------------ |
| Console prints "UART Transmit/Receive Byte Test" | TX verified — prompt transmitted via UART0 |
| Console prints "Received: Hello"                 | RX verified — 5 bytes received and matched |
| "Test PASSED" + green LED on                     | Full TX + RX round-trip successful         |
| "Test FAILED" + red LED on                       | Data mismatch or RX error                  |

---

## Compositional Model Split

This is a single-model example. Only UART0 needs an elder model; all other peripherals are handled by passthrough.

| Component   | Peripheral                         | Generated file                  | Compiled to   |
| ----------- | ---------------------------------- | ------------------------------- | ------------- |
| Model 1     | UART                               | `generated_models/uart_model.c` | `model.so`    |
| Passthrough | GPIO2 (LEDs), GCR, TMR, all others | --                              | real hardware |

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

### Virtual UART (required for elder mode RX)

The UART model uses a host PTY for TX/RX. Before running in elder mode (Step 3), start socat in a **separate terminal**:

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 -,raw,echo=0
```

TX output (printf, echo) appears in this terminal. Type here to send RX data to the firmware. During the passthrough Step 0 run, socat is also needed so the firmware can receive input from the user.

---

## Step-by-Step Workflow

### Step 0 -- Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware in passthrough mode. FastDyn records every peripheral register access to `hardware_log/io.log`.

> **TOML state:** Before running this step, temporarily set the UART0 elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `uart_config.toml`. Restore elder mode (elder `enabled = true`, passthrough `enabled = false`) before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 -- Generate LLM prompt

Encode the hardware trace for UART0. The encoder extracts init vs. steady-state patterns and computes per-register entropy.

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART \
  -mname "UART" -ms "UART" \
  -o ./fastdyn_work_uart
```

### Step 2 -- Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_uart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 -- Run with the generated elder model

The TOML is already shipped in elder mode (elder `enabled = true`, passthrough `enabled = false` for UART0). Run directly:

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `io.log` (emulation trace)

Expected behavior: Console prints the full test prompt, receives "Hello", echoes it back, and prints "Test PASSED".

### Step 4 -- Verify against hardware trace

The Verifier diffs the emulation trace against the hardware ground truth, register by register.

> **Note:** Only UART is passed to `-p`. Do NOT include passthrough-only peripherals (GPIO, GCR, TMR, etc.) — they would produce false mismatches.

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART \
  -mname UART \
  -d UART:boardrunner/boardrunner_sdk/model/model.c
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

Once verified, copy the final model to the example's generated_models directory:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/UART/generated_models/uart_model.c
```

---

## Configuration

Platform configuration: [`uart_config.toml`](uart_config.toml)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
