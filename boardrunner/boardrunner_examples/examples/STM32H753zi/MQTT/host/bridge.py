#!/usr/bin/env python3
"""
Serial <-> MQTT broker (TCP) bridge for the NUCLEO-H753ZI MQTT firmware.

The firmware speaks raw MQTT wire format over USART3 (ST-Link VCP).
This script is a dumb byte-forwarder between the serial port and the broker
TCP socket - no MQTT parsing on this side. That keeps the parser on the MCU
as the sole fuzzing target.

Usage:
    python3 bridge.py [--serial /dev/ttyACM0] [--baud 115200]
                      [--broker localhost] [--port 1883]
"""

import argparse
import select
import socket
import sys

try:
    import serial
except ImportError:
    sys.stderr.write("pyserial missing: pip install pyserial\n")
    sys.exit(1)


def run(serial_port, baud, broker, broker_port):
    ser = serial.Serial(serial_port, baud, timeout=0)
    sock = socket.create_connection((broker, broker_port))
    sock.setblocking(False)
    print(f"[bridge] {serial_port}@{baud} <-> {broker}:{broker_port}", flush=True)

    try:
        while True:
            r, _, _ = select.select([ser.fileno(), sock.fileno()], [], [], 1.0)
            if ser.fileno() in r:
                data = ser.read(4096)
                if data:
                    sock.sendall(data)
            if sock.fileno() in r:
                try:
                    data = sock.recv(4096)
                except BlockingIOError:
                    data = b""
                if data == b"":
                    print("[bridge] broker closed", flush=True)
                    break
                ser.write(data)
    finally:
        ser.close()
        sock.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--serial", default="/dev/ttyACM0")
    p.add_argument("--baud",   default=115200, type=int)
    p.add_argument("--broker", default="localhost")
    p.add_argument("--port",   default=1883, type=int)
    args = p.parse_args()
    run(args.serial, args.baud, args.broker, args.port)
