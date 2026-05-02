# ADC — Read an Analog-to-Digital Conversion

**Board:** MAX78000FTHR

The firmware performs a single ADC conversion on channel 3 via polling and reads the result.

## Hardware Connections

None. ADC channel 3 is on the FTHR's analog input pin (typically floating; the value depends on whatever is on the pin).

## Expected Output

- Console prints `Init ADC...`, `ADC init OK`, `Start conversion on channel 3...`, `ADC value: 0xXXXX (StartConv rc=..., GetData rd=0)`, then `PASSED`
- Green LED on

## Test Scope

- Read an analog-to-digital conversion

## Compositional Model Split

| Component   | Peripheral             | Compiled to     |
| ----------- | ---------------------- | --------------- |
| Elder model | ADC                    | `model.so`      |
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

> **TOML state:** Set ADC elder handler to `enabled = false` and passthrough handler to `enabled = true` in `adc_config.toml`. Restore elder mode before Step 3.

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/ADC/adc_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

Output: `hardware_log/io.log`

### Step 1 — Generate LLM prompt

```bash
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p ADC \
  -mname "ADC" -ms "ADC" \
  -o ./fastdyn_work_adc
```

### Step 2 — Send prompt to LLM and compile model

```bash
fastdyn llm -d fastdyn_work_adc \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate
```

### Step 3 — Run with the generated elder model

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/ADC/adc_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

### Step 4 — Verify against hardware trace

```bash
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p ADC \
  -mname ADC \
  -d ADC:boardrunner/boardrunner_sdk/model/model.c
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
   boardrunner/boardrunner_examples/examples/max78000_fthr/ADC/generated_models/adc_model.c
```
