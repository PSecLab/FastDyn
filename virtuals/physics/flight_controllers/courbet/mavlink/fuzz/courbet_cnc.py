import os
import sys
import pty
import subprocess
import time
import threading

MAVPROXY_CMD = [
    "mavproxy.py",
    "--master=udpout:127.0.0.1:14551",
    "--map",
    "--console",
]

master_fd = None
slave_fd = None
proc = None

_connected = False
_lock = threading.Lock()


def start_mavproxy():
    global master_fd, slave_fd, proc

    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        MAVPROXY_CMD,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
    )

    os.close(slave_fd)

    threading.Thread(target=_reader_thread, daemon=True).start()


def cleanup():
    global master_fd, proc
    if proc:
        proc.terminate()
        proc.wait()
    if master_fd:
        os.close(master_fd)


def _reader_thread():
    """Continuously read MAVProxy output and detect connection."""
    global _connected

    while True:
        try:
            data = os.read(master_fd, 1024)
            if not data:
                break

            text = data.decode(errors="ignore")
            print(text, end="")

            # Connection heuristics
            if (
                "received" in text.lower() and "parameters" in text.lower()
                # or "heartbeat" in text.lower()
                # or "detected vehicle" in text.lower()
            ):
                with _lock:
                    _connected = True

        except OSError:
            break


def is_connected() -> bool:
    with _lock:
        return _connected


def send(cmd: str):
    os.write(master_fd, (cmd + "\n").encode())


# 🔥 THIS IS WHAT YOU WANT
def send_fuzzed_input(payload: str):
    """
    Send fuzzed input into MAVProxy.

    Raises:
        RuntimeError: if MAVProxy is not connected yet
    """
    if not is_connected():
        raise RuntimeError("MAVProxy session not connected")

    send(payload)


# ---- interactive mode (optional) ----
if __name__ == "__main__":
    start_mavproxy()
    time.sleep(0.5)

    try:
        while True:
            cmd = input("MAVProxy> ")
            send(cmd)
    except KeyboardInterrupt:
        pass
    finally:
        cleanup()

