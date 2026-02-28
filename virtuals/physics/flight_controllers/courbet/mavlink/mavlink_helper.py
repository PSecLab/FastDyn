#!/usr/bin/env python3
from pymavlink import mavutil
import re

# Path to your log file
log_file = "mavlink_received.log"

# Regex to extract the message ID
msgid_re = re.compile(r'Message ID: (\d+)')

# Set to store unique message names
unique_messages = set()

# Helper function to get message name
# def get_msg_name(msgid):
#     try:
#         # Returns a string like "HEARTBEAT", "SYS_STATUS", etc.
#         return mavutil.mavlink.get_msg_name(msgid)
#     except ValueError:
#         return f"UNKNOWN ({msgid})"

with open(log_file, 'r') as f:
    for line in f:
        match = msgid_re.search(line)
        if match:
            msgid = int(match.group(1))
            # msg_name = get_msg_name(msgid)
            filler = "filler"
            print(f"Message ID: {msgid}, Name: {filler}")
            unique_messages.add(msgid)

# Print all unique message names sorted
print("\nUnique message types:")
for name in sorted(unique_messages):
    print(name)