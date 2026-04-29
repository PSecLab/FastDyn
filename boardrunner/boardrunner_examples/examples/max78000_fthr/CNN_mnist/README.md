# CNN_mnist — Compositional Rehosting of an Accelerator-Dependent Firmware

**Firmware:** MAX78000 MNIST demo. Loads weights into the on-chip CNN, feeds one 28x28 digit, triggers inference, reads classification output from CNN memory, and prints results over UART0. Built from `/scratch/Fastdyn/saved_maxim_examples/Examples/MAX78000/CNN/mnist/`.

**Board:** MAX78000FTHR.

**Case Study 2 purpose:** show that \fd can rehost firmware whose critical path depends on the on-chip CNN accelerator, a peripheral whose semantics cannot be learned from MMIO traces, by routing only the CNN's MMIO window to silicon while the rest of the board is emulated.

---

## Compositional backend split

| MMIO range                | Peripheral(s)                                                 | Backend                            |
| ------------------------- | ------------------------------------------------------------- | ---------------------------------- |
| `0x40042000 - 0x40042FFF` | UART0                                                         | VIO (elder, reused `uart_model.c`) |
| `0x40000000 - 0x40041FFF` | GCR (incl. CNN power gates), GPIO0/1, TMR, I2C, AES, DMA, ICC | Passthrough                        |
| `0x40043000 - 0xE00FFFFF` | GPIO2 (LEDs), **CNN @ `0x50000000`**, UART1/2, etc.           | Passthrough                        |
| `0xE0000000 - 0xEFFFFFFF` | Cortex-M4 system space (NVIC, SysTick, SCB)                   | Passthrough                        |

The CNN quadrants (control, weight memory, data memory, bias memory) live in the `0x5000_0000` window and fall inside the second passthrough range. No special declaration is needed beyond the existing range.

---

## Prerequisites

### OpenOCD (passthrough requires a real MAX78000FTHR attached)

Start this in a separate terminal **before** running `fastdyn`:

```bash
/scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/bin/openocd \
  -s /scratch/Softwares/Maxim_Installation/Maxim_Installation_Folder/Tools/OpenOCD/scripts \
  -f interface/cmsis-dap.cfg \
  -f target/max78000.cfg
```

Leave it running for the duration of the bring-up.

### socat (VIO UART0 PTY)

UART0 in VIO mode writes to and reads from a host PTY. Start this in another terminal:

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 -,raw,echo=0
```

The mnist classification output will appear in this terminal.

### Build the UART VIO model into `model.so`

The CNN_mnist TOML routes UART0 to the elder VIO model that the SDK build emits at `boardrunner/boardrunner_sdk/build/model.so`. That `.so` must exist before Step 1 below — `cnn_mnist_config.toml` does **not** regenerate it.

There are two paths. Pick **A** if you've already verified the UART model in the standalone UART example, **B** if you haven't.

#### A. Reuse the verified UART model (fastest)

Copy the verified model into the SDK source path and let the SDK rebuild it:

```bash
cp boardrunner/boardrunner_examples/examples/max78000_fthr/UART/generated_models/uart_model.c \
   boardrunner/boardrunner_sdk/model/model.c
cmake -S boardrunner/boardrunner_sdk -B boardrunner/boardrunner_sdk/build
cmake --build boardrunner/boardrunner_sdk/build
```

Confirm the artifact exists:

```bash
ls -l boardrunner/boardrunner_sdk/build/model.so
```

#### B. Regenerate the UART model from scratch

Run the full UART pipeline against the standalone UART example **first** (not against `cnn_mnist_config.toml` — the CNN_mnist firmware doesn't exercise UART RX). The end product is the same `model.so` that CNN_mnist consumes.

```bash
# 0. Collect the UART hardware trace (UART TOML must be in passthrough mode for this step;
#    flip elder -> disabled, passthrough -> enabled in uart_config.toml first)
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# 1. Encode the trace into an LLM prompt
fastdyn generate -hw hardware_log/io.log -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname "UART" -ms "UART" \
  -o ./fastdyn_work_uart

# 2. Synthesize and compile model.so
fastdyn llm -d fastdyn_work_uart \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium \
  --evaluate

# 3. Run the UART example in elder mode (restore uart_config.toml: elder enabled, passthrough disabled)
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/UART/uart_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr

# 4. Verify against the hardware trace
fastdyn verifier -hw hardware_log/io.log -em io.log \
  -b Max78000 \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr/board_setup/max78000.svd \
  -p UART -mname UART \
  -d UART:boardrunner/boardrunner_sdk/model/model.c
```

If Step 4 reports mismatches, run the correction loop (`fastdyn llm ... --stateless`) and repeat 3-4 until clean. Once verified, save the model into the example's record:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/max78000_fthr/UART/generated_models/uart_model.c
```

After either path, `boardrunner/boardrunner_sdk/build/model.so` is in place and CNN_mnist will pick it up automatically.

---
