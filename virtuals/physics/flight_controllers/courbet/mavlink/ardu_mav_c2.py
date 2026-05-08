import os
import sys
import pty
import subprocess
import time
from pymavlink import mavutil
import struct
import signal
import socket

MAVPROXY_CMD = [
    "mavproxy.py",
    f"--master=udpout:127.0.0.1:{os.environ.get('FASTDYN_MAVLINK_FIRMWARE_PORT', '14551')}",
    f"--out=udpout:127.0.0.1:{os.environ.get('FASTDYN_MAVLINK_GCS_PORT', '14552')}",
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
def read_until(master_fd, proc, key, timeout=sys.maxsize, file=None):

    start_time = time.perf_counter()
    timeout_time = start_time + timeout
    down_counter = 0

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
                elif "no link" in line:
                    down_counter += 1
                    print("No link message received. Count:", down_counter)
                    if down_counter >= 3:
                        print("Link down threshold reached. Exiting.")
                        sys.exit(1)

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
    usage += " /path/to/mission.txt/ comma,separated,parameter,names param_input_delay"
    usage += " /path/to/param/bytes.bin [headless]"

    if len(sys.argv) < 5:
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
    if len(param_name_list) == 1 and param_name_list[0] == "":
        param_name_list = []

    param_name_list = [
        # "BATT_AMP_OFFSET",
        # "BATT_AMP_PERVLT",
        # "COMPASS_DIA2_Z",
        # "COMPASS_DIA3_Z",
        # "SCHED_LOOP_RATE",
        # "SERIAL0_BAUD",
        # "SERIAL1_BAUD",
        # "SERIAL2_BAUD",
        # "SERIAL3_BAUD",
        # "SERIAL4_BAUD",
        # "SERIAL5_BAUD",
        # "SERIAL6_BAUD",
        # "SERIAL7_BAUD",
        # "STAT_FLTTIME",
        # "ARMING_MIS_ITEMS",
        # "TELEM_DELAY",
        # "LOG_BITMASK",
        # "STAT_RUNTIME",
        # "STAT_RESET",
        # "ARMING_CHECK",
        # "FS_OPTIONS",
        # "INS_ACCSCAL_X",
        # "INS_ACCSCAL_Y",
        # "INS_POS2_X",
        # "INS_POS2_Y",
        # "INS_POS2_Z",
        # "COMPASS_DEV_ID2",
        # "COMPASS_OFS3_X",
        # "COMPASS_OFS3_Y",
        # "COMPASS_OFS3_Z",
        # "COMPASS_OFS_X",
        # "COMPASS_OFS_Y",
        # "COMPASS_OFS_Z",
        # "COMPASS_OFS2_X",
        # "COMPASS_OFS2_Y",
        # "COMPASS_OFS2_Z",
        # "BRD_OPTIONS",
        "AHRS_TRIM_X",
        # "AHRS_TRIM_Y",
        # "AHRS_TRIM_Z",
        # "INS_ACC2SCAL_Z",
        # "INS_ACC2OFFS_Z",
        # "SERVO_ROB_POSMIN",
        # "BATT_CAPACITY",
        # "RC_OPTIONS",
        # "INS_ACC_ID",
        # "INS_ACC2_ID",
        # "BATT_ARM_MAH",
        # "SERVO_VOLZ_MASK",
        # "INS_GYR_ID",
        # "BATT_SERIAL_NUM",
        # "SERVO_ROB_POSMAX",
    ]

    param_input_delay = float(sys.argv[4])
    if param_input_delay < 0:
        print("Parameter input delay must be non-negative.")
        exit(1)

    param_bytes_path = sys.argv[5]
    if not os.path.isfile(param_bytes_path):
        print(f"Set parameter file {param_bytes_path} does not exist.")
        exit(1)

    if not len(sys.argv) > 6:
        MAVPROXY_CMD.append("--map")
        MAVPROXY_CMD.append("--console")
    elif sys.argv[6] != "headless":
        print("Invalid sixth argument. Use 'headless' or leave empty.")
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

    mav = mavutil.mavlink_connection(
        f"udp:127.0.0.1:{os.environ.get('FASTDYN_MAVLINK_GCS_PORT', '14552')}"
    )
    mav.wait_heartbeat()

    print("Connected")

    # Wait for initialization
    read_until(master_fd, proc, "Received")

    # Load initial parameters
    send(master_fd, f"param load {init_param_file_path}")
    read_until(master_fd, proc, f"parameters from {init_param_file_path}")
    
    # Wait for EKF3 active
    print("Waiting for EKF3 to become active...")
    while True:
        mav_msg = mav.recv_match(type='STATUSTEXT', blocking=True)
        if mav_msg and "EKF3 active" in mav_msg.text:
            break

    # Load mission and set AUTO
    send(master_fd, f"wp load {mission_file_path}")
    read_until(master_fd, proc, f"waypoints in")
    send(master_fd, "mode auto")

    send(master_fd, "arm throttle")

    # Check if arming check failed.
    # If so, we'll report a timeout in mission executor.
    mav_msg = mav.recv_match(type='STATUSTEXT', blocking=True, timeout=3)
    if mav_msg and "AP: PreArm" in mav_msg.text and mav_msg.severity >= mavutil.mavlink.MAV_SEVERITY_WARNING:
        print("Arming check failed:", mav_msg.text)
        cleanup(master_fd, proc)
        sys.exit(1)

    SOCKET_PATH = os.environ.get("FASTDYN_OPTIFUZZ_SOCKET", "/tmp/rust_receiver.sock")

    message = "throttle armed"

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCKET_PATH)
    sock.sendall(message.encode("utf-8"))
    sock.close()

    print("Python sent one message and exited")

    # Wait for the post-arm delay and send parameters
    # time.sleep(param_input_delay)
    # if len(param_name_list) > 0:
    #     bin_file = open(param_bytes_path, "rb")
    #     count = 1
    #     for param in param_name_list:
    #         # bytes = bin_file.read(4).ljust(4, b'\x00')
    #         # print(f"Bytes for param {param}: {bytes}")
    #         # param_set(mav, param, struct.unpack("f", bytes)[0])
    #         print(f"{count}: Setting param {param} to 1e15...")
    #         param_set(mav, param, 1e15)
    #         time.sleep(0.5)
    #         count += 1

    # Monitor mavlink until mission complete
    mission_success = False
    print("Reading MAVLink messages...")
    while True:
        mav_msg = mav.recv_match(type="MISSION_CURRENT", blocking=True)
        if mav_msg:
            if mav_msg.mission_state == MISSION_COMPLETE:
                mission_success = True
                break
            # elif mav_msg.mission_state != MISSION_IN_PROGRESS:
            #     # Something bad happened
            #     break

    # heartbeat_count = 1
    # while True:
    #     mav.wait_heartbeat()
    #     print(f"Heartbeat {heartbeat_count}...")
    #     heartbeat_count += 1

    print("Terminating MAVProxy...")
    cleanup(master_fd, proc)

    # Exit with success or failure for the mission completion observer in mission_execution.rs
    sys.exit(0 if mission_success else 1)

if __name__ == "__main__":
    main()
