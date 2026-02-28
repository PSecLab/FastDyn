import os
import random
import socket
import struct
import time

from pymavlink.generator.mavcrc import x25crc

TARGET_IP = "127.0.0.1"
TARGET_PORT = 14550

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def rand_u8():
    return random.randint(0, 255)


'''
HardFault_Handler assert *0
_ZN6AP_HAL5panicEPKcz assert *0
abort assert *0
0x8089e84 assert *0x08089d7c
0x8089d84 anchor 0:0xFFFFFFFF
'''

def fuzz_once():
    stx = 0xFD

    payload_len = random.randint(0, 255)
    payload = os.urandom(payload_len)

    incompat_flags = rand_u8()
    compat_flags   = rand_u8()
    seq            = rand_u8()
    sysid          = rand_u8()
    compid         = rand_u8()

    msgid = random.randint(0, 0xFFFFFF)

    header = struct.pack(
        "<BBBBBBI",
        payload_len,
        incompat_flags,
        compat_flags,
        seq,
        sysid,
        compid,
        msgid
    )[:9]  # msgid is 3 bytes

    packet = bytes([stx]) + header + payload

    # CRC over header (minus STX) + payload
    crc = x25crc(packet[1:])
    crc_bytes = struct.pack("<H", crc.crc)

    full_packet = packet + crc_bytes

    sock.sendto(full_packet, (TARGET_IP, TARGET_PORT))

if __name__ == "__main__":
    while True:
        fuzz_once()
        time.sleep(0.001)
