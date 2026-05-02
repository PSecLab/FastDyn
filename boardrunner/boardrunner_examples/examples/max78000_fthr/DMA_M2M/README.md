# DMA — Memory-to-Memory Transfer

**Board:** MAX78000FTHR

The firmware configures DMA channel 0 to copy 64 bytes from one RAM buffer to another, waits for the DMA completion interrupt, then verifies the destination matches the source.

## Hardware Connections

None.

## Expected Output

- Console prints `DMA done: 1`, `Mismatches: 0/64`, source and destination first 4 bytes, then `PASSED`
- Green LED on

## Test Scope

- DMA memory transfer

## Compositional Model Split

| Component   | Peripheral                  | Compiled to   |
| ----------- | --------------------------- | ------------- |
| Elder model | DMA                         | `model.so`    |
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

> **TOML state:** Set DMA elder handler to `enabled = false` and passthrough handler to `enabled = true` in `dma_m2m_config.toml`. Restore elder mode before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/DMA_M2M/dma_m2m_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

The MAX78000 SVD models DMA channels as register clusters which the encoder does not yet expand (see `docs/TODO_encoder_svd_cluster_resolution.md`). Use `--no-encoder` to feed the raw I/O log to the LLM:

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p DMA \
  -mname "DMA" -ms "DMA" \
  --no-encoder \
  -o ./fastdyn_work_dma
```

### Step 2 — Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_dma \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Run with the generated elder model

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/DMA_M2M/dma_m2m_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

### Step 4 — Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p DMA \
  -mname DMA \
  -d DMA:boardrunner/boardrunner_sdk/model/model.c
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
   boardrunner/boardrunner_examples/examples/max78000_fthr/DMA_M2M/generated_models/dma_model.c
```
