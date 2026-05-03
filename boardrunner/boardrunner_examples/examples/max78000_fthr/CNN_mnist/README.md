# CNN_mnist — Compositional Rehosting with Passthrough Accelerator

**Board:** MAX78000FTHR

The firmware loads weights into the on-chip CNN accelerator, runs inference on a 28×28 MNIST digit, reads the classification result from CNN memory, and prints it over UART0. This is a case study: the CNN accelerator's MMIO window is routed to silicon (passthrough), while UART0 is served by a learned VIO model — the rest of the firmware runs in emulation.

## Hardware Connections

None beyond the board itself (CN1 USB connection for power + serial console).

## Expected Output

- The MNIST classification result printed via UART0 to the host PTY (e.g. `Class 7: confidence ...`)

## Test Scope

- Compositional rehosting of accelerator-dependent firmware (CNN passthrough + UART VIO model)

## Compositional Model Split

| MMIO range                | Peripheral(s)                                                 | Backend       |
| ------------------------- | ------------------------------------------------------------- | ------------- |
| `0x40042000 - 0x40042FFF` | UART0                                                         | Elder VIO (`model.so`, reused from UART example) |
| `0x40000000 - 0x40041FFF` | GCR, GPIO0/1, TMR, I2C, AES, DMA, ICC                          | Passthrough   |
| `0x40043000 - 0xE00FFFFF` | GPIO2 (LEDs), **CNN @ `0x50000000`**, UART1/2, etc.            | Passthrough   |
| `0xE0000000 - 0xEFFFFFFF` | Cortex-M4 system space (NVIC, SysTick, SCB)                    | Passthrough   |

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

### Start socat (separate terminal — host PTY for UART output)

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 -,raw,echo=0
```

The MNIST classification will appear in this terminal.

### Build the UART VIO model into `model.so`

CNN_mnist reuses the verified UART model. Copy it into the SDK source path and rebuild:

```bash
cp boardrunner/boardrunner_examples/examples/max78000_fthr/UART/generated_models/uart_model.c \
   boardrunner/boardrunner_sdk/model/model.c
cmake --build boardrunner/boardrunner_sdk/build
```

If the verified UART model isn't available, run the standalone UART example first to produce it (see `../UART/README.md`).

## Run

```bash
fastdyn run -c boardrunner/boardrunner_examples/examples/max78000_fthr/CNN_mnist/cnn_mnist_config.toml \
  -s boardrunner/boardrunner_examples/examples/max78000_fthr
```

The firmware runs entirely under FastDyn: UART writes go to the elder VIO model (visible in the socat terminal); CNN MMIO accesses are forwarded to the real accelerator on the FTHR board via passthrough.
