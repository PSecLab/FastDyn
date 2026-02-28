import socket
from pymavlink.dialects.v20 import common as mavlink2

LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 14550

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, LISTEN_PORT))

print(f"[+] Listening on UDP {LISTEN_IP}:{LISTEN_PORT}")

mav = mavlink2.MAVLink(None)
mav.robust_parsing = True   # IMPORTANT: don't die on malformed packets

while True:
    data, addr = sock.recvfrom(4096)

    print(f"\n[>] {len(data)} bytes from {addr}")

    for b in data:
        try:
            msg = mav.parse_char(bytes([b]))
            if msg:
                print(
                    f"[✓] Parsed message | "
                    f"sysid={msg.get_srcSystem()} "
                    f"compid={msg.get_srcComponent()} "
                    f"msgid={msg.get_msgId()} "
                    f"len={len(msg.get_payload())}"
                )
        except Exception as e:
            print(f"[!] Parser exception: {e}")
