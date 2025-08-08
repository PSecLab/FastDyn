import os
import re
import sys
import argparse
import subprocess
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import List, Optional, Tuple
from datetime import datetime

from colorama import Fore, Style, init
from cmsis_svd.parser import SVDParser

init(autoreset=True)

@dataclass
class MMIOAccess:
    timestamp_ns: int
    address: int
    value: int
    access_type: str
    pc: int
    peripheral: Optional[str] = None
    register: Optional[str] = None
    register_desc: Optional[str] = None

    def __repr__(self):
        ts_sec = self.timestamp_ns / 1e9
        access_str = f"[{ts_sec:10.6f}] {self.access_type.upper():<5} "
        if self.peripheral and self.register:
            access_str += f"to {self.peripheral}->{self.register} (0x{self.address:08X}) value=0x{self.value:X}, pc=0x{self.pc:X}"
        else:
            access_str += f"to address 0x{self.address:08X} value=0x{self.value:X}, pc=0x{self.pc:X}"
        return access_str

class MMIOAnalyzer:
    def __init__(self, svd_path: str):
        print(f"Parsing SVD file: {svd_path}")
        try:
            parser = SVDParser.for_xml_file(svd_path)
            self.device = parser.get_device()
            self._build_peripheral_map()
        except FileNotFoundError:
            print(Style.BRIGHT + Fore.RED + f"[ERROR] SVD file not found at '{svd_path}'"); sys.exit(1)
        except AttributeError as e:
            print(Style.BRIGHT + Fore.YELLOW + f"[WARNING] SVD parsing issue: {e}. Attempting to continue.")
            if not hasattr(self, 'device'): self.device = parser.get_device()
            self._build_peripheral_map()
        except Exception as e:
            print(Style.BRIGHT + Fore.RED + f"[ERROR] Could not parse SVD file: {e}"); self.device, self.peripheral_map = None, {}

    def _build_peripheral_map(self):
        self.peripheral_map = {}
        for p in self.device.peripherals:
            size = 0x400
            if hasattr(p, 'address_block') and p.address_block:
                size = p.address_block.size
            self.peripheral_map[(p.base_address, p.base_address + size)] = p
        print(f"SVD parsed successfully. Found {len(self.device.peripherals)} peripherals.")

    def _find_register_for_address(self, addr: int):
        for (start, end), p in self.peripheral_map.items():
            if start <= addr < end:
                for r in p.registers:
                    if p.base_address + r.address_offset == addr: return p.name, r.name, r.description
        return None, None, None

    def load_and_correlate_log(self, log_path: str) -> List[MMIOAccess]:
        print(f"Loading and correlating log file: {log_path}")
        log_pattern = re.compile(
            r'\[\s*(?P<timestamp>\d+\.\d+)\s*\]\s*(?P<type>Read|Write):\s*address\s*=\s*(?P<address>0x[0-9a-fA-F]+),\s*'
            r'size\s*=\s*\d+\s*bytes,\s*value\s*=\s*(?P<value>0x[0-9a-fA-F]+),\s*pc=(?P<pc>0x[0-9a-fA-F]+)'
        )
        enriched_accesses = []
        try:
            with open(log_path, 'r') as f:
                for line_num, line in enumerate(f, 1):
                    match = log_pattern.match(line.strip())
                    if not match: continue
                    data = match.groupdict()
                    address = int(data['address'], 16)
                    p_name, r_name, d_name = self._find_register_for_address(address)
                    if p_name is None:
                        print(Style.BRIGHT + Fore.RED + f"[ERROR] On log line {line_num}: Failed to map address 0x{address:08X} to any SVD peripheral.")
                        sys.exit(1)
                    access = MMIOAccess(
                        timestamp_ns=int(float(data['timestamp']) * 1e9), access_type=data['type'].lower(),
                        address=address, value=int(data['value'], 16), pc=int(data['pc'], 16),
                        peripheral=p_name, register=r_name, register_desc=d_name
                    )
                    enriched_accesses.append(access)
        except FileNotFoundError:
            print(Style.BRIGHT + Fore.RED + f"[ERROR] Log file not found at '{log_path}'"); sys.exit(1)
        print(f"Log correlated successfully. Total valid accesses parsed: {len(enriched_accesses)}")
        return enriched_accesses

    def separate_init_and_runtime(self, accesses: List[MMIOAccess]) -> Tuple[List[MMIOAccess], List[MMIOAccess]]:
        print(f"  Separating accesses...")
        op_key = lambda acc: (acc.peripheral, acc.register, acc.access_type)
        if len(accesses) < 2: return accesses, []
        op_counts = Counter(op_key(acc) for acc in accesses)
        if not op_counts or not op_counts.most_common(1): return accesses, []
        most_common_op, count = op_counts.most_common(1)[0]
        if count <= 1:
            print("  No dominant repetitive operation found. Classifying all as initialization.")
            return accesses, []
        loop_start_time = -1
        for acc in accesses:
            if op_key(acc) == most_common_op:
                loop_start_time = acc.timestamp_ns
                break
        init_accesses = [acc for acc in accesses if acc.timestamp_ns < loop_start_time]
        runtime_accesses = [acc for acc in accesses if acc.timestamp_ns >= loop_start_time]
        print(f"  Separation complete. Init accesses: {len(init_accesses)}, Runtime accesses: {len(runtime_accesses)}")
        return init_accesses, runtime_accesses

    def find_repeating_patterns(self, runtime_accesses: List[MMIOAccess], method: str, **kwargs):
        print(f"  Finding patterns in runtime accesses...")
        sequence = [(a.peripheral, a.register, a.access_type) for a in runtime_accesses]
        if not sequence: return []
        if method == 'minimal_cycle':
            return self._find_patterns_minimal_cycle(sequence, **kwargs)
        elif method == 'ngram':
            return self._find_patterns_ngram(sequence, **kwargs)
        else:
            raise ValueError(f"Unsupported method: {method}")
    
    def _find_patterns_ngram(self, sequence: List, n: int = 4, top_k: int = 3, **kwargs):
        if len(sequence) < n: return []
        ngrams = zip(*[sequence[i:] for i in range(n)])
        return Counter(ngrams).most_common(top_k)

    def _find_patterns_minimal_cycle(self, sequence: List, **kwargs) -> List[Tuple[Tuple, int]]:
        seq_len = len(sequence)
        if seq_len < 2: return []
        for p in range(1, seq_len // 2 + 1):
            pattern = tuple(sequence[0:p])
            if len(sequence) >= 2*p and tuple(sequence[p : 2*p]) == pattern:
                return [(pattern, seq_len // p)]
        return []

    # --- NEW: Stateful Analysis Method ---
    def analyze_stateful_behavior(self, accesses: List[MMIOAccess]) -> List[str]:
        """Scans a list of accesses for common stateful patterns."""
        print("  Analyzing stateful behavior...")
        findings = []
        findings.extend(self._find_rmw_patterns(accesses))
        findings.extend(self._find_write_poll_patterns(accesses))

        if not findings:
            return ["No significant stateful patterns detected for this peripheral."]
        return findings

    def _find_rmw_patterns(self, accesses: List[MMIOAccess]) -> List[str]:
        """Finds unique Read-Modify-Write patterns."""
        found_registers = set()
        results = []
        for i in range(len(accesses) - 1):
            read_op = accesses[i]
            write_op = accesses[i+1]
            if (read_op.access_type == 'read' and
                write_op.access_type == 'write' and
                read_op.address == write_op.address):
                
                reg_tuple = (read_op.peripheral, read_op.register)
                if reg_tuple not in found_registers:
                    results.append(f"Read-Modify-Write (RMW) pattern detected on register: {read_op.register}")
                    found_registers.add(reg_tuple)
        return results

    def _find_write_poll_patterns(self, accesses: List[MMIOAccess]) -> List[str]:
        """Finds unique Write-Poll patterns."""
        found_patterns = set()
        results = []
        # Look ahead with a window of 3 accesses
        for i in range(len(accesses) - 2):
            op1 = accesses[i]
            op2 = accesses[i+1]
            op3 = accesses[i+2]

            # Pattern: WRITE(A), READ(B), READ(B)
            if (op1.access_type == 'write' and
                op2.access_type == 'read' and
                op3.access_type == 'read' and
                op2.address == op3.address and # It's a poll on the same register
                op1.address != op2.address):   # The command register is different from the status register
                
                pattern_tuple = (op1.register, op2.register)
                if pattern_tuple not in found_patterns:
                    results.append(f"Write-Poll pattern detected: WRITE to {op1.register}, followed by polling {op2.register}")
                    found_patterns.add(pattern_tuple)
        return results

def discover_svd_files():
    repo_dir = "cmsis-svd-data"; repo_url = "https://github.com/cmsis-svd/cmsis-svd-data.git"
    if not os.path.isdir(repo_dir):
        print(f"SVD data directory '{repo_dir}' not found.")
        try:
            if input("   Would you like to clone it now? (y/n): ").lower() != 'y': print("Aborting."); sys.exit(0)
            print(f"Cloning SVD data from {repo_url}..."); subprocess.run(["git", "clone", "--depth", "1", repo_url, repo_dir], check=True, capture_output=True); print("Clone successful.")
        except (FileNotFoundError, subprocess.CalledProcessError):
            print(Style.BRIGHT + Fore.RED + "[ERROR] Failed to clone repository. Please install Git or clone it manually."); sys.exit(1)
    svd_map = {}
    data_path = os.path.join(repo_dir, "data")
    for root, _, files in os.walk(data_path):
        for filename in files:
            if filename.endswith('.svd'):
                device_name = os.path.splitext(filename)[0]
                if device_name in svd_map: print(Style.BRIGHT + Fore.YELLOW + f"[WARNING] Duplicate device name '{device_name}'.")
                svd_map[device_name] = os.path.join(root, filename)
    print(f"Automatically discovered {len(svd_map)} SVD files.")
    return svd_map

# ==============================================================================
# MAIN EXECUTION BLOCK
# ==============================================================================
if __name__ == "__main__":
    
    parser = argparse.ArgumentParser(description="Analyze MMIO logs to find initialization and repeating patterns.", formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("logfile", nargs='?', default=None, help="Path to the MMIO log file.")
    parser.add_argument("platform", nargs='?', default=None, help="The target platform name (e.g., 'STM32F407').")
    parser.add_argument("--method", type=str, default='minimal_cycle', choices=['minimal_cycle', 'ngram'], help="Algorithm for pattern detection.")
    parser.add_argument("--n", type=int, default=4, help="The sequence length for 'ngram' method (default: 4).")
    parser.add_argument("--list-platforms", action="store_true", help="List all discovered platform names and exit.")
    
    args = parser.parse_args()
    svd_file_map = discover_svd_files()
    if args.list_platforms:
        print("\n--- Discovered Platforms ---"); [print(f"  - {name}") for name in sorted(svd_file_map.keys())]; sys.exit(0)
    
    if not args.logfile or not args.platform:
        parser.error("the following arguments are required: logfile, platform")

    if args.platform not in svd_file_map:
        print(Style.BRIGHT + Fore.RED + f"[ERROR] Platform '{args.platform}' not found."); sys.exit(1)
    
    svd_file_path = svd_file_map[args.platform]
    analyzer = MMIOAnalyzer(svd_path=svd_file_path)
    all_accesses = analyzer.load_and_correlate_log(log_path=args.logfile)
    
    output_dir = "out_min_ctxt"
    os.makedirs(output_dir, exist_ok=True)
    print(f"\nAnalysis starting. Output will be saved in the '{output_dir}' directory.")
    
    accesses_by_peripheral = defaultdict(list)
    for acc in all_accesses:
        accesses_by_peripheral[acc.peripheral].append(acc)

    with open(os.path.join(output_dir, "summary.txt"), 'w') as f:
        f.write(f"--- Analysis Summary ---\n")
        f.write(f"Run Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Log File: {os.path.abspath(args.logfile)}\n")
        f.write(f"Platform: {args.platform}\n")
        f.write(f"Pattern Method: {args.method}\n")
        if args.method == 'ngram':
            f.write(f"N-gram Size: {args.n}\n")
        f.write("-" * 25 + "\n")
        f.write(f"Found {len(accesses_by_peripheral)} active peripherals:\n")
        for p_name in sorted(accesses_by_peripheral.keys()): f.write(f"  - {p_name}\n")

    print("-" * 60)

    for peripheral, p_accesses in sorted(accesses_by_peripheral.items()):
        print(f"\n--- Analyzing Peripheral: {peripheral} ---")
        peripheral_dir = os.path.join(output_dir, peripheral)
        os.makedirs(peripheral_dir, exist_ok=True)
        
        init_accesses, runtime_accesses = analyzer.separate_init_and_runtime(p_accesses)
        
        with open(os.path.join(peripheral_dir, "init.txt"), 'w') as f:
            for access in init_accesses: f.write(f"{access}\n")
        print(f"  Initialization context saved to: {os.path.join(peripheral_dir, 'init.txt')}")
            
        with open(os.path.join(peripheral_dir, "runtime_full_trace.txt"), 'w') as f:
            for access in runtime_accesses: f.write(f"{access}\n")
        print(f"  Full runtime trace saved to: {os.path.join(peripheral_dir, 'runtime_full_trace.txt')}")
        
        patterns = analyzer.find_repeating_patterns(runtime_accesses, method=args.method, n=args.n)
        if patterns:
            runtime_sequence_simplified = [(a.peripheral, a.register, a.access_type) for a in runtime_accesses]
            for i, (abstract_pattern, count) in enumerate(patterns):
                pattern_file_path = os.path.join(peripheral_dir, f"loop_pattern_{i+1}.txt")
                with open(pattern_file_path, 'w') as f:
                    first_occurrence_index = -1
                    for j in range(len(runtime_sequence_simplified) - len(abstract_pattern) + 1):
                        if tuple(runtime_sequence_simplified[j : j + len(abstract_pattern)]) == abstract_pattern:
                            first_occurrence_index = j; break
                    header = ""
                    if args.method == 'minimal_cycle':
                        header = f"# Minimal repeating cycle of length {len(abstract_pattern)} (repeats approx. {count} times)"
                    else:
                        header = f"# Pattern #{i+1} (found {count} times)"
                    f.write(f"{header}\n")
                    if first_occurrence_index != -1:
                        for k in range(len(abstract_pattern)):
                            f.write(f"{runtime_accesses[first_occurrence_index + k]}\n")
                print(f"  Detected pattern saved to: {pattern_file_path}")
                
        # --- NEW: Call stateful analysis and write file ---
        state_findings = analyzer.analyze_stateful_behavior(p_accesses)
        state_file_path = os.path.join(peripheral_dir, "state.txt")
        with open(state_file_path, 'w') as f:
            f.write(f"# Stateful Behavior Analysis for {peripheral}\n\n")
            for finding in state_findings:
                f.write(f"- {finding}\n")
        print(f"  Stateful analysis saved to: {state_file_path}")
        
    print("-" * 60); print("Analysis complete.")
