# USART Ablation — NUCLEO-H753ZI

**Peripheral:** USART3 (base `0x40004800`, NVIC IRQn 39)
**Firmware:** `boardrunner/boardrunner_examples/examples/STM32H753zi/USART/firmwares/UART_ComIT.elf` — waits for blue button (PC13 EXTI), transmits a banner, then enters an interrupt-driven echo loop. 9600 8N1.
**TOML:** `boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml`
**Host bridge:** PTY via `api_pty_*` (no slave device on the verifier `-d` line).

## Results

| Variant | Iters | Tokens | Latency (s) | Result |
|---|---|---|---|---|
| `full_medium` | 1 | 26,217 | 227.6 | PASS |
| `ablation_no_encoder` | 1 | 96,409 | 228.0 | PASS |
| `ablation_no_vio` | 3 | 90,559 | 552.5 | FAIL (verifier pass / functional fail — `seed[]="Hello world"` memorized at line 156 of `generated_model.c`) |
| `ablation_no_verifier` | 1 (single-shot) | 26,217 | 227.6 | PASS (reused from `full_medium/001_*`) |

A2 (No RCA) is omitted: USART baseline converges at iter 1, leaving no broken iter-1 model to seed A2 with.

## Prerequisites

```bash
# Flash firmware
tests/idle_firmwares/flash_scripts/flash.sh \
  boardrunner/boardrunner_examples/examples/STM32H753zi/USART/firmwares/UART_ComIT.elf

# PTY bridge (leave running in a separate terminal)
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 STDIO

# For Step 0 only: also flash the idle firmware
tests/idle_firmwares/flash_scripts/flash.sh tests/idle_firmwares/prebuilt/idlefirmware_h7.axf
```

Edit `uart_config.toml` to switch the `[Device.usart3]` handler between `passthrough` (Step 0) and `elder` (everything else).

## Step 0 — Passthrough trace

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml
```

Press the blue button; type `hello` in `minicom -D /dev/ttyACM0 -b 9600`; Ctrl-C. Output: `hardware_log/io.log`. Flip TOML to elder mode and re-flash the USART firmware before continuing.

## `full_medium/`

```bash
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p USART3 -mname USART3 -ms USART3 \
  -o ./fastdyn_work_usart

fastdyn llm -d fastdyn_work_usart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml

fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p USART3 -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c

# Correction loop (repeat run + verifier + llm until verifier passes or 10-call cap)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

## A1: `ablation_no_encoder/`

Add `--no-encoder` on `fastdyn generate` and on every `fastdyn verifier` call:

```bash
fastdyn generate --no-encoder -hw hardware_log/io.log -b STM32H753x \
  -p USART3 -mname USART3 -ms USART3 \
  -o ./fastdyn_work_usart_a1

fastdyn llm -d fastdyn_work_usart_a1 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml

fastdyn verifier --no-encoder -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p USART3 -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c
```

## A3: `ablation_no_vio/`

Add `--no-vio` on `fastdyn generate` and on every `fastdyn verifier` call:

```bash
fastdyn generate --no-vio -hw hardware_log/io.log -b STM32H753x \
  -p USART3 -mname USART3 -ms USART3 \
  -o ./fastdyn_work_usart_a3

fastdyn llm -d fastdyn_work_usart_a3 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml

fastdyn verifier --no-vio -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p USART3 -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c
```

## A4: `ablation_no_verifier/`

Single-shot: A4 = the iter-1 LLM call from `full_medium/`. The cell's `001_*` files and `metrics.jsonl` are direct copies of the corresponding `full_medium/` artifacts. The functional pass/fail is determined by running the iter-1 model once on hardware:

```bash
# Generate + single LLM call (identical to full_medium iter 1)
fastdyn generate -hw hardware_log/io.log -b STM32H753x \
  -p USART3 -mname USART3 -ms USART3 \
  -o ./fastdyn_work_usart_a4

fastdyn llm -d fastdyn_work_usart_a4 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate

# Single-shot verification (verifier used as pass/fail oracle only — no correction)
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/USART/uart_config.toml
fastdyn verifier -hw hardware_log/io.log -em io.log -b STM32H753x \
  -p USART3 -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c
```

## Pitfalls

- The ablation flag (`--no-encoder` / `--no-vio`) must be passed on **every** `fastdyn verifier` call, not just iter 1. Forgetting it on a revision iter re-introduces the ablated component into the correction prompt.
- The PTY bridge (`socat`) must already be open before `fastdyn run` starts.
- Type the same characters in passthrough and elder runs so trace comparisons are meaningful.
- `qemu_plugin_raise_irq` takes `NVIC IRQn + 16` (USART3 → 55).
