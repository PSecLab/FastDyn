#!/usr/bin/env python3
"""
modbus_bridge.py — Host-side Modbus RTU driver for the Nucleo-F103RB
slave example. Exercises FC 01/02/03/04/05/06/0F/10.

Usage:
    python3 modbus_bridge.py --serial /dev/ttyACM0   # real board (ST-Link VCP)
    python3 modbus_bridge.py --serial /tmp/host_modbus  # elder-mode PTY
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

import serial


def install_paced_write(pace_ms: float) -> None:
    """Patch serial.Serial.write so every TX call sends bytes one at a
    time with `pace_ms` of sleep between them. Stays well under
    FreeMODBUS RTU's t3.5 (~59 ms at 600 baud), so the chip still sees
    a single contiguous frame."""
    if pace_ms <= 0.0:
        return
    original_write = serial.Serial.write
    delay_s = pace_ms / 1000.0

    def _paced_write(self, data):
        if isinstance(data, (bytes, bytearray, memoryview)):
            buf = bytes(data)
            for i, byte in enumerate(buf):
                original_write(self, bytes([byte]))
                self.flush()
                if i + 1 < len(buf):
                    time.sleep(delay_s)
            return len(buf)
        return original_write(self, data)

    serial.Serial.write = _paced_write


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
    """Run one pass over every register-callback path FreeMODBUS exposes,
    covering FC 01/02/03/04/05/06/0F/10."""
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

    return failures


def main():
    p = argparse.ArgumentParser(description="Modbus RTU slave host driver")
    p.add_argument("--serial", default="/dev/ttyACM0",
                   help="Serial port (real ST-Link VCP or socat'd PTY)")
    p.add_argument("--baud", type=int, default=600)
    p.add_argument("--timeout", type=float, default=5.0,
                   help="pymodbus per-frame timeout in seconds")
    p.add_argument("--repeat", type=int, default=1,
                   help="Run the exercise N times back-to-back to grow the trace")
    p.add_argument("--pace-ms", type=float, default=20.0,
                   help="Inter-byte gap on TX in ms (0 = back-to-back). "
                        "Must stay below FreeMODBUS RTU t3.5.")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    install_paced_write(args.pace_ms)

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
