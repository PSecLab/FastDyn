#!/usr/bin/env python3
import sys
import re
import subprocess

# -------------------------------------------------------------------------------
# This script takes a binary, and outputs a list of its writable ranges of memory
# -------------------------------------------------------------------------------

# Run readelf

def get_readelf_output(elf_path):
    result = subprocess.run(
        ["readelf", "-S", elf_path],
        capture_output=True,
        text=True,
        check=True
    )
    return result.stdout.splitlines()

# Parses through sections, if writable, adds address range and name to list

def parse_sections(lines):
    sections = []

    section_re = re.compile(
        r"\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+\S+\s+([0-9a-fA-F]+)\s+\S+\s+(\S+)"
    )

    for line in lines:
        m = section_re.search(line)
        if not m:
            continue

        name = m.group(1)
        addr = int(m.group(2), 16)
        size = int(m.group(3), 16)
        flags = m.group(4)

        if size == 0:
            continue

        if "W" in flags:
            sections.append((addr, addr + size, name))

    return sections

# Merges consecutive writable ranges

def merge_ranges(ranges):
    if not ranges:
        return []

    ranges.sort(key=lambda x: x[0])
    merged = []
    cur_start, cur_end = ranges[0][0], ranges[0][1]
    cur_names = [ranges[0][2]]

    for start, end, name in ranges[1:]:
        if start == cur_end:
            cur_end = end
            cur_names.append(name)
        else:
            merged.append((cur_start, cur_end, cur_names))
            cur_start, cur_end = start, end
            cur_names = [name]

    merged.append((cur_start, cur_end, cur_names))
    return merged

# Write tab delimited list of address + size of all writable ranges

def run(out_file, bin_path):
    try:
        with open(out_file, 'w') as f:
            lines = get_readelf_output(bin_path)
            sections = parse_sections(lines)
            merged = merge_ranges(sections)
            for start, end, names in merged:
                size = end - start
                f.write(f"0x{start:08X}\t{size:#x}\n")
    except Exception as e:
        print("[binary_wrange.py] Couldn't open ", out_file, e)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python merge_writable_sections.py <firmware.elf>")
        sys.exit(1)
    
    run("fastdyn_work/bin-writable-ranges", sys.argv[1])