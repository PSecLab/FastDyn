from pymavlink import mavutil

mav = mavutil.mavlink_connection("udp:127.0.0.1:14551")
mav.wait_heartbeat()

print("Connected")

while True:
    msg = mav.recv_match(type='STATUSTEXT', blocking=True)
    if msg:
        text = msg.text.decode('utf-8', errors='ignore')
        severity = msg.severity
        print(f"STATUSTEXT[{severity}]: {text}")

