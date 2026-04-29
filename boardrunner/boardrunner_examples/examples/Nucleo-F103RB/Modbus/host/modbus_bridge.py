#!/usr/bin/env python3
"""
modbus_bridge.py — Host-side pymodbus driver for the Nucleo-F103RB Modbus
RTU slave example.

This script *is* the application-layer test for the firmware. It exercises a
representative spread of FreeMODBUS function codes so that:

  - Step 0 passthrough captures a hardware trace covering FC 01/02/03/04/
    05/06/0F/10 (coils + discrete + holding + input registers, single + multi).
  - Step 3 elder-mode driving uses the same byte sequence end-to-end so the
    BoardRunner-generated USART2 model is validated against the same surface
    it was synthesized from.
  - Later fuzz campaigns can swap pymodbus for a structured mutator hitting
    the exact same UART, with the firmware unchanged.

Usage:
    Passthrough (real board, ST-Link VCP):
        python3 modbus_bridge.py --serial /dev/ttyACM0

    Elder mode (socat-spliced PTY pair from the BoardRunner model side):
        python3 modbus_bridge.py --serial /tmp/host_modbus

Bug-attribution discipline: the script never assumes a specific response
shape beyond what pymodbus already validates. Any divergence between
hardware-trace and elder-mode behaviour indicates a bug in the BoardRunner-
generated USART2 model. Any FreeMODBUS-side crash under fuzzing is a
genuine FreeMODBUS finding because this driver only sends well-formed
frames during the passthrough/elder steps.
"""
import argparse
import logging
import sys
import time

try:
    from pymodbus.client import ModbusSerialClient
except ImportError:
    sys.stderr.write(
        "pymodbus is required. Install with:\n"
        "    pip3 install --user 'pymodbus>=3.0'\n"
    )
    sys.exit(1)

SLAVE_ID = 0x0A   # matches modbus_app.c:SLAVE_ID

# Application register map — mirrors modbus_app.c.
CMD_REG       = 0
SETPOINT_REG  = 1
CMD_RESET     = 0x0001
CMD_CALIBRATE = 0x0002
CMD_DIAGNOSTIC = 0x0003


def banner(s):
    print(f"\n=== {s} ===")


def expect(label, ok, detail=""):
    tag = "OK " if ok else "FAIL"
    print(f"  [{tag}] {label}{(' — ' + detail) if detail else ''}")
    return ok


def exercise(client):
    """Hit every register-callback path FreeMODBUS exposes, plus a few
    application-layer behaviours. Each call lights up a different branch
    in the FreeMODBUS dispatcher; cumulatively they cover FC 01/02/03/04/
    05/06/0F/10."""
    failures = 0

    banner("Read input registers (FC 04) — sanity / liveness")
    rr = client.read_input_registers(address=0, count=5, device_id=SLAVE_ID)
    if rr.isError():
        failures += not expect("FC 04 read input[0..4]", False, str(rr))
    else:
        expect("FC 04 read input[0..4]", True, f"values={rr.registers}")

    banner("Read holding registers (FC 03) — single + multi")
    rr = client.read_holding_registers(address=0, count=1, device_id=SLAVE_ID)
    failures += not expect("FC 03 read holding[0]   (cmd reg)",
                           not rr.isError(),
                           "" if rr.isError() else f"value={rr.registers}")
    rr = client.read_holding_registers(address=0, count=8, device_id=SLAVE_ID)
    failures += not expect("FC 03 read holding[0..7]",
                           not rr.isError(),
                           "" if rr.isError() else f"values={rr.registers}")

    banner("Write single holding register (FC 06)")
    rr = client.write_register(address=SETPOINT_REG, value=750, device_id=SLAVE_ID)
    failures += not expect("FC 06 setpoint=750", not rr.isError())
    rr = client.read_holding_registers(address=SETPOINT_REG, count=1, device_id=SLAVE_ID)
    if not rr.isError():
        expect("FC 03 setpoint readback", rr.registers[0] == 750,
               f"got {rr.registers[0]}")

    banner("Write multiple holding registers (FC 16) — exercises range path")
    rr = client.write_registers(address=10,
                                values=[0x1111, 0x2222, 0x3333, 0x4444],
                                device_id=SLAVE_ID)
    failures += not expect("FC 16 write holding[10..13]", not rr.isError())
    rr = client.read_holding_registers(address=10, count=4, device_id=SLAVE_ID)
    if not rr.isError():
        expect("FC 03 holding[10..13] readback",
               rr.registers == [0x1111, 0x2222, 0x3333, 0x4444],
               f"got {rr.registers}")

    banner("Range validation — setpoint clamping (out-of-range write)")
    rr = client.write_register(address=SETPOINT_REG, value=5000, device_id=SLAVE_ID)
    expect("FC 06 setpoint=5000 (will clamp to 1000)", not rr.isError())
    rr = client.read_holding_registers(address=SETPOINT_REG, count=1, device_id=SLAVE_ID)
    if not rr.isError():
        expect("FC 03 setpoint readback (expect clamped 1000)",
               rr.registers[0] == 1000,
               f"got {rr.registers[0]}")

    banner("Coils (FC 01 read, FC 05 write single, FC 0F write multi)")
    rr = client.read_coils(address=0, count=8, device_id=SLAVE_ID)
    failures += not expect("FC 01 read coils[0..7]",
                           not rr.isError(),
                           "" if rr.isError() else f"bits={rr.bits[:8]}")
    rr = client.write_coil(address=0, value=True, device_id=SLAVE_ID)
    failures += not expect("FC 05 write coil[0]=1", not rr.isError())
    rr = client.write_coils(address=8, values=[True, False, True, True, False],
                            device_id=SLAVE_ID)
    failures += not expect("FC 0F write coils[8..12]", not rr.isError())

    banner("Discrete inputs (FC 02) — cross-link from coil[0]")
    rr = client.read_discrete_inputs(address=0, count=4, device_id=SLAVE_ID)
    if not rr.isError():
        expect("FC 02 read discrete[0..3]", True, f"bits={rr.bits[:4]}")
        expect("discrete[0] mirrors coil[0]=1", rr.bits[0] is True,
               f"got {rr.bits[0]}")

    banner("State machine — apply CALIBRATE, observe state in input[3]")
    rr = client.write_register(address=CMD_REG, value=CMD_CALIBRATE, device_id=SLAVE_ID)
    expect("FC 06 cmd=CALIBRATE", not rr.isError())
    time.sleep(0.05)
    rr = client.read_input_registers(address=3, count=1, device_id=SLAVE_ID)
    if not rr.isError():
        expect("input[3] == STATE_CALIBRATION (1)", rr.registers[0] == 1,
               f"got {rr.registers[0]}")

    banner("State machine — apply RESET, observe state back to NORMAL")
    rr = client.write_register(address=CMD_REG, value=CMD_RESET, device_id=SLAVE_ID)
    expect("FC 06 cmd=RESET", not rr.isError())
    time.sleep(0.05)
    rr = client.read_input_registers(address=3, count=1, device_id=SLAVE_ID)
    if not rr.isError():
        expect("input[3] == STATE_NORMAL (0)", rr.registers[0] == 0,
               f"got {rr.registers[0]}")

    banner("Out-of-range address — expect Modbus exception (illegal data addr)")
    rr = client.read_holding_registers(address=999, count=1, device_id=SLAVE_ID)
    expect("FC 03 holding[999] returns ExceptionResponse",
           rr.isError(),
           "no exception" if not rr.isError() else "got expected exception")

    return failures


def main():
    p = argparse.ArgumentParser(description="Modbus RTU slave host driver")
    p.add_argument("--serial", default="/dev/ttyACM0",
                   help="Serial port (real ST-Link VCP or socat'd PTY)")
    p.add_argument("--baud", type=int, default=1200)
    p.add_argument("--timeout", type=float, default=3.0,
                   help="pymodbus per-frame timeout in seconds (1200 baud is slow)")
    p.add_argument("--repeat", type=int, default=1,
                   help="Run the exercise N times back-to-back to grow the trace")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    client = ModbusSerialClient(port=args.serial, baudrate=args.baud,
                                bytesize=8, parity="N", stopbits=1,
                                timeout=args.timeout)
    if not client.connect():
        print(f"failed to open {args.serial}", file=sys.stderr)
        return 2

    total_fail = 0
    try:
        for i in range(args.repeat):
            if args.repeat > 1:
                print(f"\n########## Iteration {i + 1}/{args.repeat} ##########")
            total_fail += exercise(client)
    finally:
        client.close()

    print(f"\nTotal failures: {total_fail}")
    return 0 if total_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
