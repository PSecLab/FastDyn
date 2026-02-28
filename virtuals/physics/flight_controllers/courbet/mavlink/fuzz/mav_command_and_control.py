import os
import sys
import pty
import subprocess
import time
import threading
import signal

MAVPROXY_CMD = [
    "mavproxy.py",
    "--master=udpout:127.0.0.1:14551",
    "--map",
    "--console",
]

QEMU_CMD = ["/root/rooney/FastDyn/courbet/mavlink/param_fuzzing/start_qemu.sh"]
GAZEBO_CMD = ["/root/rooney/FastDyn/courbet/mavlink/param_fuzzing/start_gazebo.sh"]

master_fd = None
slave_fd = None

mavproxy_proc = None
qemu_proc = None
gazebo_proc = None

_connected = False
_lock = threading.Lock()


# ---------- MAVPROXY ----------
def start_mavproxy():
    global master_fd, slave_fd, mavproxy_proc

    master_fd, slave_fd = pty.openpty()

    mavproxy_proc = subprocess.Popen(
        MAVPROXY_CMD,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        preexec_fn=os.setsid,  # new PGID
        close_fds=True,
    )

    os.close(slave_fd)
    threading.Thread(target=_reader_thread, daemon=True).start()


def _reader_thread():
    global _connected
    while True:
        try:
            data = os.read(master_fd, 1024)
            if not data:
                break

            text = data.decode(errors="ignore")
            print(text, end="")

            if "received" in text.lower() and "parameters" in text.lower():
                with _lock:
                    _connected = True

        except OSError:
            break


def is_connected():
    with _lock:
        return _connected


def send(cmd: str):
    os.write(master_fd, (cmd + "\n").encode())


def send_fuzzed_input(payload: str):
    if not is_connected():
        raise RuntimeError("MAVProxy session not connected")
    send(payload)


# ---------- GAZEBO / QEMU ----------
def start_gazebo():
    global gazebo_proc
    gazebo_proc = subprocess.Popen(
        GAZEBO_CMD,
        stdin=subprocess.DEVNULL,
        stdout=sys.stdout,
        stderr=sys.stderr,
        preexec_fn=os.setsid,
    )


def start_qemu():
    global qemu_proc
    qemu_proc = subprocess.Popen(
        QEMU_CMD,
        stdin=subprocess.DEVNULL,
        stdout=sys.stdout,
        stderr=sys.stderr,
        preexec_fn=os.setsid,
    )


# ---------- CLEANUP ----------
def cleanup(signum=None, frame=None):
    print("\n[+] Shutting down all processes...")

    for proc in [mavproxy_proc, gazebo_proc, qemu_proc]:
        if proc and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    time.sleep(0.5)

    for proc in [mavproxy_proc, gazebo_proc, qemu_proc]:
        if proc and proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    if master_fd:
        os.close(master_fd)

    sys.exit(0)


# ---------- MAIN ----------
if __name__ == "__main__":
    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    print("[+] Starting MAVProxy...")
    start_mavproxy()
    time.sleep(1.0)

    print("[+] Starting Gazebo...")
    start_gazebo()
    time.sleep(1.0)

    print("[+] Starting QEMU...")
    start_qemu()
    time.sleep(1.0)

    # Only MAVProxy gets input
    try:
        while True:
            cmd = input("MAVProxy> ")
            send(cmd)
    except KeyboardInterrupt:
        cleanup()
