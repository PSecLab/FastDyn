import os
import sys
import pty
import subprocess
import time

MAVPROXY_CMD = [
    "mavproxy.py",
    "--master=udpout:127.0.0.1:14551",
    "--map",
    "--console",
]

master_fd, slave_fd = pty.openpty()

proc = subprocess.Popen(
    MAVPROXY_CMD,
    stdin=slave_fd,
    stdout=slave_fd,
    stderr=slave_fd,
    close_fds=True,
)

os.close(slave_fd)

def cleanup():
    proc.terminate()
    proc.wait()
    os.close(master_fd)

def read():
    try:
        while True:
            try:
                data = os.read(master_fd, 1024)
                if not data:
                    break
                line = data.decode(errors="ignore")
                print(line, end="")
                if "Received" in line and "parameters" in line:
                    break
            except OSError:
                break
    except KeyboardInterrupt:
        print("Exiting before parameters set...")
        cleanup()
        sys.exit(0)

def send(cmd):
    os.write(master_fd, (cmd + "\n").encode())

# Allow startup
time.sleep(0.5)

read()

try:
    while True:
        command = input("MAVProxy> ")
        send(command)
        time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    print("Terminating MAVProxy...")
    cleanup()

