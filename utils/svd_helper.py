import os
import re
import sys
import argparse
import subprocess
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import List, Optional, Tuple
from datetime import datetime
from scipy.stats import entropy as calculate_entropy

from colorama import Fore, Style, init
from cmsis_svd.parser import SVDParser

init(autoreset=True)

@dataclass
class MMIOAccess:
    timestamp_ns: int
    access_type: str
    address: Optional[int] = None
    value: Optional[int] = None
    pc: Optional[int] = None
    peripheral: Optional[str] = None
    register: Optional[str] = None
    register_desc: Optional[str] = None
    vector: Optional[int] = None

    def __repr__(self):
        ts_sec = self.timestamp_ns / 1e9
        access_str = f"[{ts_sec:10.6f}] {self.access_type.upper():<5} "
        if self.access_type == 'interrupt':
            return f"[{ts_sec:10.6f}] INTERRUPT on {self.peripheral}, Vector={self.vector} (0x{self.vector:X})"
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
        self.interrupt_map = {}
        for p in self.device.peripherals:
            size = 0x400
            if hasattr(p, 'address_block') and p.address_block:
                size = p.address_block.size
            self.peripheral_map[(p.base_address, p.base_address + size)] = p
            if hasattr(p, 'interrupts') and p.interrupts:
                for i in p.interrupts:
                    self.interrupt_map[i.value] = p.name
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
        interrupt_pattern = re.compile(
            r'\[\s*(?P<timestamp>\d+\.\d+)\s*\]\s*Interrupt Taken:\s*Vector\s*=\s*(?P<vector>0x[0-9a-fA-F]+)'
        )
        enriched_accesses = []
        try:
            with open(log_path, 'r') as f:
                for line_num, line in enumerate(f, 1):
                    match = log_pattern.match(line.strip())
                    interrupt_match = interrupt_pattern.match(line.strip())
                    if interrupt_match:
                        data = interrupt_match.groupdict()
                        vector = int(data['vector'], 16) - 16
                        p_name = self.interrupt_map.get(vector)
                        if p_name is None:
                            print(Style.BRIGHT + Fore.RED + f"[ERROR] On log line {line_num}: Failed to map interrupt vector {vector} (0x{vector:X}) to any SVD peripheral.")
                            sys.exit(1)
                        access = MMIOAccess(
                            timestamp_ns=int(float(data['timestamp']) * 1e9),
                            access_type='interrupt',
                            peripheral=p_name,
                            vector=vector
                        )
                        enriched_accesses.append(access)
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

    def _get_op_key(self, acc: MMIOAccess) -> Tuple:
        """Creates a unique tuple representing an operation for frequency counting."""
        if acc.access_type == 'interrupt':
            return (acc.peripheral, acc.access_type, acc.vector)
        else:
            return (acc.peripheral, acc.register, acc.access_type)
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

        # --- NEW: Entropy Analysis Method ---
    def analyze_entropy(self, accesses: List[MMIOAccess]) -> List[str]:
        """Calculates Shannon entropy for each read register."""
        print("  Analyzing entropy of read values...")
        reads_by_register = defaultdict(list)
        results = []

        # 1. Group all read values by register
        for acc in accesses:
            if acc.access_type == 'read':
                reads_by_register[(acc.peripheral, acc.register)].append(acc.value)

        if not reads_by_register:
            return ["No read operations found for this peripheral."]

        # 2. Calculate entropy for each register's reads
        for (p, r), values in sorted(reads_by_register.items()):
            if len(values) < 2:
                continue # Not enough data to be meaningful

            value_counts = Counter(values)
            # Use scipy's entropy function, which takes probabilities (or raw counts)
            entropy = calculate_entropy(list(value_counts.values()), base=2)
            
            # 3. Classify and format the result
            entropy_class = ""
            if entropy < 2.0:
                entropy_class = "LOW (suggests status/control register)"
            elif entropy < 6.0:
                entropy_class = "MEDIUM (suggests counter or complex status)"
            else:
                entropy_class = "HIGH (suggests data register)"
            
            results.append(f"Register {r}: {entropy_class} - Entropy = {entropy:.2f} bits")
            
        return results if results else ["No registers with sufficient read data for entropy analysis."]

        # --- NEW: ISR Detection Method ---
     # --- MODIFIED: ISR analysis now groups and counts repeating ISRs ---
    def analyze_isr_behavior(self, all_events: List[MMIOAccess], isr_window_ns: int) -> List[str]:
        """Finds repeating ISRs and reports their frequency."""
        print("  Analyzing Interrupt Service Routines (ISRs)...")
        
        interrupt_indices = [i for i, event in enumerate(all_events) if event.access_type == 'interrupt']
        if not interrupt_indices:
            return ["No interrupts found in the entire log."]

        op_counts = Counter(self._get_op_key(evt) for evt in all_events)

        # Store ISRs by their abstract trace (the sequence of operations)
        isrs_by_trace = defaultdict(list)

        for i in interrupt_indices:
            interrupt_event = all_events[i]
            isr_trace_concrete = []
            
            for j in range(i + 1, len(all_events)):
                subsequent_event = all_events[j]
                time_delta = subsequent_event.timestamp_ns - interrupt_event.timestamp_ns

                if 0 < time_delta <= isr_window_ns:
                    if subsequent_event.access_type != 'interrupt':
                        isr_trace_concrete.append(subsequent_event)
                elif time_delta > isr_window_ns:
                    break
            
            # Create an abstract key for the trace to group identical ISRs
            abstract_trace = tuple(self._get_op_key(evt) for evt in isr_trace_concrete)
            
            # Store the first concrete example of this trace
            if not isrs_by_trace[abstract_trace]:
                 isrs_by_trace[abstract_trace].append(interrupt_event) # Store the trigger
                 isrs_by_trace[abstract_trace].extend(isr_trace_concrete) # Store the actions
            else:
                 # We've already stored an example, just increment a counter conceptually
                 # The count will be the length of the list of triggers
                 isrs_by_trace[abstract_trace].append(interrupt_event)


        # Filter for repeating ISRs and format the output
        findings = []
        # Sort by frequency (most common ISR first)
        sorted_isrs = sorted(isrs_by_trace.items(), key=lambda item: len(item[1]), reverse=True)

        for abstract_trace, concrete_events in sorted_isrs:
            count = 0
            trace_triggers = [evt for evt in concrete_events if evt.access_type == 'interrupt']
            trace_count = sum(1 for evt in concrete_events if evt.access_type == 'interrupt')

            if trace_count > 1: # Only report ISRs that happened more than once
                time_frequency_info = ""
                interrupt_trigger = concrete_events[0]
                isr_body = concrete_events[1:len(abstract_trace)+1]

                trigger_key = self._get_op_key(interrupt_trigger)
                total_interrupt_count = op_counts.get(trigger_key, 0)

                time_frequency_info = ""
                if len(trace_triggers) > 1:
                    deltas = [(trace_triggers[k+1].timestamp_ns - trace_triggers[k].timestamp_ns) for k in range(len(trace_triggers)-1)]
                    if deltas:
                        mean_delta_ns = sum(deltas) / len(deltas)
                        variance = sum([(d - mean_delta_ns) ** 2 for d in deltas]) / len(deltas)
                        std_dev_ns = math.sqrt(variance)
                        if std_dev_ns < (mean_delta_ns * 0.05):
                             time_frequency_info = f"# Time Analysis: Periodic, occurs approx. every {mean_delta_ns / 1e6:.3f} ms (std dev: {std_dev_ns / 1e6:.3f} ms)\n"
                        else:
                             time_frequency_info = f"# Time Analysis: Aperiodic, average interval {mean_delta_ns / 1e6:.3f} ms (std dev: {std_dev_ns / 1e6:.3f} ms)\n"

                header = (f"## Repeating ISR for Vector {interrupt_trigger.vector} (0x{interrupt_trigger.vector:X}) ##\n"
                          f"# Total Interrupts on this Vector: {total_interrupt_count}\n"
                          f"# Occurrences of this specific ISR trace: {trace_count}\n"
                          f"{time_frequency_info}") 

                finding_str = header
                finding_str += f"{interrupt_trigger}\n"
                if not isr_body:
                    finding_str += "  - ISR consists of no MMIO activity.\n"
                else:
                    finding_str += "  - Inferred ISR Trace:\n"
                    for trace_event in isr_body:
                        finding_str += f"    {trace_event}\n"
                findings.append(finding_str)
            
        return findings if findings else ["No repeating ISR patterns detected."]


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

