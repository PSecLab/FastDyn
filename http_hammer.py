import socket
import time
import struct

TARGET_IP = "192.168.8.2"
TARGET_PORT = 80

PATH_LEN = 900
PAD_LEN  = 200

PATH = "/" + ("A" * PATH_LEN)
PAD  = "B" * PAD_LEN

REQ = (
    f"GET {PATH} HTTP/1.1\r\n"
    f"Host: x\r\n"
    f"User-Agent: x\r\n"
    f"X-Fuzz: {PAD}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
).encode()

def one_case():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    #s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    # Optional: RST on close to reduce TIME_WAIT / speed up lwIP cleanup
    # (Try both modes; keep whichever gives higher sustained coverage.)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))

    s.settimeout(0.3)
    try:
        s.connect((TARGET_IP, TARGET_PORT))
        s.sendall(REQ)

        # Read a bit (or until close). Not strictly required, but helps exercise server send path.
        while True:
            try:
                data = s.recv(4096)
                if not data:
                    break
            except socket.timeout:
                break
            except Exception:
                break
    except Exception:
        pass

    try:
        s.shutdown(socket.SHUT_RDWR)
    except Exception:
        pass
    try:
        s.close()
    except Exception:
        pass

i = 0
while True:
    one_case()
    i += 1
    time.sleep(0.003)  # tune; too low may starve lwIP

