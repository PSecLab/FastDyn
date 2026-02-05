#!/usr/bin/env python3
import subprocess
import sys
import re
import argparse
from collections import defaultdict
import bisect

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------

def run(cmd):
    return subprocess.check_output(cmd, shell=True, text=True)

def addr2line(elf, pc):
    out = run(f"arm-none-eabi-addr2line -e {elf} -f -C -p {pc}")
    return out.strip()

# ------------------------------------------------------------
# Load executed blocks: <pc>\t<count>
# ------------------------------------------------------------

def load_executed_blocks(path):
    blocks = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            pc_str, count_str = line.split("\t")
            pc = int(pc_str, 16)
            count = int(count_str)
            blocks[pc] = count
    return blocks

# ------------------------------------------------------------
# Parse disassembly into ordered list of PCs
# ------------------------------------------------------------

def parse_disasm_pcs(disasm_text):
    pcs = []
    for line in disasm_text.splitlines():
        m = re.match(r"\s*([0-9a-fA-F]+):", line)
        if m:
            pcs.append(int(m.group(1), 16))
    return pcs

# ------------------------------------------------------------
# Expand executed blocks into full instruction PCs
# ------------------------------------------------------------

# disasm_pcs must be a sorted list of all pcs in the binary
def expand_executed(blocks, disasm_pcs):
    executed = set()

    for block_pc in blocks:
        block_len = blocks[block_pc]
        idx = bisect.bisect_left(disasm_pcs, block_pc)
        if not disasm_pcs[idx] == block_pc:
            print("Error, block_pc not in disassembled pcs, make sure the list is sorted.")
            exit()

        # Could extend past array, but in that case something else must be wrong, and will at least let us know
        for i in range(idx, idx + block_len):
            executed.add(disasm_pcs[i])

    return executed


# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("elf")
    parser.add_argument("disasm")
    parser.add_argument("executed_blocks")
    args = parser.parse_args()

    elf = args.elf
    disasm_text = open(args.disasm).read()
    executed_blocks = load_executed_blocks(args.executed_blocks)

    print("[+] Parsing disassembly...")

    disasm_pcs = parse_disasm_pcs(disasm_text)
    
    all_pcs = sorted(disasm_pcs)
    
    print(f"[+] Found {len(disasm_pcs)} instruction PCs")

    print("[+] Expanding executed blocks...")

    executed_pcs = expand_executed(executed_blocks, disasm_pcs)
    print(f"[+] Executed PCs: {len(executed_pcs)}")

    missing_pcs = sorted(set(all_pcs) - executed_pcs)
    print(f"[+] Missing PCs: {len(missing_pcs)}")

    print("[+] Mapping missing PCs to source lines...")

    all_files = set()
    executed_by_file = defaultdict(set)

    for pc in executed_pcs:
        src = addr2line(elf, hex(pc))
        if " at " not in src or ":" not in src:
            continue
        _, file_and_line = src.split(" at ", 1)
        if ":" not in file_and_line:
            continue
        file, line_str = file_and_line.rsplit(":", 1)
        if file == "??" or file.endswith(".o") or file.endswith(".o:?"):
            continue
        all_files.add(file)

        m = re.search(r":(\d+)", file_and_line)
        if not m:
            continue

        line = int(m.group(1))
        executed_by_file[file].add(line)

    for pc in missing_pcs:
        src = addr2line(elf, hex(pc))
        if " at " not in src or ":" not in src:
            continue
        _, file_and_line = src.split(" at ", 1)
        if ":" not in file_and_line:
            continue
        file, line_str = file_and_line.rsplit(":", 1)
        if file == "??" or file.endswith(".o") or file.endswith(".o:?"):
            continue
        all_files.add(file)

    # ------------------------------------------------------------
    # Generate LCOV tracefile
    # ------------------------------------------------------------
    print("\n[+] Generating LCOV tracefile: coverage.info")

    with open("coverage.info", "w") as out:
        for file in all_files:
            try:
                with open(file, errors="ignore") as f:
                    total_lines = len(f.readlines())
            except Exception as e:
                print("File Error:", file, "->", repr(e))
                continue

            out.write("TN:\n")
            out.write(f"SF:{file}\n")

            # Mark each line as executed (1) or not (0)
            for line in range(1, total_lines + 1):
                if line in executed_by_file[file]:
                    hits = 1
                else:
                    hits = 0
                out.write(f"DA:{line},{hits}\n")

            out.write("end_of_record\n")

    print("[+] LCOV tracefile written to coverage.info")

    # ------------------------------------------------------------
    # Generate HTML report
    # ------------------------------------------------------------
    print("[+] Generating HTML report in ./coverage_html")

    run("genhtml coverage.info --output-directory coverage_html")

    print("[+] Opening HTML report...")
    run("open coverage_html/index.html")



if __name__ == "__main__":
    main()