import os
import sys
import pty
import subprocess
import time
from pymavlink import mavutil
import struct
import signal

MAVPROXY_CMD = [
    "mavproxy.py",
    "--master=udpout:127.0.0.1:14551",
    "--out=udpout:127.0.0.1:14552",
    "--map",
    "--console",
]

MISSION_IN_PROGRESS = 3
MISSION_COMPLETE = 5

def param_set(mav, name: str, value: float, param_type=mavutil.mavlink.MAV_PARAM_TYPE_REAL32):
    mav.mav.param_set_send(
        mav.target_system,
        mav.target_component,
        name.encode("ascii"),
        value,
        param_type
    )

def cleanup(master_fd, proc):
    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    proc.wait()
    os.close(master_fd)

# Read from the master fd until the specified key is found or timeout occurs
def read_until(master_fd, proc, key, timeout=sys.maxsize):

    start_time = time.perf_counter()
    timeout_time = start_time + timeout

    try:

        while True:

            if time.perf_counter() > timeout_time:
                print("Timeout waiting for key:", key)
                break

            try:
                data = os.read(master_fd, 1024)
                if not data:
                    break
                line = data.decode(errors="ignore")
                print(line, end="")
                if key in line:
                    print("Found key:", key)
                    break

            except OSError:
                break

    except KeyboardInterrupt:
        print("Exiting before parameters set...")
        cleanup(master_fd, proc)
        sys.exit(0)

def send(master_fd, cmd):
    os.write(master_fd, (cmd + "\n").encode())

def main():

    usage = "Usage: python3 ardu_mav_c2.py /path/to/init/params.txt"
    usage += " /path/to/mission.txt/ comma,separated,parameter,names /path/to/param/bytes.bin"
    if len(sys.argv) < 2:
        print(usage)
        exit(1)
    
    init_param_file_path = sys.argv[1]
    if not os.path.isfile(init_param_file_path):
        print(f"Parameter file {init_param_file_path} does not exist.")
        exit(1)

    mission_file_path = sys.argv[2]
    if not os.path.isfile(mission_file_path):
        print(f"Mission file {mission_file_path} does not exist.")
        exit(1)

    param_name_list = sys.argv[3][:-1].split(",") # Removes trailing comma before split
    if len(param_name_list) == 0:
        print("No parameter names provided.")
        exit(1)

    param_bytes_path = sys.argv[4]
    if not os.path.isfile(param_bytes_path):
        print(f"Set parameter file {param_bytes_path} does not exist.")
        exit(1)
    
    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        MAVPROXY_CMD,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
        preexec_fn=os.setsid
    )

    os.close(slave_fd)

    # Allow startup
    time.sleep(0.5)

    mav = mavutil.mavlink_connection("udp:127.0.0.1:14552")
    mav.wait_heartbeat()

    print("Connected")

    # Wait for initialization
    read_until(master_fd, proc, "Received")

    # Load initial parameters
    send(master_fd, f"param load {init_param_file_path}")
    read_until(master_fd, proc, f"parameters from {init_param_file_path}")
    
    # Wait for EKF3 active
    while True:
        mav_msg = mav.recv_match(type='STATUSTEXT', blocking=True)
        if mav_msg and "EKF3 active" in mav_msg.text:
            break

    # Load mission and set AUTO
    send(master_fd, f"wp load {mission_file_path}")
    read_until(master_fd, proc, f"waypoints in")
    send(master_fd, "mode auto")

    # Start sending over the mutated parameters
    bin_file = open(param_bytes_path, "rb")
    for param in param_name_list:
        bytes = bin_file.read(4).ljust(4, b'\x00')
        print("Bytes for param", param, ":", bytes)
        param_set(mav, param, struct.unpack("f", bytes)[0])
        time.sleep(0.5)

    send(master_fd, "arm throttle")
    # read_until(master_fd, proc, "Throttle armed")
    
    # while True:
    #     mav_msg = mav.recv_match(type='MISSION_CURRENT', blocking=True)
    #     if mav_msg and mav_msg.mission_state == MISSION_IN_PROGRESS:
    #         break

    # # Start sending over the mutated parameters
    # bin_file = open(param_bytes_path, "rb")
    # for param in param_name_list:
    #     bytes = bin_file.read(4).ljust(4, b'\x00')
    #     print("Bytes for param", param, ":", bytes)
    #     param_set(mav, param, struct.unpack("f", bytes)[0])
    #     time.sleep(0.5)

    # Check if arming check failed.
    # If so, we'll report a timeout in mission executor.
    mav_msg = mav.recv_match(type='STATUSTEXT', blocking=True, timeout=3)
    if mav_msg and "AP: PreArm" in mav_msg.text and mav_msg.severity >= mavutil.mavlink.MAV_SEVERITY_WARNING:
        print("Arming check failed:", mav_msg.text)
        cleanup(master_fd, proc)
        sys.exit(1)

    # Monitor mavlink until mission complete
    mission_success = False
    print("Reading MAVLink messages...")
    while True:
        mav_msg = mav.recv_match(type="MISSION_CURRENT", blocking=True)
        if mav_msg:
            if mav_msg.mission_state == MISSION_COMPLETE:
                mission_success = True
                break
            elif mav_msg.mission_state != MISSION_IN_PROGRESS:
                # Something bad happened
                break


    print("Terminating MAVProxy...")
    cleanup(master_fd, proc)

    # Exit with success or failure for the mission completion observer in mission_execution.rs
    sys.exit(0 if mission_success else 1)

if __name__ == "__main__":
    main()