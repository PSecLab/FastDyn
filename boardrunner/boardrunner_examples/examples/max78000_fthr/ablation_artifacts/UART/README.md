# UART Ablation Artifacts

**Peripheral:** UART0 (0x40042000), Console UART

**Firmware:** Transmit prompt via printf, receive 5 bytes ("Hello") via polling, echo back, verify match. Pass/fail indicated by green/red LED.

**Model type:** Single elder model, no slave, no DMA, no interrupts (polling mode).

**Hardware trace:** `hardware_log/io.log` (collected via passthrough with socat PTY)

## Results: Full Pipeline (baseline)

| Run      | Iterations | Total Tokens | Latency (s) | Result |
| -------- | ---------- | ------------ | ----------- | ------ |
| 1        | 1          | 11,750       | 101.8       | PASS   |
| 2        | 3          | 37,828       | 264.5       | PASS   |
| 3        | 1          | 10,755       | 98.9        | PASS   |
| **Mean** | **1.7**    | **20,111**   | **155.1**   |        |

## Results: Ablation Variants

| Variant                  | Run 1  | Run 2   | Run 3   | Mean Iters          |
| ------------------------ | ------ | ------- | ------- | ------------------- |
| full_medium              | 1 iter | 3 iters | 1 iter  | 1.7                 |
| ablation_no_encoder (A1) | 1 iter | 1 iter  | 2 iters | 1.3 (102.3k tokens) |
| ablation_no_rca (A2)     | 1 iter | 1 iter  | 1 iter  | 1.0 (11.9k tokens)  |
| ablation_no_vio (A3)     | 6 iters | --     | --      | --                  |

## Commands

### Prerequisites (all variants)

Start these in separate terminals before any run:

```bash
# Terminal 1: OpenOCD
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg

# Terminal 2: Virtual UART (socat)
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 -,raw,echo=0
```

### Full Pipeline -- `full_medium/runN`

```bash
# Generate prompt (full encoder)
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname "UART" -ms "UART" -o ./fastdyn_work_uart

# LLM synthesis
fastdyn llm -d fastdyn_work_uart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate --max-retries 6

# Elder run (TOML must be in elder mode)
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# Verify
fastdyn verifier -hw hardware_log/io.log -em io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname UART -d UART:boardrunner/boardrunner_sdk/model/model.c

# Correction loop (if mismatch, repeat elder run + verify after each)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate --max-retries 6
```

### A1: No Encoder -- `ablation_no_encoder/runN`

Raw `io.log` trace fed directly to LLM. No SVD annotations, no pattern mining, no entropy analysis, no init/runtime separation.

```bash
# Generate prompt WITHOUT encoder
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname "UART" -ms "UART" -o ./fastdyn_work_uart_a1 \
  --no-encoder

# LLM synthesis
fastdyn llm -d fastdyn_work_uart_a1 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate --max-retries 6

# Elder run
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# Verify with --no-encoder (raw trace correction prompt on failure)
fastdyn verifier -hw hardware_log/io.log -em io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname UART -d UART:boardrunner/boardrunner_sdk/model/model.c \
  --no-encoder

# Correction loop (if mismatch)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate --max-retries 6
```

### A2: No RCA -- `ablation_no_rca/runN`

Full encoder for initial prompt, but on verification failure a generic retry prompt is sent instead of targeted RCA correction.

```bash
# Generate prompt (full encoder, same as baseline)
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname "UART" -ms "UART" -o ./fastdyn_work_uart_a2

# LLM synthesis
fastdyn llm -d fastdyn_work_uart_a2 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate --max-retries 6

# Elder run
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# Verify with --no-rca (generic retry prompt on failure)
fastdyn verifier -hw hardware_log/io.log -em io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname UART -d UART:boardrunner/boardrunner_sdk/model/model.c \
  --no-rca

# Correction loop (if mismatch, uses generic revised_prompt.txt)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate --max-retries 6
```

### A3: No VIO -- `ablation_no_vio/runN`

VIO APIs stripped from prompt. Only base QEMU APIs (memory read/write, IRQ, timers) provided. No PTY, I2C, SPI, or signal abstractions.

```bash
# Generate prompt WITHOUT VIO APIs
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname "UART" -ms "UART" -o ./fastdyn_work_uart_a3 \
  --no-vio

# LLM synthesis
fastdyn llm -d fastdyn_work_uart_a3 \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate --max-retries 6

# Elder run
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# Verify (standard verifier with RCA)
fastdyn verifier -hw hardware_log/io.log -em io.log -b Max78000 \
-s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
-p UART -mname UART -d UART:boardrunner/boardrunner_sdk/model/model.c \
--no-vio


# Correction loop (if mismatch)
fastdyn llm -d fastdyn_work \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate --max-retries 6
```

### Saving Artifacts (after each run)

```bash
# Replace <variant> with: full_medium, ablation_no_encoder, ablation_no_rca, ablation_no_vio
# Replace N with: 1, 2, or 3
mkdir -p boardrunner/boardrunner_examples/examples/max78000_fthr/ablation_artifacts/UART/<variant>/runN
cp fastdyn_llm_history/* \
  boardrunner/boardrunner_examples/examples/max78000_fthr/ablation_artifacts/UART/<variant>/runN/
cp boardrunner/boardrunner_sdk/model/model.c \
  boardrunner/boardrunner_examples/examples/max78000_fthr/ablation_artifacts/UART/<variant>/runN/generated_model.c
```

## Notes

- The UART model requires a host PTY for TX/RX. Start socat before elder runs:
  `socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 -,raw,echo=0`
- During passthrough (Step 0), socat is also needed so the user can type "Hello" to generate the RX trace.
- The encoder's rare value transitions feature was critical for first-attempt success -- it exposes the STATUS 0x50 -> 0x140 transition that the top-k pattern extraction previously dropped.
- Run 2 required 3 iterations due to a hex typo in the CTRL ready bit (0x00800000 vs 0x00080000). This is expected LLM non-determinism, not a systematic issue.
- For A3 (No VIO), the model will lack PTY support since `api_pty_*` functions are VIO APIs. The UART model cannot do interactive TX/RX without them, so A3 is expected to fail or produce a non-functional model.
