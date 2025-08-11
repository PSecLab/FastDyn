import ctypes
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

# --- C Model Information (for context) ---
# This script is designed to test a pre-compiled C shared library that
# models a memory-mapped device. The C library should export functions
# for initialization, reading, and writing, which can be specified via
# command-line arguments.
#
# Example C function signatures:
#
# // Function to initialize the device's state
# EXPORT void initialize_state();
#
# // Function to emulate MMIO reads from the device
# EXPORT uint64_t device_read(void *opaque, uint64_t addr, unsigned size);
#
# // Function to emulate MMIO writes to the device
# EXPORT void device_write(void *opaque, uint64_t offset, uint64_t value, unsigned size);
#
# You would compile your C code into a shared library like this:
# gcc -shared -o my_device_model.so -fPIC my_device_model.c
#
# --- Main Test Harness Logic ---

class DeviceTestHarness:
    """A generic test harness to test a pre-compiled C device model from Python."""

    def __init__(self, lib_path, trace_file_path, base_addr, size, init_func_name, read_func_name, write_func_name, verbose=False):
        """
        Initializes the test harness.
        
        :param lib_path: Path to the compiled C shared library (.so or .dll).
        :param trace_file_path: Path to the trace log file.
        :param base_addr: The base memory address of the target device.
        :param size: The memory size/range of the device.
        :param init_func_name: Name of the initialization function in the C library.
        :param read_func_name: Name of the memory read function in the C library.
        :param write_func_name: Name of the memory write function in the C library.
        :param verbose: Boolean flag for verbose output.
        """
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"Shared library not found at: {lib_path}")
        if not os.path.exists(trace_file_path):
            raise FileNotFoundError(f"Trace file not found at: {trace_file_path}")
            
        self.lib_path = lib_path
        self.trace_file_path = trace_file_path
        self.base_addr = base_addr
        self.size = size
        self.init_func_name = init_func_name
        self.read_func_name = read_func_name
        self.write_func_name = write_func_name
        self.verbose = verbose
        self.errors = 0
        self.match  = 0
        self.writes = 0
        self.lib = None

    def _log_info(self, msg):
        if self.verbose:
            print(msg)
            
    def _log_success(self, msg):
        if self.verbose:
            print(f"{Fore.GREEN}{msg}")

    def _log_warning(self, msg):
        # Warnings (like test failures) are always printed
        print(f"{Fore.YELLOW}{msg}")
        
    def _log_error(self, msg):
        # Errors are always printed
        print(f"{Fore.RED}{msg}", file=sys.stderr)

    def _load_library_and_setup_functions(self):
        """Loads the compiled library and sets up ctypes function prototypes."""
        self._log_info(f"--- Step 1: Loading shared library and setting up functions ---")
        try:
            self.lib = ctypes.CDLL(os.path.abspath(self.lib_path))
            
            self.init_func = getattr(self.lib, self.init_func_name)
            self.read_func = getattr(self.lib, self.read_func_name)
            self.write_func = getattr(self.lib, self.write_func_name)

            self.init_func.argtypes, self.init_func.restype = [], None
            self.read_func.argtypes, self.read_func.restype = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint], ctypes.c_uint64
            self.write_func.argtypes, self.write_func.restype = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint], None
            
            self._log_success(f"Library '{self.lib_path}' loaded and functions configured successfully.\n")
            return True
        except AttributeError as e:
            self._log_error(f"Error: A specified function was not found in the library: {e}")
            self._log_error("Please check the --init-func, --read-func, and --write-func arguments.")
            return False
        except Exception as e:
            self._log_error(f"Failed to load shared library: {e}")
            return False

    def _parse_and_run_trace(self):
        """Parses the trace log, calls C functions, and validates read operations."""
        self._log_info(f"--- Step 2: Parsing trace from '{self.trace_file_path}' and driving the C model ---")
        
        self.init_func()
        self._log_info("-" * 50)

        trace_regex = re.compile(
            r"\[\s*[\d\.]+\].*?(Read|Write):\s*"
            r"address\s*=\s*(0x[0-9a-fA-F]+),\s*"
            r"size\s*=\s*(\d+)\s*bytes,\s*"
            r"value\s*=\s*(0x[0-9a-fA-F]+)", re.IGNORECASE
        )
        
        with open(self.trace_file_path, 'r') as f:
            for i, line in enumerate(f):
                match = trace_regex.search(line)
                if not match:
                    continue

                op_type, addr_str, size_str, val_str = match.groups()
                address = int(addr_str, 16)
                size = int(size_str)
                expected_value = int(val_str, 16)

                if self.base_addr <= address < self.base_addr + self.size:
                    offset = address
                    self._log_info(f"Python Harness: Processing line {i+1}: {op_type} at offset 0x{offset:02x}")
                    
                    if op_type.lower() == "read":
                        c_model_value = self.read_func(None, offset, size)
                        if c_model_value == expected_value:
                            self._log_success(f"Read Match:    Offset 0x{offset:02x}. Expected 0x{expected_value:x}, Got 0x{c_model_value:x}")
                            self.match = self.match + 1
                        else:
                            self._log_warning(f"Read Mismatch: Offset 0x{offset:02x}. Expected 0x{expected_value:x}, but got 0x{c_model_value:x}")
                            self.errors = self.errors + 1

                    elif op_type.lower() == "write":
                        self._log_info(f"Write Operation: Offset 0x{offset:02x}, Value 0x{expected_value:x}")
                        self.write_func(None, offset, expected_value, size)
                        self.writes = self.writes + 1
                    self._log_info("-" * 50)
        
        self._log_success("Trace processing complete.")

    def run_test(self):
        """Executes the full test sequence."""
        if self._load_library_and_setup_functions():
            self._parse_and_run_trace()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="A generic Python test harness for a pre-compiled C device model.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--lib", required=True, help="Path to the pre-compiled shared library file (e.g., my_model.so).")
    parser.add_argument("--trace", required=True, help="Path to the trace log file to use as input.")
    parser.add_argument("--base-addr", required=True, type=lambda x: int(x, 0), help="Base memory address of the target device in hex (e.g., 0x40021800).")
    parser.add_argument("--size", type=lambda x: int(x, 0), default=0x1000, help="Memory size/range of the device in hex (default: 0x1000).")
    parser.add_argument("--init-func", default="initialize_state", help="Name of the state initialization function in the library (default: initialize_state).")
    parser.add_argument("--read-func", default="device_read", help="Name of the memory read function in the library (default: device_read).")
    parser.add_argument("--write-func", default="device_write", help="Name of the memory write function in the library (default: device_write).")
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose output.")

    args = parser.parse_args()

    try:
        harness = DeviceTestHarness(
            lib_path=args.lib,
            trace_file_path=args.trace,
            base_addr=args.base_addr,
            size=args.size,
            init_func_name=args.init_func,
            read_func_name=args.read_func,
            write_func_name=args.write_func,
            verbose=args.verbose
        )
        harness.run_test()
    except FileNotFoundError as e:
        print(f"{Fore.RED}Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"{Fore.RED}An unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"{Fore.CYAN}--- Harness Configuration ---")
    print(f"{Fore.CYAN}  Library Path: {harness.lib_path}")
    print(f"{Fore.CYAN}  Trace File:   {harness.trace_file_path}")
    print(f"{Fore.CYAN}  Base Address: {hex(harness.base_addr)}")
    print(f"{Fore.CYAN}  Memory Size:  {hex(harness.size)}")
    print(f"{Fore.CYAN}  Init Func:    '{harness.init_func_name}'")
    print(f"{Fore.CYAN}  Read Func:    '{harness.read_func_name}'")
    print(f"{Fore.CYAN}  Write Func:   '{harness.write_func_name}'")
    print(f"{Fore.CYAN}---------------------------{Style.RESET_ALL}")
    if harness.errors > 0:
        print(f"{Fore.RED}Result -> Matches: {harness.match}, Errors: {harness.errors}, Writes: {harness.writes}{Style.RESET_ALL}")
    else:
        print(f"{Fore.GREEN}Result -> Matches: {harness.match}, Errors: {harness.errors}, Writes: {harness.writes}{Style.RESET_ALL}")

