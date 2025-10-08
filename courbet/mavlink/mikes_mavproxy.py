import socket
import time
import json
import threading
from pymavlink.dialects.v20 import common as mavlink

LOG_FILE = "mavlink_gcs.log"
FIRST_LOG = True

def log(msg):
    global FIRST_LOG
    if FIRST_LOG:
        with open(LOG_FILE, "w") as f:
            f.write(msg + "\n")
        FIRST_LOG = False
    else:
        with open(LOG_FILE, "a") as f:
            f.write(msg + "\n")

def request_data_streams(mav, target_system, target_component):
    stream_ids = [
        mavlink.MAV_DATA_STREAM_POSITION,
        mavlink.MAV_DATA_STREAM_EXTRA1,
        mavlink.MAV_DATA_STREAM_EXTRA2,
        mavlink.MAV_DATA_STREAM_EXTENDED_STATUS,
    ]
    for stream_id in stream_ids:
        req = mavlink.MAVLink_request_data_stream_message(
            target_system=target_system,
            target_component=target_component,
            req_stream_id=stream_id,
            req_message_rate=2,
            start_stop=1
        )
        log(f"[SEND][GCS → Drone] REQUEST_DATA_STREAM {stream_id}")
        mav.send(req)

def send_rc_channel_overrides(mav, target_system, target_component, channel_index, value):
    if channel_index < 0 or channel_index > 16:
        raise ValueError("Channel index must be between 0 and 16")

    channels = [65535] * 18  # MAVLink RC_CHANNELS_OVERRIDE has 18 channels (0-17)

    channels[channel_index] = int(value)

    # Ensure the target system and component are set
    if target_system is None or target_component is None:
        raise ValueError("Target system and component must be set before sending RC overrides")

    # Send the override message
    msg = mavlink.MAVLink_rc_channels_override_message(
        target_system,
        target_component,
        chan1_raw=channels[0],
        chan2_raw=channels[1],
        chan3_raw=channels[2],
        chan4_raw=channels[3],
        chan5_raw=channels[4],
        chan6_raw=channels[5],
        chan7_raw=channels[6],
        chan8_raw=channels[7],
        chan9_raw=channels[8]
    )
    log(f"[SEND][GCS → Drone] RC_CHANNELS_OVERRIDE: channel {channel_index} set to {value}")
    mav.send(msg)

def send_arm_command(mav, target_system, target_component, arm=True):
    if target_system is None or target_component is None:
        raise ValueError("Target system and component must be set before arming/disarming")

    msg = mavlink.MAVLink_command_long_message(
        target_system=target_system,
        target_component=target_component,
        command=mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
        confirmation=0,
        param1=1.0 if arm else 0.0,  # 1 to arm, 0 to disarm
        param2=0, param3=0, param4=0, param5=0, param6=0, param7=0
    )

    action = "ARM" if arm else "DISARM"
    log(f"[SEND][GCS → Drone] COMMAND_LONG: {action}")
    mav.send(msg)

def load_mission_from_json(filename):
    mission = []
    try:
        with open(filename, 'r') as f:
            mission_data = json.load(f)
    except Exception as e:
        print(f"[ERROR] Failed to load mission file {filename}: {e}")
        return []

    for item in mission_data:
        mission_item = mavlink.MAVLink_mission_item_message(
            target_system=0,
            target_component=0,
            seq=int(item['seq']),
            frame=int(item['frame']),
            command=int(item['command']),
            current=int(item['current']),
            autocontinue=int(item['autocontinue']),
            param1=float(item['param1']),
            param2=float(item['param2']),
            param3=float(item['param3']),
            param4=float(item['param4']),
            x=float(item['x']),
            y=float(item['y']),
            z=float(item['z'])
        )
        mission.append(mission_item)
    print(f"[INFO] Loaded {len(mission)} mission items from {filename}")
    return mission

class MAVLinkGCS:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 14550))
        self.sock.setblocking(False)

        class SocketWrapper:
            def __init__(self, sock):
                self.sock = sock
            def read(self, n=1024):
                try:
                    return self.sock.recv(n)
                except BlockingIOError:
                    return b''
            def write(self, data):
                return self.sock.sendto(data, ('127.0.0.1', 14552))

        self.mav = mavlink.MAVLink(SocketWrapper(self.sock))
        self.mav.srcSystem = 255

        self.buf = b''
        self.target_system = 1
        self.target_component = 0
        self.param_value_received = False
        self.requested_streams = False
        self.last_heartbeat_sent_time = 0

        self.mission = []
        self.mission_state = {
            'upload_in_progress': False,
            'seq_to_send': 0
        }

        self.running = True

    def process_incoming(self):
        try:
            data, _ = self.sock.recvfrom(4096)
            self.buf += data
        except BlockingIOError:
            pass

        while True:
            try:
                msg = self.mav.parse_char(self.buf[:1])
                self.buf = self.buf[1:]
            except mavlink.MAVError:
                self.buf = self.buf[1:]
                continue
            if not msg:
                break

            log(f"[RECV][Drone → GCS] {msg.get_type()} {msg.to_dict()}")

            if msg.get_type() == "HEARTBEAT":
                self.target_system = msg.get_srcSystem()
                self.target_component = msg.get_srcComponent()
                log(f"Connected to system {self.target_system}, component {self.target_component}")

                # Send GCS heartbeat immediately upon receiving vehicle heartbeat
                hb = self.mav.heartbeat_encode(
                    mavlink.MAV_TYPE_GCS,
                    mavlink.MAV_AUTOPILOT_INVALID,
                    0, 0,
                    mavlink.MAV_STATE_ACTIVE
                )
                log(f"[SEND][GCS → Drone] HEARTBEAT")
                self.mav.send(hb)

                # Send PARAM_REQUEST_LIST only if we haven't received PARAM_VALUE yet
                if not self.param_value_received:
                    param_req = self.mav.param_request_list_encode(
                        self.target_system,
                        self.target_component
                    )
                    log(f"[SEND][GCS → Drone] PARAM_REQUEST_LIST")
                    self.mav.send(param_req)

                if not self.requested_streams:
                    request_data_streams(self.mav, self.target_system, self.target_component)
                    self.requested_streams = True

                if self.mission_state['upload_in_progress'] and not self.mission:
                    # No mission loaded but upload flagged - cancel upload
                    self.mission_state['upload_in_progress'] = False
                    log("[WARN] Upload flagged but no mission loaded.")

                if self.mission_state['upload_in_progress']:
                    # Start mission upload by sending MISSION_COUNT
                    self.mission_state['seq_to_send'] = 0
                    mission_count_msg = self.mav.mission_count_encode(
                        self.target_system,
                        self.target_component,
                        len(self.mission)
                    )
                    log(f"[SEND][GCS → Drone] MISSION_COUNT {len(self.mission)}")
                    self.mav.send(mission_count_msg)

            elif msg.get_type() == "TIMESYNC":
                # If tc1 != 0, send response with tc1=0, ts1=received tc1
                if msg.tc1 != 0:
                    timesync_resp = self.mav.timesync_encode(
                        tc1=0,
                        ts1=msg.tc1
                    )
                    log(f"[SEND][GCS → Drone] TIMESYNC response tc1=0 ts1={msg.tc1}")
                    self.mav.send(timesync_resp)
                continue

            elif msg.get_type() == "PARAM_VALUE":
                if not self.param_value_received:
                    log("[INFO] Received PARAM_VALUE. Stopping PARAM_REQUEST_LIST.")
                self.param_value_received = True

            elif msg.get_type() == "MISSION_REQUEST":
                seq = msg.seq
                log(f"[RECV] MISSION_REQUEST for seq {seq}")

                if self.mission_state['upload_in_progress'] and seq < len(self.mission):
                    item = self.mission[seq]
                    item.target_system = self.target_system
                    item.target_component = self.target_component
                    log(f"[SEND][GCS → Drone] MISSION_ITEM seq {seq}")
                    self.mav.send(item)
                else:
                    log(f"[WARN] Received MISSION_REQUEST for invalid seq {seq}")

            elif msg.get_type() == "MISSION_ACK":
                log("[RECV] MISSION_ACK received from vehicle")
                self.mission_state['upload_in_progress'] = False
                print("[INFO] Mission upload complete!")

    def send_heartbeat_and_params(self):
        pass
        # now = time.time()
        # if self.target_system is not None and now - self.last_heartbeat_sent_time > 1.0:
        #     self.last_heartbeat_sent_time = now

        #     hb = self.mav.heartbeat_encode(
        #         mavlink.MAV_TYPE_GCS,
        #         mavlink.MAV_AUTOPILOT_INVALID,
        #         0, 0,
        #         mavlink.MAV_STATE_ACTIVE
        #     )
        #     log(f"[SEND][GCS → Drone] HEARTBEAT")
        #     self.mav.send(hb)

        #     if not self.param_value_received:
        #         param_req = self.mav.param_request_list_encode(
        #             self.target_system,
        #             self.target_component
        #         )
        #         log(f"[SEND][GCS → Drone] PARAM_REQUEST_LIST")
        #         self.mav.send(param_req)

    def run(self):
        while self.running:
            self.process_incoming()
            self.send_heartbeat_and_params()
            time.sleep(0.05)

    def upload_mission(self, filename):
        mission = load_mission_from_json(filename)
        if mission:
            self.mission = mission
            self.mission_state['upload_in_progress'] = True
            print(f"[INFO] Mission upload started from file {filename}")
        else:
            print(f"[ERROR] Mission upload failed, no mission loaded from {filename}")

def cli_loop(gcs: MAVLinkGCS):
    while True:
        try:
            command = input("(Mike's MavProxy)> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not command:
            continue

        parts = command.split()

        # Handle 'mission upload <filename>'
        if len(parts) >= 2 and parts[0].lower() == "mission" and parts[1].lower() == "upload":
            if len(parts) < 3:
                print("[ERROR] Missing filename for mission upload command.")
                continue
            filename = " ".join(parts[2:]).strip()
            if not filename:
                print("[ERROR] Empty filename provided for mission upload command.")
                continue
            gcs.upload_mission(filename)

        # If user just types 'mission' or 'mission something_else' that's invalid here
        elif parts[0].lower() == "mission":
            print(f"[ERROR] Unknown or incomplete 'mission' command: {command}")
        # Handle 'rc override <channel> <value>'
        elif parts[0].lower() == "rc":
            if len(parts) != 3:
                print("[ERROR] Invalid RC override command. Usage: rc override <channel> <value>")
                continue
            try:
                channel_index = int(parts[1])
                value = int(parts[2])
                send_rc_channel_overrides(gcs.mav, gcs.target_system, gcs.target_component, channel_index, value)
            except ValueError as e:
                print(f"[ERROR] Invalid channel or value: {e}")
        # Handle 'arm' and 'disarm' commands
        elif parts[0].lower() == "arm":
            try:
                send_arm_command(gcs.mav, gcs.target_system, gcs.target_component, arm=True)
                print("[INFO] Sent ARM command to vehicle")
            except ValueError as e:
                print(f"[ERROR] {e}")

        elif parts[0].lower() == "disarm":
            try:
                send_arm_command(gcs.mav, gcs.target_system, gcs.target_component, arm=False)
                print("[INFO] Sent DISARM command to vehicle")
            except ValueError as e:
                print(f"[ERROR] {e}")
        else:
            print(f"[WARN] Unknown command: {command}")


if __name__ == "__main__":
    gcs = MAVLinkGCS()

    # Run MAVLink processing loop in background thread
    thread = threading.Thread(target=gcs.run, daemon=True)
    thread.start()

    # Run CLI in main thread
    try:
        cli_loop(gcs)
    except KeyboardInterrupt:
        pass

    gcs.running = False
    thread.join()
