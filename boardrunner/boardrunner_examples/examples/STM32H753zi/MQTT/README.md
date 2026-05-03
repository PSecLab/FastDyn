# MQTT — MQTT Client over USART Serial Transport

**Board:** NUCLEO-H753ZI

The firmware runs an MQTT 3.1.1 client (Eclipse Paho `MQTTPacket`) over USART3. A host-side script bridges the serial stream to a local Mosquitto broker on `localhost:1883`. The firmware connects, subscribes to `stm32/cmd`, publishes a counter on `stm32/sensor` every 2 s, and toggles LED2 on `on` / `off` commands.

## Hardware Connections

USART3 routes to the on-board ST-LINK VCP — no external wiring needed.

| Peripheral         | Pin   | Use                                       |
| ------------------ | ----- | ----------------------------------------- |
| USART3 TX          | PD8   | MCU → host bridge                         |
| USART3 RX          | PD9   | host bridge → MCU                         |
| LED1 (green, PB0)  | —     | CONNACK received                          |
| LED2 (yellow, PE1) | —     | Reflects last `stm32/cmd` (`on` / `off`)  |
| LED3 (red, PB14)   | —     | Toggles on each PUBLISH tick              |

## Expected Output

- LED1 lights ~1 s after launch (CONNACK received)
- LED3 toggles every 2 s (PUBLISH ticks)
- LED2 reflects `mosquitto_pub -t 'stm32/cmd' -m 'on' / 'off'`

## Test Scope

- Compositional rehosting of an MQTT client over a learned USART3 model

## Compositional Model Split

| Component   | Peripheral                       | Compiled to     |
| ----------- | -------------------------------- | --------------- |
| Elder model | USART3                           | `model.so`      |
| Passthrough | RCC, PWR, GPIO, all others       | real hardware   |

## Host Prerequisites

```bash
sudo apt install -y mosquitto mosquitto-clients
pip3 install --user pyserial
groups | grep -q dialout || sudo usermod -aG dialout $USER   # then re-login
```

Mosquitto auto-starts as a systemd service on `localhost:1883`. Confirm with `ss -ltn | grep 1883`.

## Idle Firmware on the Real Board

Before any passthrough run, flash the idle firmware so the real MCU stays out of the way while QEMU drives USART3.

```bash
# build (one time)
cd tests/idle_firmwares/NucleoH753zi && make
cd -

# flash
tests/idle_firmwares/flash_scripts/flash.sh \
  tests/idle_firmwares/NucleoH753zi/build/idlefirmware.axf
```

## Step-by-Step Workflow

### Step 0 — Passthrough run (collect hardware I/O trace)

> **TOML state:** Set USART3 elder handler to `enabled = false` and passthrough handler to `enabled = true` in `mqtt_config.toml`. Restore elder mode before Step 3.

**Terminal A — serial-to-broker bridge** (start before launching `boardrunner`):

```bash
python3 boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/host/bridge.py \
  --serial /dev/ttyACM0 --baud 115200
```

**Terminal B — passthrough capture:**

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/mqtt_config.toml
```

**Press the B1 (NRST) button** on the NUCLEO once the bridge prints its ready banner and `boardrunner run` has launched QEMU. This forces the MCU to cold-boot after the passthrough link is up.

After NRST: LED1 lights within ~1 s, LED3 toggles on each PUBLISH.

**Terminal C — drive inputs while capture is live:**

```bash
mosquitto_sub -t 'stm32/sensor' -v
mosquitto_pub -t 'stm32/cmd' -m 'on'
mosquitto_pub -t 'stm32/cmd' -m 'off'
mosquitto_pub -t 'stm32/cmd' -m 'on'
mosquitto_pub -t 'stm32/cmd' -m 'off'
```

> Keep command payloads ≤ 16 bytes (`on`, `off`, `hi`, `ok`). Longer payloads will overrun the USART3 RX FIFO during passthrough capture.

Let the capture run ~30 s so both the 2 s PUBLISH cadence and the 15 s PINGREQ are represented, then Ctrl-C the `boardrunner run`.

**Quick trace sanity check:**

```bash
grep -c "0x40004824" io.log    # inbound RDR reads — expect ≥ 30
grep -c "0x40004828" io.log    # outbound TDR writes — expect 40–80
grep -c "0x58024818" io.log    # PWR_D3CR polls — expect < 10 (huge counts = VOSRDY stuck → press NRST and re-run)
```

Save the hardware trace before Step 3 overwrites `io.log`:

```bash
cp io.log hardware_log/io.log
```

### Step 1 — Generate LLM prompt

```bash
boardrunner generate -hw hardware_log/io.log -b STM32H753x \
  -p USART3 \
  -mname "USART3" -ms "USART3" \
  -o ./fastdyn_work_mqtt
```

### Step 2 — Send prompt to LLM and compile model

```bash
boardrunner llm -d fastdyn_work_mqtt \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --evaluate
```

### Step 3 — Run with the generated elder model

Restore `Device.usart3` to elder mode (elder `enabled = true`, passthrough `enabled = false`) in `mqtt_config.toml`.

In elder mode the model exposes a PTY at `/tmp/usart1_pty`. Splice it to Mosquitto with `socat` instead of using the serial bridge.

**Terminal A — Mosquitto** (already running as systemd service in most setups):

```bash
sudo systemctl start mosquitto
```

**Terminal B — PTY ↔ broker splice** (start **before** `boardrunner run`, otherwise the model's `/tmp/usart1_pty` doesn't exist and RX bytes never reach the firmware):

```bash
socat -d -d PTY,link=/tmp/usart1_pty,raw,echo=0 TCP:localhost:1883
```

**Terminal C — elder run:**

```bash
boardrunner run -c boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/mqtt_config.toml
```

**Terminal D — drive inputs** (same commands as Step 0):

```bash
mosquitto_sub -t 'stm32/sensor' -v
mosquitto_pub -t 'stm32/cmd' -m 'on'
mosquitto_pub -t 'stm32/cmd' -m 'off'
```

The emulated firmware should connect, subscribe, publish on `stm32/sensor`, and react to inbound `on` / `off` PUBLISHes — same behavior as on hardware.

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
boardrunner llm -d fastdyn_work_mqtt \
  -o boardrunner/boardrunner_sdk/model/model.c \
  --compile --model gpt-5.4 --reasoning-effort medium --stateless --evaluate
```

Repeat Steps 3–4 (kill and relaunch `socat` between runs) until the verifier reports no mismatches.

Once verified, snapshot the model:

```bash
cp boardrunner/boardrunner_sdk/model/model.c \
   boardrunner/boardrunner_examples/examples/STM32H753zi/MQTT/generated_models/usart3_model.c
```
