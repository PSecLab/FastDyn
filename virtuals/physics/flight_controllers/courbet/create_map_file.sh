

FIRMWARE=$1
OUTPUT_MAP_FILE=$2

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <firmware.elf> <output_map_file.txt>"
    exit 1
fi

arm-none-eabi-objdump -t "$FIRMWARE" \
| awk '$1 ~ /^[0-9a-fA-F]+$/ { print $NF ":" "0x"$1 }' > "$OUTPUT_MAP_FILE"