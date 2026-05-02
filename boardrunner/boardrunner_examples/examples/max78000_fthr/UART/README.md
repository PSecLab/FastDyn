# UART — Transmit/Receive Byte

**Board:** MAX78000FTHR

The firmware transmits a prompt over UART0, receives 5 characters from the host, echoes each back, and verifies the received string is `"Hello"`.

## Hardware Connections

None. UART0 is the on-board console (USB-serial via CN1).

In a separate terminal, open the console at 115200 8-N-1 and type `Hello` when prompted.

## Expected Output

- Console prompts for 5 characters, echoes each typed byte, then prints `PASSED`
- Green LED on

## Test Scope

- Transmit a byte
- Receive a byte

## Compositional Model Split

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | UART0                  | `model.so`      |
| Passthrough | GCR, GPIO, all others  | real hardware   |

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

> **TOML state:** Set UART elder handler to `enabled = false` and passthrough handler to `enabled = true` in `uart_config.toml`. Restore elder mode before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Type `Hello` on the console when prompted.

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART \
  -mname "UART" -ms "UART" \
  -o ./fastdyn_work_uart
```

### Step 2 — Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_uart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Run with the generated elder model

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Type `Hello` again into the same console.

### Step 4 — Verify against hardware trace

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

```bash
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --stateless --evaluate
```

Once verified, snapshot the model:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/UART/generated_models/uart_model.c
```
