#!/bin/bash
# tail_uint32.sh
# Usage: ./tail_uint32.sh <file> [num]
# Prints the last [num] 32-bit values (default 10) from a binary file in hex

FILE="$1"
NUM="${2:-10}"  # default to 10 if not provided

if [[ ! -f "$FILE" ]]; then
    echo "File not found: $FILE"
    exit 1
fi

hexdump -v -e '1/4 "%08X\n"' "$FILE" 

