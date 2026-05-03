# USART — Transmit/Receive Byte

**Board:** NUCLEO-H753ZI

The firmware waits for the user button, transmits a string, then waits for typed input on the same UART and echoes it back.

## Hardware Connections

USART3 is on the on-board ST-LINK USB virtual COM port. Press the user button (PC13) to start the test.

In a separate terminal, attach a host PTY for elder-mode runs:

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 STDIO
```

## Expected Output

- Console prints the firmware's transmitted string after button press
- Anything typed back into the console is echoed by the firmware

## Test Scope

- Transmit a byte
- Receive a byte

## Compositional Model Split

| Component   | Peripheral                  | Compiled to     |
| ----------- | --------------------------- | --------------- |
| Elder model | USART3                      | `model.so`      |
| Passthrough | RCC, GPIO, all others       | real hardware   |

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set USART3 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `uart_config.toml`. Restore elder mode before Step 3.

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml
```

Press the user button, then type some characters into the host console.

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p USART3 \
  -mname "USART3" -ms "USART3" \
  -o ./fastdyn_work_usart
```

### Step 2 — Send prompt to LLM and compile model

```bash
boardrunner llm -d fastdyn_work_usart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium
```

### Step 3 — Run with the generated elder model

Start the socat PTY in another terminal first (see Hardware Connections), then:

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml
```

### Step 4 — Verify against hardware trace

```bash
boardrunner verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p USART3 \
  -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c
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
   boardrunner/boardrunner_examples/examples/STM32H753zi/USART/generated_models/model.c
```
