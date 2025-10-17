#!/usr/bin/env python3
"""
cvg_monitor.py

Monitor a circular buffer in /tmp/cvg (or any file path) and buffer all new values in memory.
Dump the captured new values to a binary file on demand (timestamp/address not stored).
"""

import mmap
import struct
import time
import os
import argparse

# ---------------------------
# Configuration / defaults
# ---------------------------
DEFAULT_PATH = "/tmp/cvg"
BUF_SIZE = 64 * 1024      # 64 KB circular buffer
WORD_SIZE = 4
FMT = "I"                  # native-endian 32-bit
NUM_WORDS = BUF_SIZE // WORD_SIZE

# ---------------------------
# Command-line parser
# ---------------------------
parser = argparse.ArgumentParser(description="Monitor a circular buffer in shared memory.")
parser.add_argument("--file", type=str, default=DEFAULT_PATH, help="Path to circular buffer file")
parser.add_argument("--dump", type=str, default="trace_log.bin", help="File to dump buffered new values")
args = parser.parse_args()

# ---------------------------
# Globals
# ---------------------------
observed_values = []  # list of new uint32 values only

# ---------------------------
# Helper function to dump
# ---------------------------
def dump_values(filename):
    with open(filename, "wb") as f:
        for val in observed_values:
            f.write(struct.pack(FMT, val))
    print(f"[+] Dumped {len(observed_values)} entries to {filename}")
    observed_values.clear()

# ---------------------------
# Wait for file to exist
# ---------------------------
while not os.path.exists(args.file):
    print(f"[!] Waiting for {args.file} ...")
    time.sleep(0.5)

# ---------------------------
# Memory-map the file
# ---------------------------
with open(args.file, "rb", buffering=0) as f:
    mm = mmap.mmap(f.fileno(), BUF_SIZE, access=mmap.ACCESS_READ)

    last_index = 0
    mm.seek(0)
    last_data = mm.read(BUF_SIZE)
    last_words = list(struct.unpack(f"{NUM_WORDS}{FMT}", last_data))

    print(f"[+] Monitoring {args.file} ({NUM_WORDS} entries) in circular buffer mode")
    print("Press Ctrl+C to stop monitoring and dump buffer.\n")

    try:
        while True:
            # Read the next word in circular buffer
            mm.seek(last_index * WORD_SIZE)
            data = mm.read(WORD_SIZE)
            if len(data) < WORD_SIZE:
                mm.seek(0)
                data = mm.read(WORD_SIZE)

            val = struct.unpack(FMT, data)[0]

            if val != last_words[last_index]:
                observed_values.append(val)
                last_words[last_index] = val

            last_index = (last_index + 1) % NUM_WORDS

    except KeyboardInterrupt:
        print("\n[!] Stopping monitoring...")
        dump_values(args.dump)

