from collections import defaultdict
import os
import re
import sys
import argparse

try:
    from colorama import Fore, Style, init
    init(autoreset=True)
except ImportError:
    print("Warning: colorama library not found. Please install it with 'pip install colorama' for colored output.")
    # Create dummy colorama classes if it's not installed
    class DummyStyle:
        def __getattr__(self, name):
            return ""
    Fore = Style = DummyStyle()

class Verifier:
    def __init__(self, hardware_log, emulator_log):
        self.hardware_log = hardware_log
        self.emulator_log = emulator_log

    def _log_info(self, msg):
        print(msg)

    def _log_success(self, msg):
        print(f"{Fore.GREEN}{msg}")

    def _log_warning(self, msg):
        # Warnings (like test failures) are always printed
        print(f"{Fore.YELLOW}{msg}")

    def _log_error(self, msg):
        # Errors are always printed
        print(f"{Fore.RED}{msg}", file=sys.stderr)

    def run_test(self):
        hw_db_r, hw_db_w = self.get_db(self.hardware_log)
        em_db_r, em_db_w = self.get_db(self.emulator_log)

        # Use hardware log as ground truth -- Read Database matching
        for address, hardware_vals in hw_db_r.items():
            emulator_vals = em_db_r.get(address, [])

            try:
                for i in range(len(hardware_vals)):
                    if i >= len(emulator_vals):
                        print(f"Mismatch: Emulator missing value @ address 0x{address:X}, index {i}")
                        continue

                    if hardware_vals[i] != emulator_vals[i]:
                        print(f"Mismatch: READ @ address 0x{address:X} -> Hardware: {hardware_vals[i]} -- Emulator: {emulator_vals[i]}")
            except Exception as e:
                print(f"Error in matching the Read db: {e} for address 0x{address:X}")

        # Use hardware log as ground truth -- Write Database matching
        for address, hardware_vals in hw_db_w.items():
            emulator_vals = em_db_w.get(address, [])

            try:
                for i in range(len(hardware_vals)):
                    if i >= len(emulator_vals):
                        print(f"Mismatch: Emulator missing value @ address 0x{address:X}, index {i}")
                        continue

                    if hardware_vals[i] != emulator_vals[i]:
                        print(f"Mismatch: READ @ address 0x{address:X} -> Hardware: {hardware_vals[i]} -- Emulator: {emulator_vals[i]}")
            except Exception as e:
                print(f"Error in matching the Read db: {e} for address 0x{address:X}")


    def get_db(self, trace_path):
        read_db = defaultdict(list)
        write_db = defaultdict(list)
        self._log_info(f"--- Parsing trace from '{trace_path}' and populating the database ---")

        trace_regex = re.compile(
            r"\[\s*[\d\.]+\].*?(Read|Write):\s*"
            r"address\s*=\s*(0x[0-9a-fA-F]+),\s*"
            r"size\s*=\s*(\d+)\s*bytes,\s*"
            r"value\s*=\s*(0x[0-9a-fA-F]+)", re.IGNORECASE
        )

        with open(trace_path, 'r') as f:
            for i, line in enumerate(f):
                match = trace_regex.search(line)
                if not match:
                    continue

                op_type, addr_str, size_str, val_str = match.groups()
                address = int(addr_str, 16)
                size = int(size_str)
                expected_value = int(val_str, 16)

                #for the current line, check whether it is a read or write, if a read then append to the reading dict.
                if op_type.lower() == "read":
                    if address in read_db:
                        if read_db[address][-1] == expected_value:  #if the current value already in the list, ignore this one
                            continue
                        else:
                            read_db[address].append(expected_value)
                    else:
                        read_db[address].append(expected_value)
                elif op_type.lower() == "write":
                    if address in write_db:
                        if write_db[address][-1] == expected_value:  #if the current value already in the list, ignore this one
                            continue
                        else:
                            write_db[address].append(expected_value)
                    else:
                        write_db[address].append(expected_value)

        return read_db, write_db




if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="A generic Python test harness for a pre-compiled C device model.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "-e", "--emulator-trace",
        required=True,
        help="Path to the trace log from the emulated device."
    )

    parser.add_argument(
        "-hw", "--hardware-trace",
        required=True,
        help="Path to the trace log from the actual hardware."
    )

    args = parser.parse_args()

    try:
        harness = Verifier(
            hardware_log = args.hardware_trace,
            emulator_log = args.emulator_trace
        )

        harness.run_test()
    except FileNotFoundError as e:
        print(f"{Fore.RED}Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"{Fore.RED}An unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"{Fore.CYAN}--- Harness Configuration ---")
    print(f"{Fore.CYAN}  Trace File:   {harness.hardware_log}")
    print(f"{Fore.CYAN}  Trace File:   {harness.emulator_log}")
    print(f"{Fore.CYAN}---------------------------{Style.RESET_ALL}")
