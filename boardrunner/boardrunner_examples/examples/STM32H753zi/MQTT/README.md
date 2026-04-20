# MQTT client over serial transport (cellular-modem IoT pattern)

**Firmware:** MQTT 3.1.1 client built on **Eclipse Paho Embedded C** (`MQTTPacket`, client subset — the canonical embedded MQTT stack, deployed in production IoT gateways). The MCU speaks the MQTT wire format directly over USART3 (115200-8N1) — the same deployment pattern used by real cellular-modem IoT devices (Quectel BG96, SIMCom SIM7600, u-blox SARA), where the modem owns the TCP/TLS stack and the MCU frames MQTT over a serial link. In this example the role of the modem is played by a host-side bridge (`bridge.py` for passthrough, `socat` for elder) that relays the byte stream to a Mosquitto broker on TCP:1883. The firmware connects, subscribes to `stm32/cmd`, publishes a counter on `stm32/sensor` every 2 s, and toggles LED2 on received `on` / `off` commands.

**Board:** NUCLEO-H753ZI (STM32H753ZI, Cortex-M7).

**Firmware source:** `/scratch/Fastdyn/STM_Projects/STM32H7_4_53/MQTT/` (Paho vendored under `Middlewares/MQTTPacket/`).

**Required model goals:**

1. Faithfully emulate USART3 polled TX/RX such that the Paho parser (`MQTTPacket_read` → `MQTTDeserialize_*`) consumes and produces identical byte sequences to hardware.
2. Preserve the timing of inbound PUBLISH frames enough that `on_cmd_message` fires on the same bytes seen during passthrough.

---

## Hardware Wiring

No external peripherals — the MQTT broker lives on the host PC, reached through the ST-Link VCP that is already on the NUCLEO board.

| Peripheral             | Pin(s) | Use                                                      |
| ---------------------- | ------ | -------------------------------------------------------- |
| USART3 TX              | PD8    | MCU → host bridge (PUBLISH, PINGREQ, SUBSCRIBE, CONNECT) |
| USART3 RX              | PD9    | host bridge → MCU (CONNACK, SUBACK, PUBLISH, PINGRESP)   |
| LED1 (green, PB0)      | —      | connected (CONNACK received)                             |
| LED2 (yellow, PE1)     | —      | last `stm32/cmd` message — `on` / `off`                  |
| LED3 (red, PB14)       | —      | toggles on each PUBLISH tick                             |
| LD_RED (on error path) | —      | `Error_Handler` — broker unreachable or protocol error   |

---

## Compositional Model Split

Single-model example. Only USART3 needs an elder model; everything else (RCC, PWR, GPIOB/D/E, SYSCFG, SysTick) is handled by passthrough.

| Component   | Peripheral                                 | Generated file                    | Compiled to   |
| ----------- | ------------------------------------------ | --------------------------------- | ------------- |
| Model 1     | USART3                                     | `generated_models/usart3_model.c` | `model.so`    |
| Passthrough | RCC, PWR, GPIOB/D/E, SYSCFG, Cortex-M7 PPB | —                                 | real hardware |

No inter-model signals, no external slave devices, no DMA. The MQTT parser itself (Eclipse Paho) runs entirely inside the firmware binary — it is above the modeled-hardware boundary and is not a separate BoardRunner model.

---

## Host Prerequisites

These sit outside BoardRunner but are required for both passthrough trace collection and any live interactive run on hardware:

```bash
sudo apt install -y mosquitto mosquitto-clients
pip3 install --user pyserial
groups | grep -q dialout || sudo usermod -aG dialout $USER   # then re-login
```

Mosquitto auto-starts on `localhost:1883` after install (`ss -ltn | grep 1883`). If you see `Error: Address already in use` when invoking `mosquitto -v`, that's the systemd service already listening — ignore it, the bridge will still connect.

The serial ↔ broker bridge is at `host/bridge.py` (copied from the firmware tree). Run it **before** power-cycling the board, or the firmware will block in `mqtt_client_connect` and `Error_Handler` will light LED_RED.

```bash
python3 boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/host/bridge.py \
    --serial /dev/ttyACM0 --baud 115200
```

Drive commands from a second shell:

```bash
mosquitto_sub -t 'stm32/sensor' -v          # observe PUBLISH
mosquitto_pub -t 'stm32/cmd' -m 'on'        # LED2 ON
mosquitto_pub -t 'stm32/cmd' -m 'off'       # LED2 OFF
```

> **Keep command payloads ≤ 16 bytes** during passthrough capture (`on`, `off`, `hi`, `ok`). Longer payloads will overrun the USART3 RX FIFO — this is a passthrough-timing artifact (see _Passthrough Notes_ below), not a firmware bug.

---

## Idle Firmware on the Real Board

Before any passthrough run, flash the **idle firmware** to the NUCLEO so the real MCU's CPU stays out of the way while QEMU drives USART3 via ST-Link. (If you instead flash the MQTT firmware itself onto the real board, both the real CPU and QEMU will fight for USART3 and produce corrupted traces.)

```bash
# one-time build (if not already built)
cd /scratch/Fastdyn/FastDyn/tests/idle_firmwares/NucleoH753zi && make
# flash
/scratch/Fastdyn/FastDyn/tests/idle_firmwares/flash_scripts/flash.sh \
    /scratch/Fastdyn/FastDyn/tests/idle_firmwares/NucleoH753zi/build/idlefirmware.axf
```

The idle firmware is just `while(1);` — zero peripheral touching.

---

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

Run the firmware against real hardware. FastDyn routes every USART3 register access to the real peripheral via ST-Link and logs the sequence to `io.log` in the FastDyn working directory.

> **TOML state:** Before running this step, temporarily flip `Device.usart3` to passthrough — set the elder handler to `enabled = false` and the passthrough handler to `enabled = true` in `mqtt_config.toml`. Restore elder mode before Step 3.

**Terminal A — bridge + broker:**

```bash
cd /scratch/Fastdyn/FastDyn
python3 boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/host/bridge.py \
    --serial /dev/ttyACM0 --baud 115200
# expect: [bridge] /dev/ttyACM0@115200 <-> localhost:1883
```

**Terminal B — passthrough capture:**

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/mqtt_config.toml
```

**Press the black B1 (NRST) button** on the NUCLEO once the bridge prints its ready banner and `fastdyn run` has launched QEMU. This forces the real MCU to cold-boot _after_ the passthrough link is up, so PWR/VOS transitions start from a clean state (see _Passthrough Notes_ below for why).

After NRST, LED1 should light within ~1 s (CONNACK received) and LED3 starts toggling on each PUBLISH.

**Terminal C — drive inputs while capture is live:**

```bash
mosquitto_sub -t 'stm32/sensor' -v              # observe PUBLISH ticks
mosquitto_pub -t 'stm32/cmd' -m 'on'            # LED2 → ON
mosquitto_pub -t 'stm32/cmd' -m 'off'           # LED2 → OFF
mosquitto_pub -t 'stm32/cmd' -m 'on'            # repeat so the trace has varied inbound timing
mosquitto_pub -t 'stm32/cmd' -m 'off'
```

Let the capture run ~30 s so both the 2 s PUBLISH cadence and the 15 s PINGREQ are represented, then Ctrl-C the `fastdyn run`.

**Sanity-check the trace before moving on:**

```bash
grep -c "0x40004824" io.log         # inbound RDR reads — should be ≥ 30 (CONNACK + SUBACK + a few command PUBLISHes)
grep -c "0x40004828" io.log         # outbound TDR writes — typically 40–80
grep -c "0x58024818" io.log         # PWR_D3CR polls — should be < 10 (a stuck VOS wait would be > 10,000)
```

If `0x58024824` comes back < 4, your FIFO buffering of RX isn't working — re-check the firmware build (see _Passthrough Notes_). If `0x58024818` count is huge, VOSRDY is stuck — press NRST and re-run.

### Step 1 — Generate LLM prompt

Encode the trace for USART3. The encoder extracts init vs. steady-state patterns, per-register entropy, and the byte stream flowing through RDR/TDR.

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn generate -hw io.log -b STM32H753x \
  -p USART3 \
  -mname "USART3" -ms "USART3" \
  -o ./fastdyn_work_mqtt
```

(If you prefer to match the TOML's declared `hardware_trace` path, `cp io.log hardware_log/io.log` first and use `-hw hardware_log/io.log`.)

### Step 2 — Send prompt to LLM and compile model

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn llm -d fastdyn_work_mqtt \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate
```

`--evaluate` appends per-call cost/token/latency rows to `metrics.jsonl`. Keep it on for every run on this board — the paper's LLM cost table consumes it.

### Step 3 — Run with the generated elder model

**Before running:**

1. Flip `Device.usart3` back to elder mode in `mqtt_config.toml` — elder `enabled = true`, passthrough `enabled = false`.
2. Save the hardware trace before Step 3 overwrites `io.log`:
   ```bash
   cp io.log hardware_log/io.log
   ```

Because MQTT is an interactive protocol, elder mode needs a **live byte source** on the model's PTY — the generated model reads inbound bytes from `/tmp/usart1_pty` (the hardcoded path in `boardrunner/boardrunner_sdk/boardrunner_vio/src/pty/pty.c`) and writes outbound bytes out the same PTY. We splice that PTY directly to Mosquitto with one `socat` command; no `bridge.py` or physical board is involved in this step.

**Terminal A — Mosquitto broker** (usually already running as a systemd service; `ss -ltn | grep 1883` to confirm):

```bash
sudo systemctl start mosquitto        # if not already running
```

**Terminal B — PTY ↔ broker splice.** Start this **before** `fastdyn run`, otherwise `api_pty_fd_gen()` fails (`/tmp/usart1_pty` doesn't exist), the model's `pty_fd` stays at -1, and RX bytes never reach the firmware.

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 TCP:localhost:1883
```

`socat` creates a pseudo-terminal pair, symlinks `/tmp/usart1_pty` to the slave end, and wires the master end bidirectionally to `localhost:1883`. The emulated firmware sees a standard tty from the model's perspective; Mosquitto sees a standard TCP client on its end.

**Terminal C — elder run:**

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn run -c boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/mqtt_config.toml
```

**Terminal D — drive inputs live** (exactly the same commands you used in Step 0):

```bash
mosquitto_sub -t 'stm32/sensor' -v          # observe emulated firmware's PUBLISH ticks
mosquitto_pub -t 'stm32/cmd' -m 'on'        # emulated LED2 → ON (visible as GPIOE write)
mosquitto_pub -t 'stm32/cmd' -m 'off'       # emulated LED2 → OFF
```

**Expected:** the emulated firmware CONNECTs to the real broker, receives CONNACK, subscribes, publishes sensor ticks every 2 s on `stm32/sensor`, and reacts to inbound `on`/`off` PUBLISHes by toggling LED2 — exactly the same behavior as hardware, but running entirely in QEMU with the LLM-generated USART3 model.

> **Note on baud rate:** socat's PTY has no physical baud — bytes flow as fast as the reader drains them. If you see inbound-byte overruns (corrupted payloads in the emulated MQTT session), add `b115200` to the socat PTY options to rate-limit to the modeled baud. In practice this hasn't been necessary for this firmware.

### Step 4 — Verify emulation against hardware trace

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn verifier -hw hardware_log/io.log \
  -em io.log \
  -b STM32H753x \
  -p USART3 \
  -mname USART3 \
  -d USART3:boardrunner/boardrunner_sdk/model/model.c
```

> **Note:** Only USART3 is passed to `-p`. Passthrough-only peripherals (GPIOB, GPIOD, GPIOE, RCC, PWR, SYSCFG) must not appear in `-p`, or the verifier will flag false mismatches from passthrough polling patterns.

#### Correction loop (if mismatches found)

The Verifier automatically writes its correction prompt to `fastdyn_llm_history/NNN_prompt.txt`; rerun `fastdyn llm` to apply it:

```bash
cd /scratch/Fastdyn/FastDyn
fastdyn llm -d fastdyn_work_mqtt \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

Then repeat Steps 3–4 (remember to kill and relaunch `socat` between runs if you restart the model, and `cp io.log hardware_log/io.log` is already done from the first pass so no need to repeat). In our reference run the model converged in **3 iterations** — the Verifier's auto-generated prompts focused on missing ISR status bits (IDLE, TEACK/REACK, and FIFO-mode bits TXFE/RXFT/TXFT once CR1.FIFOEN=1 was observed in the trace).

> **Note on model architecture:** the Verifier drives correctness at the register-bit level but can't detect structural architectural mismatches (e.g., interactive-PTY vs. pure-replay). If the first LLM pass produces a PTY-backed model (as in this example), the elder mode must be driven via `socat` as above — do not try to "fix" the model into replay mode through the Verifier loop; it will keep iterating on ISR bits forever without making RX progress. The interactive PTY architecture _is_ the right fit for this peripheral.

#### Final archive

Once the model passes verification, copy the verified source into this example's tree as a permanent record:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/generated_models/usart3_model.c
```

---

## Why This Is a Real-World Target (not a toy)

- The parser under test (`Middlewares/MQTTPacket/`) is **Eclipse Paho Embedded C** — the canonical embedded MQTT client, deployed in production IoT gateways.
- Historically interesting bug classes all live in this parser: remaining-length varint overflow (`MQTTPacket.c:MQTTPacket_decode`), topic-length overflow in PUBLISH (`MQTTDeserializePublish.c`), malformed CONNACK / SUBACK decoding.
- UART-as-transport mirrors real cellular-modem IoT deployments (Quectel BG96, SIMCom SIM7600, u-blox SARA) where the modem owns the TCP stack and the MCU speaks MQTT over AT-framed serial.
- Complements the existing USART echo example by driving a full protocol stack, not a single-byte echo.

---

## Fuzzing Follow-on (post-evaluation)

Once the elder model reproduces the hardware trace, the same model supports fuzz-style input injection by replacing the captured inbound byte sequence with mutated MQTT frames. The mutator lives outside BoardRunner (replaces `bridge.py`); the on-MCU attack surface is exclusively:

- `MQTTPacket.c` — fixed-header / remaining-length varint parser
- `MQTTDeserializePublish.c` — topic-length and payload-length fields
- `MQTTConnectClient.c` — CONNACK deserializer
- `MQTTSubscribeClient.c` — SUBACK deserializer

---

## Configuration

Platform configuration: [`mqtt_config.toml`](mqtt_config.toml)
Firmware binary: [`firmware/mqtt_h753.elf`](firmware/mqtt_h753.elf)
Host bridge: [`host/bridge.py`](host/bridge.py)

All FastDyn CLI options are documented in `src/fastdyn/main.py`.
