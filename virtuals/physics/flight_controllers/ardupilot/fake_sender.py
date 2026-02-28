#!/usr/bin/env python3
import time
import socket
from pymavlink import mavutil

# Target UDP address (your receiver)
UDP_IP = "127.0.0.1"
UDP_PORT = 14552

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Create a MAVLink connection object (no device, just for message generation)
mav = mavutil.mavlink.MAVLink(None)
mav.srcSystem = 255  # GCS system ID
mav.srcComponent = 190

print(f"Sending MAVLink heartbeats to {UDP_IP}:{UDP_PORT}...")

while True:
    # Create heartbeat message
    hb = mav.heartbeat_encode(
        type=mavutil.mavlink.MAV_TYPE_GCS,      # MAV type: GCS
        autopilot=mavutil.mavlink.MAV_AUTOPILOT_INVALID,
        base_mode=0,
        custom_mode=0,
        system_status=mavutil.mavlink.MAV_STATE_ACTIVE
    )

    # Pack message to bytes
    msg_bytes = hb.pack(mav)

    # Send over UDP
    sock.sendto(msg_bytes, (UDP_IP, UDP_PORT))

    print("Heartbeat sent")
    time.sleep(1)  # send 1 Hz