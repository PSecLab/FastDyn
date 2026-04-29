# Modbus RTU slave (industrial fieldbus pattern)

**Firmware:** Modbus RTU slave built on **FreeMODBUS** (cwalter-at fork — the canonical embedded Modbus stack, deployed in industrial PLCs, building-automation gateways, and SCADA front-ends). The MCU speaks the Modbus-RTU wire format directly over USART2 (1200-8N1) — the exact pattern used in factory-floor RS-485 networks. The firmware is a fully-featured slave (slave id 0x0A) exposing 32 holding regs, 32 coils, 16 input regs, 16 discrete inputs, an application command register driving a small state machine (NORMAL / CALIBRATE / DIAGNOSTIC / RESET), setpoint clamping, cross-linked coil↔discrete bits, and out-of-range exception responses. The host driver is `pymodbus` 3.x.

**Board:** NUCLEO-F103RB (STM32F103RB, Cortex-M3).

**Firmware source:** `/scratch/Fastdyn/STM_Projects/STM32F103_Modbus/` (FreeMODBUS vendored under `Middlewares/Third_Party/FreeMODBUS/`).

**Required model goals:**

1. Faithfully emulate USART2 IRQ-driven RX/TX so FreeMODBUS's byte-level state machine (`xMBRTUReceiveFSM`, `xMBRTUTransmitFSM`) produces and consumes identical byte sequences to hardware.
2. Reproduce the SR transition sequence (RXNE → IDLE → TXE → TC) that the FreeMODBUS port driver depends on at every byte boundary.
3. Deliver level-asserted RX and TX interrupts under both `RXNEIE` (RX side) and `TXEIE` / `TCIE` (TX side) so the response transmit loop completes.

---

## Hardware Wiring

No external peripherals — the Modbus master lives on the host PC, reached through the ST-Link VCP that is already on the NUCLEO board.

| Peripheral         | Pin(s) | Use                                              |
| ------------------ | ------ | ------------------------------------------------ |
| USART2 TX          | PA2    | MCU → host (Modbus response frames)              |
| USART2 RX          | PA3    | host → MCU (Modbus request frames)               |
| LED LD2 (green)    | PA5    | heartbeat — toggles every 500 ms in the main loop |
| NRST (B1, black)   | —      | cold reset for clean passthrough start           |

USART2 is multiplexed onto the ST-Link's USB-CDC serial bridge by default on this Nucleo, so no jumper changes are needed.

---

## Compositional Model Split

Single-model example. Only USART2 needs an elder model; everything else (RCC, FLASH, AFIO, GPIOA, NVIC, EXTI, Cortex-M3 PPB) is handled by passthrough.

| Component   | Peripheral                                     | Generated file                  | Compiled to   |
| ----------- | ---------------------------------------------- | ------------------------------- | ------------- |
| Model 1     | USART2                                         | `generated_models/usart2.c`     | `model.so`    |
| Passthrough | RCC, FLASH, AFIO, GPIOA, NVIC, EXTI, ARM PPB   | —                               | real hardware |

No inter-model signals, no external slave devices, no DMA. The Modbus protocol stack itself (FreeMODBUS) runs entirely inside the firmware binary — it is above the modeled-hardware boundary and is not a separate BoardRunner model.

---

## Host Prerequisites

```bash
pip3 install --user 'pymodbus>=3.0' pyserial
groups | grep -q dialout || sudo usermod -aG dialout $USER   # then re-login
```

The single host driver is `host/modbus_bridge.py`. It is the **same script** for both passthrough capture and elder driving — only the `--serial` argument changes:

| Mode        | `--serial` value     |
| ----------- | -------------------- |
| Passthrough | `/dev/ttyACM0`       |
| Elder       | `/tmp/host_modbus`   |

`modbus_bridge.py` exercises FC 01 / 02 / 03 / 04 / 05 / 06 / 0F / 10 plus the application-layer state machine and out-of-range exception path (17 assertions per pass).

```bash
python3 boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/host/modbus_bridge.py \
    --serial <serial-port> [--baud 1200] [--timeout 3.0] [--repeat 1] [-v]
```

---

## Idle Firmware on the Real Board

Before any passthrough run, flash the **idle firmware** to the NUCLEO so the real MCU's CPU stays out of the way while QEMU drives USART2 via ST-Link. (If you instead flash the Modbus firmware itself onto the real board, both the real CPU and QEMU will fight for USART2 and produce corrupted traces.)

```bash
# one-time build (if not already built)
cd /scratch/Fastdyn/FastDyn/tests/idle_firmwares/Nucleo_F103RB && make

# flash
/scratch/Fastdyn/FastDyn/tests/idle_firmwares/flash_scripts/flash.sh \
    /scratch/Fastdyn/FastDyn/tests/idle_firmwares/Nucleo_F103RB/build/idlefirmware.axf
```

The idle firmware is `while(1);` plus BKPT-storing peripheral IRQ vectors — zero peripheral activity from the board's CPU, but USART2/TIM3 IRQs are still forwarded back to QEMU through libhw.

---

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware. FastDyn routes every USART2 register access to the real peripheral via ST-Link and logs the sequence to `io.log` in the FastDyn working directory.

> **TOML state:** Before running this step, flip `Device.usart2` to passthrough — set the elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `modbus_config.toml`. Restore elder mode before Step 3.

**Terminal A — passthrough capture:**

```bash
cd /scratch/Fastdyn/FastDyn
rm -f io.log
fastdyn run --config boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/modbus_config.toml
```

**Press the black B1 (NRST) button** on the NUCLEO once `fastdyn run` has launched QEMU and printed `Loading device [usart2] ...`. This forces the real MCU to cold-boot _after_ the passthrough link is up. LED LD2 should start blinking ~2 Hz once the firmware is running.

**Terminal B — drive Modbus traffic while capture is live:**

```bash
cd /scratch/Fastdyn/FastDyn
python3 boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/host/modbus_bridge.py \
    --serial /dev/ttyACM0
```

Expect 17/17 `[OK ]` lines and `Total failures: 0`. Repeat the bridge invocation a couple of times to grow trace coverage:

```bash
python3 boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/host/modbus_bridge.py \
    --serial /dev/ttyACM0 --repeat 3
```

When you have enough trace, Ctrl-C `fastdyn run` in Terminal A, then archive the trace:

```bash
cp io.log hardware_log/io.log
```

**Sanity-check the trace before moving on:**

```bash
grep -c "0x40004404" io.log         # DR accesses (TX writes + RX reads) — should be ≥ 200
grep -c "Vector = 0x00000036" io.log # USART2 IRQ taken — should be ≥ 80
grep -c "0x40004408" io.log         # BRR writes — should be 1–2 (init only)
```

### Step 1 — Generate LLM prompt

Encode the trace for USART2. The encoder extracts init vs. steady-state patterns, per-register entropy, and the byte stream flowing through DR.

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn generate -hw hardware_log/io.log -b STM32F103xx \
    -p USART2 \
    -mname USART2 -ms USART2 \
    -o ./fastdyn_work_modbus
```

### Step 2 — Send prompt to LLM and compile model

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn llm -d fastdyn_work_modbus \
    -o boardrunner/boardrunner_sdk/model/model.c \
    --compile --model gpt-5.4 --reasoning-effort medium --evaluate
```

`--evaluate` appends per-call cost/token/latency rows to `fastdyn_llm_history/metrics.jsonl`. Keep it on for every run on this board — the paper's LLM cost table consumes it.

### Step 3 — Run with the generated elder model

**Before running:**

1. Flip `Device.usart2` back to elder mode in `modbus_config.toml` — elder `enabled = true`, passthrough `enabled = false`.
2. Make sure `hardware_log/io.log` is preserved from Step 0; this run will overwrite `io.log` with the elder trace.

Because Modbus is an interactive protocol, elder mode needs a **live byte source** on the model's PTY — the generated model reads inbound bytes from `/tmp/usart1_pty` (the hardcoded path in `boardrunner/boardrunner_sdk/boardrunner_vio/src/pty/pty.c`) and writes outbound bytes out the same PTY. We splice that PTY to a host-side endpoint with one `socat` command that creates a PTY pair (`/tmp/usart1_pty` ↔ `/tmp/host_modbus`); no real board is involved in this step.

**Terminal A — PTY pair.** Start this **before** `fastdyn run`, otherwise `api_pty_fd_gen()` fails (`/tmp/usart1_pty` doesn't exist), the model's `pty_fd` stays at -1, and bytes never flow.

```bash
pkill -9 -f socat 2>/dev/null
rm -f /tmp/usart1_pty /tmp/host_modbus
nohup socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 PTY,link=/tmp/host_modbus,raw,echo=0 \
    > /tmp/socat.log 2>&1 &
disown
sleep 1
ls -la /tmp/usart1_pty /tmp/host_modbus    # both should symlink to /dev/pts/<N>
```

`socat` creates a pseudo-terminal pair. The emulated firmware sees one tty (`/tmp/usart1_pty`, opened by the model); the host driver sees the other (`/tmp/host_modbus`).

**Terminal B — elder run:**

```bash
cd /scratch/Fastdyn/FastDyn
rm -f io.log
fastdyn run --config boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/modbus_config.toml
# expect: Loading device [usart2] from boardrunner/boardrunner_sdk/build/model.so
```

**Terminal C — drive Modbus traffic** (exactly the same script as Step 0, only `--serial` changes):

```bash
cd /scratch/Fastdyn/FastDyn
python3 boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/host/modbus_bridge.py \
    --serial /tmp/host_modbus
```

**Expected:** the emulated firmware boots, emits its banner, then responds to all 17 Modbus operations the same way real hardware does — values read back match writes, the state machine reflects CALIBRATE/RESET commands, the setpoint clamps at 1000, and out-of-range addresses return Modbus exception 02. Total failures: 0.

When done, Ctrl-C `fastdyn run` in Terminal B and tear down socat:

```bash
pkill -9 -f socat
```

### Step 4 — Verify emulation against hardware trace

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn verifier -hw hardware_log/io.log \
    -em io.log \
    -b STM32F103xx \
    -p USART2 \
    -mname USART2 \
    -d USART2:boardrunner/boardrunner_sdk/model/model.c \
    -o fastdyn_work_modbus
```

> **Note:** Only USART2 is passed to `-p`. Passthrough-only peripherals (RCC, FLASH, GPIOA, AFIO, NVIC, EXTI) must not appear in `-p`, or the verifier will flag false mismatches from passthrough polling patterns.

#### Correction loop (if mismatches found)

The Verifier writes its correction prompt to `fastdyn_work_modbus/revised_prompt.txt`; rerun `fastdyn llm` to apply it:

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn llm -d fastdyn_work_modbus \
    -o boardrunner/boardrunner_sdk/model/model.c \
    --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

Then repeat Steps 3–4. In our reference run the model converged in **3 iterations** — the first pass produced the basic register layout, the second round fixed the TXE/TC progression after DR writes (TXE rises before TC, both transiently clear on write), and the third round added level-asserted IRQ retriggering on every SR read so the FreeMODBUS port driver's lean IRQ handler sees the correct status word every time.

#### Final archive

Once the model passes verification, copy the verified source into this example's tree as a permanent record:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/Nucleo-F103RB/Modbus/generated_models/usart2.c
```

---

## Why This Is a Real-World Target (not a toy)

- The protocol stack under test (`Middlewares/Third_Party/FreeMODBUS/`) is **FreeMODBUS** (cwalter-at fork) — the canonical embedded Modbus client, deployed in industrial PLCs, BMS gateways, RTUs, and SCADA front-ends.
- Historically interesting bug classes all live in this stack: PDU length validation in `eMBFuncReadHoldingRegister` / `eMBFuncWriteMultipleHoldingRegister`, off-by-one in coil bit-packing in `xMBUtilSetBits` / `xMBUtilGetBits`, CRC-vs-length skew in `eMBRTUReceive`, and address-callback corner cases in `eMBRegHoldingCB`.
- UART-as-fieldbus mirrors real industrial-Modbus deployments where the slave MCU is the only thing on a long RS-485 daisy-chain and must handle adversarial framing, frame-end detection (t3.5), broadcast addresses, and exception responses correctly.
- Complements the existing USART echo example by driving a full protocol stack with bidirectional state, not a single-byte echo.

---

## Fuzzing Follow-on (post-evaluation)

Once the elder model reproduces the hardware trace, the same model supports fuzz-style input injection by replacing `modbus_bridge.py` with a structured Modbus mutator hitting the exact same UART, with the firmware unchanged. The on-MCU attack surface is exclusively:

- `mbrtu.c` — `xMBRTUReceiveFSM`, `eMBRTUReceive` (CRC + length validation, t3.5 state machine)
- `mbcrc.c` — `usMBCRC16` (table-driven CRC)
- `mbfuncholding.c`, `mbfunccoils.c`, `mbfuncinput.c`, `mbfuncdisc.c` — function-code dispatchers and PDU-length checks
- `mbutils.c` — bit-packing helpers for coils / discrete inputs
- `modbus_app.c` — register-callback range checks and the application command state machine

---

## Configuration

Platform configuration: [`modbus_config.toml`](modbus_config.toml)
Firmware binary: [`firmware/STM32F103RB_Modbus.elf`](firmware/STM32F103RB_Modbus.elf)
Host bridge: [`host/modbus_bridge.py`](host/modbus_bridge.py)
Reference model: [`generated_models/usart2.c`](generated_models/usart2.c)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
