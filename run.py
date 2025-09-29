#!/usr/bin/env python3
import tomli  # For Python < 3.11, install with: pip install tomli
# For Python 3.11+, you can use: import tomllib
import sys
from utils.svd_helper import *

# Import colorama for highlighted output
# If you don't have it, install with: pip install colorama
from colorama import init, Fore, Style
import tempfile
import shutil
import shlex
import argparse
import os
import subprocess
import tempfile
from dotenv import load_dotenv

DEBUG=False


def parse_symbol(s: str):
    """
    Parse a string into (symbol, offset).
    If string is just a symbol, offset is 0.
    If string is 'symbol+<num>', offset is the integer value.
    """
    match = re.fullmatch(r'([A-Za-z_][A-Za-z0-9_]*)(?:\+(\d+))?', s.strip())
    if not match:
        raise ValueError(f"Invalid input: {s}")

    symbol = match.group(1)
    offset = int(match.group(2)) if match.group(2) else 0
    return symbol, offset

def is_number(value: str) -> str:
    """
    Check if the given value is an integer, hex, or not a number.

    Returns:
        "hex" if value is hexadecimal,
        "int" if value is integer,
        "none" otherwise.
    """
    # Check for hex (0x prefix and valid hex digits)
    if re.fullmatch(r"0x[0-9A-Fa-f]+", value):
        return "hex"
    # Check for decimal integer
    if re.fullmatch(r"\d+", value):
        return "int"
    return "none"

def calculate_model_ranges(config):
    """Aggregates memory ranges for each active device model."""
    model_ranges = {}
    device_conf = config.get('Device', {})
    for device_name, device_data in device_conf.items():
        if device_name == 'Models' or not isinstance(device_data, dict):
            continue
        for handler in device_data.get('handlers', []):
            model = handler.get('model')
            # We only care about custom plugin models here
            if handler.get('enabled') and model != 'qemu':
                if model not in model_ranges:
                    model_ranges[model] = []
                model_ranges[model].extend(device_data.get('ranges', []))
    return model_ranges


def is_cortexm_register(value: str) -> bool:
    """
    Check if a string is a valid Cortex-M register.
    Valid ranges:
      - r0–r15
      - s0–s31
      - d0–d15
    """
    # Match r0–r15
    if re.fullmatch(r"r([0-9]|1[0-5])", value):
        return True
    # Match s0–s31
    if re.fullmatch(r"s([0-9]|[12][0-9]|3[01])", value):
        return True
    # Match d0–d15
    if re.fullmatch(r"d([0-9]|1[0-5])", value):
        return True
    return False


def load_symbol_addresses(filename: str) -> dict[str, int]:
    """
    Reads a file with lines of format: symbol:address
    and returns a dictionary mapping symbol -> address.
    """
    symbols = {}
    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line or ":" not in line:
                continue
            symbol, address = line.split(":", 1)  # split only once
            address = int(address.strip(), 16)
            # check if address is thumb (odd)
            if address & 1:
                address -= 1  # make even
            symbols[symbol.strip()] = address
    return symbols


def convert_config_file(symbols_dict: dict[str, int], input_file: list[str], output: str) -> bool:
    """
    Convert the virtuals/modifiers file content.
    """
    with open(output, "w") as output_file:
        for line in input_file:
            line = line.strip()
            if not line:
                continue
            tokens = line.split()

            # first token could be a symbol
            first_token = tokens[0]
            if is_number(first_token) == "none":
                symbol, offset = parse_symbol(first_token)
                if symbol in symbols_dict:
                    resolved_address = symbols_dict[symbol]
                    if resolved_address & 1:  # if thumb, make even
                        resolved_address -= 1
                    first_token = hex(resolved_address + offset)
                else:
                    print(f"Warning: {first_token} not found in symbols dictionary")
                    return False

            # second token will be the name of the virtual instruction, register, or memory location
            second_token = tokens[1]

            # third token if it exists could be a symbol
            third_token = None
            if len(tokens) > 2:
                third_token = tokens[2]
                if "*" in third_token or "[" in third_token or "]" in third_token or is_cortexm_register(third_token):
                    pass
                elif is_number(third_token) == "none":
                    if third_token in symbols_dict:
                        third_token = hex(symbols_dict[third_token])
                    else:
                        print(f"Warning: {third_token} not found in symbols dictionary")
                        return False

            output_file.write(f"{first_token} {second_token} {third_token if third_token else ''}\n")

    return True

def extract_regs(expr: str):
    """
    Given a patch expression like 'r2 <- r3',
    return the left and right operands as strings.
    """
    parts = expr.split("<-")
    if len(parts) != 2:
        raise ValueError(f"Invalid expression: {expr}")
    left = parts[0].strip()
    right = parts[1].strip()
    return left, right

def load_config(config_path):
    """
    Loads and parses the TOML configuration file.
    Returns the config dictionary on success, or None on error.
    """
    print(Fore.YELLOW + Style.BRIGHT + "Loading configuration: " + Style.RESET_ALL + config_path)
    try:
        with open(config_path, "rb") as f:
            config = tomli.load(f)
        print(Fore.GREEN + Style.BRIGHT + "Configuration loaded successfully!\n")
        return config
    except FileNotFoundError:
        init(autoreset=True)
        print(Fore.RED + Style.BRIGHT + f"❌ Error: The file '{config_path}' was not found.", file=sys.stderr)
        return None
    except tomli.TOMLDecodeError as e:
        init(autoreset=True)
        print(Fore.RED + Style.BRIGHT + f"❌ Error: Failed to parse TOML file '{config_path}': {e}", file=sys.stderr)
        return None

def parse_and_print_config(config):
    """Loads, parses, and prints key values from the FastDyn TOML config file with color."""
    # Initialize colorama to auto-reset styles after each print
    init(autoreset=True)
    try:
        # --- Print Memory Configuration ---
        print(Style.BRIGHT + Fore.CYAN + "--- Memory ---")
        memory_conf = config.get('Memory', {})
        print(Fore.YELLOW + "  Main RAM Size:" + Style.RESET_ALL + f" {memory_conf.get('main_ram_size')}")
        print(Fore.YELLOW + "  Main RAM Path:" + Style.RESET_ALL + f" {memory_conf.get('shared_mem_path')}/{memory_conf.get('main_ram_file')}")
        print(Fore.YELLOW + "  Shared RAM Size:" + Style.RESET_ALL + f" {memory_conf.get('shared_ram_size')}")
        print(Fore.YELLOW + "  Shared RAM Path:" + Style.RESET_ALL + f" {memory_conf.get('shared_mem_path')}/{memory_conf.get('shared_ram_file')}")
        print(Style.BRIGHT + Fore.CYAN + "-" * 20)

        # --- Print CPU Configuration ---
        print(Style.BRIGHT + Fore.CYAN + "\n--- CPU ---")
        cpu_conf = config.get('CPU', {})
        print(Fore.YELLOW + "  Architecture:" + Style.RESET_ALL + f" {cpu_conf.get('arch')}")
        final_binary = cpu_conf.get('binary')
        final_platform = cpu_conf.get('platform')

        if not final_binary or not final_platform:
            print(Fore.RED + Style.BRIGHT + "❌ Error: Missing configuration in TOML file.")
            if not final_binary:
                print(Fore.RED + "  - The binary path must be specified in the TOML file under `[CPU].binary`.")
            if not final_platform:
                print(Fore.RED + "  - The platform name must be specified in the TOML file under `[CPU].platform`.")
            sys.exit(1)

        # --- Print Runtime Info ---
        print(Style.BRIGHT + Fore.CYAN + "--- Runtime Info ---")
        print(Fore.YELLOW + "  Platform:" + Style.RESET_ALL + f" {final_platform}")
        print(Fore.YELLOW + "  Binary:" + Style.RESET_ALL + f" {final_binary}")
        print(Style.BRIGHT + Fore.CYAN + "-" * 20)

        print(Fore.YELLOW + "  CPU:" + Style.RESET_ALL + f" {cpu_conf.get('cpu')}")
        print(Fore.YELLOW + "  GDB Enabled:" + Style.RESET_ALL + f" {cpu_conf.get('enable_gdb')}")

        # Accessing an array of tables (modifiers)
        modifiers = cpu_conf.get('modifiers', [])
        if modifiers:
            print(Fore.YELLOW + Style.BRIGHT + "\n  Instruction Modifiers:")
            for mod in modifiers:
                print(f"    - At '{mod.get('at')}': Patch with '{mod.get('patch')}'")

        # Accessing an array of tables (virtuals)
        virtuals = cpu_conf.get('virtuals', [])
        if virtuals:
            print(Fore.YELLOW + Style.BRIGHT + "\n  Virtual Instructions:")
            for virt in virtuals:
                # The args can be an array, so join them for nice printing
                args_str = " ".join(virt.get('args', []))
                print(f"    - At '{virt.get('at')}': Execute '{virt.get('instruction')}' with args [{args_str}]")

        print(Style.BRIGHT + Fore.CYAN + "-" * 20)

        # --- Print Device Configuration ---
        print(Style.BRIGHT + Fore.CYAN + "\n--- Devices ---")
        device_conf = config.get('Device', {})

        # NEW: Dynamically print all model configurations
        models_conf = device_conf.get('Models', {})
        if models_conf:
            print(Fore.YELLOW +  Style.BRIGHT + "\n  --- Registered Models ---")
            for model_name, model_data in models_conf.items():
                print(Fore.MAGENTA + Style.BRIGHT + f"\n  Model: [{model_name}]")
                if not model_data:
                    print(Style.DIM + "    (No specific configuration)")
                else:
                    for key, value in model_data.items():
                        print(f"    {key}: {value}")

        print(Fore.YELLOW + Style.BRIGHT + "\n  --- Registered Peripherals ---")
        # Dynamically discover and print all peripherals
        for device_name, device_data in device_conf.items():
            # Skip the 'Models' table, as it's not a peripheral
            if device_name == 'Models' or not isinstance(device_data, dict):
                continue

            print(Fore.MAGENTA + Style.BRIGHT + f"\n  Device: [{device_name}]")
            print(f"    Description: {device_data.get('description')}")
            print(f"    Ranges: {device_data.get('ranges')}")

            handlers = device_data.get('handlers', [])
            if handlers:
                print("    Handlers:")
                for handler in handlers:
                    print(f"      - Model: {handler.get('model')}, Enabled: {handler.get('enabled')}")

        print(Style.BRIGHT + Fore.CYAN + "-" * 20)

    except FileNotFoundError:
        print(Fore.RED + Style.BRIGHT + f"❌ Error: The file '{config_path}' was not found.")
    except tomli.TOMLDecodeError as e:
        print(Fore.RED + Style.BRIGHT + f"❌ Error: Failed to parse TOML file '{config_path}': {e}")
    except Exception as e:
        print(Fore.RED + f"An unexpected error occurred: {e}")

def svd_irq_map(svd_device):
    name_map = {}
    for peripheral in svd_device.peripherals:
        if peripheral.interrupts:
            for interrupt in peripheral.interrupts:
                # The key is the interrupt name, the value is the number
                name_map[interrupt.name] = interrupt.value
    return name_map

def build_qemu_command(config, temp_files, model_ranges):
    """Builds the full qemu-system-arm command from the configuration."""
    cmd = [os.getenv("QEMU_PATH", "qemu-system-arm")]
    device_conf = config.get('Device', {})
    
    # --- Memory Config ---
    mem_conf = config.get('Memory', {})
    ram0_path = os.path.join(mem_conf.get('shared_mem_path', '/dev/shm'), mem_conf.get('main_ram_file'))
    ram1_path = os.path.join(mem_conf.get('shared_mem_path', '/dev/shm'), mem_conf.get('shared_ram_file'))

    #Ugly hardcoding, base address and VTOR should come from SVD
    cmd.extend([
        '-object', f"memory-backend-file,id=ram0,mem-path={ram0_path},size={mem_conf.get('main_ram_size')},share=on",
        '-object', f"memory-backend-file,id=ram1,mem-path={ram1_path},size={mem_conf.get('shared_ram_size')},share=on",
        '-global', 'cortexm-soc.shram_backend=ram1',
        '-global', 'cortexm-soc.ram_baseaddr=0x20000000', 
        '-global', 'cortexm-soc.shram_baseaddr=0x30000000',
        '-global', 'armv7m.init-nsvtor=0x08000000'
    ])
    
    # --- CPU Config ---
    cpu_conf = config.get('CPU', {})
    # TODO: Ugly hardcoding needs to go away
    cmd.extend(['-machine', cpu_conf.get('machine') +",memory-backend=ram0", '-cpu', cpu_conf.get('cpu')])
    cmd.extend(['-kernel', cpu_conf.get('binary')])
    if cpu_conf.get('enable_gdb'): cmd.append('-s')
    if cpu_conf.get('stop_on_start'): cmd.append('-S')
    
    # --- Device Config (QEMU native) ---
    for device_name, device_data in device_conf.items():
        if device_name == 'Models' or not isinstance(device_data, dict): continue
        
        for handler in device_data.get('handlers', []):
            if handler.get('model') == 'qemu' and handler.get('enabled'):
                base_addr = device_data.get('ranges', ["0x0-0x0"])[0].split('-')[0]
                cmd.extend(['-device', f"{handler['type']},id={device_name},addr={base_addr}"])
    
    # --- Plugin Config ---
    plugin_args = []
    plugin_lib_path = config.get('plugin', {}).get('library_path', './build/libfastdyn.so')
    plugin_args.append(plugin_lib_path)
    
    if 'virtuals' in temp_files: plugin_args.append(f"virtual={temp_files['virtuals']}")
    if 'modifiers' in temp_files: plugin_args.append(f"modifier={temp_files['modifiers']}")
    if 'scroll_file' in temp_files:
        elder_model_conf = device_conf.get('Models', {}).get('elder', {})
        backend = elder_model_conf.get('backend')
        plugin_args.append(f"elder_scroll_config={backend}@{temp_files['scroll_file']}")
    
    # Use the pre-calculated model ranges to build the 'dev' string
    dev_strings = []
    for model, ranges in model_ranges.items():
        model_conf = device_conf.get('Models', {}).get(model, {})
        backend = model_conf.get('backend', '')
        arg_str = f"{backend}*" if backend else ""
        range_str = "~".join(ranges)
        dev_strings.append(f"{model}:{arg_str}{range_str}")
        
    if dev_strings:
        plugin_args.append(f"dev={'|'.join(dev_strings)}")
        
    cmd.extend(['-plugin', ",".join(plugin_args)])
    
    # --- Other fixed args ---
    cmd.extend(['-nographic', '-monitor', 'tcp:127.0.0.1:4444,server,nowait'])
    
    return cmd

def generate_config_files(config, output_dir, irq_map, symbols_dict):
    """
    Generates the flat configuration files required by the QEMU plugin.
    """
    init(autoreset=True)
    print(Style.BRIGHT + Fore.BLUE + f"\n--- Generating Configuration Files in '{output_dir}' ---")

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"  Created directory: {output_dir}")

    cpu_conf = config.get('CPU', {})
    generated_files = {}

    # --- Generate virtuals.txt ---
    virtuals = cpu_conf.get('virtuals', [])
    if virtuals:
        virtuals_path = os.path.join(output_dir, 'virtuals.txt')
        virtuals_ir = []
        for virt in virtuals:
            args_str = " ".join(virt.get('args', []))
            if (virt.get('instruction') == "raise_irq"):
                if args_str in irq_map:
                    args_str = str(irq_map[args_str])
                else:
                    # check if it's already a number
                    if not args_str.isdigit():
                        print(Fore.RED + Style.BRIGHT + "❌ Error: Invalid Interrupt for IRQ") 
                        sys.exit()
            virtuals_ir.append(f"{virt.get('at')} {virt.get('instruction')} {args_str}\n")
        convert_config_file(symbols_dict, virtuals_ir, virtuals_path)
        generated_files['virtuals'] = virtuals_path
        print(Fore.GREEN + f"  ✅ Wrote {len(virtuals)} instructions to {virtuals_path}")

    # --- Generate modifiers.txt ---
    modifiers = cpu_conf.get('modifiers', [])
    if modifiers:
        modifiers_path = os.path.join(output_dir, 'modifiers.txt')
        modifiers_ir = []
        for mod in modifiers:
            lhs, rhs = extract_regs(mod.get('patch'))
            modifiers_ir.append(f"{mod.get('at')} {lhs} {rhs}\n")
        convert_config_file(symbols_dict, modifiers_ir, modifiers_path)
        generated_files['modifiers'] = modifiers_path
        print(Fore.GREEN + f"  ✅ Wrote {len(modifiers)} patches to {modifiers_path}")

    # --- Generate Elder Scroll INI file ---
    device_conf = config.get('Device', {})
    elder_model_conf = device_conf.get('Models', {}).get('elder', {})
    scroll_file_path = elder_model_conf.get('scroll_file')

    if scroll_file_path:
        full_scroll_path = os.path.join(output_dir, os.path.basename(scroll_file_path))
        final_scroll_content = []
        for device_name, device_data in device_conf.items():
            if device_name == 'Models' or not isinstance(device_data, dict):
                continue

            # Check if this device has an enabled handler for the elder model
            is_elder_handled = any(h.get('model') == 'elder' and h.get('enabled') for h in device_data.get('handlers', []))

            if is_elder_handled and 'scroll_config' in device_data:
                # Calculate base and size from ranges
                range_str = device_data.get('ranges', ["0x0-0x0"])[0]
                start_str, end_str = range_str.split('-')
                base_addr = int(start_str, 16)
                size = int(end_str, 16) - base_addr + 1

                # Append base and size to the device's scroll config
                scroll_snippet = device_data['scroll_config'].strip()
                scroll_snippet += f"\nbase = {hex(base_addr)}\nsize = {hex(size)}\n"
                final_scroll_content.append(scroll_snippet)

        if final_scroll_content:
            with open(full_scroll_path, 'w') as f:
                f.write("\n".join(final_scroll_content))
            generated_files['scroll_file'] = full_scroll_path
            print(Fore.GREEN + f"  ✅ Wrote combined configuration for {len(final_scroll_content)} devices to {full_scroll_path}")

    print(Style.BRIGHT + Fore.BLUE + "--------------------------------------------------")
    return generated_files

def run_qemu(config, dry_run=False, launch_gdb = False):
    """Orchestrates file generation, command building, and execution."""
    init(autoreset=True)
    temp_dir = tempfile.mkdtemp(prefix="fastdyn_")
    print(Fore.BLUE + f"Created temporary directory: {temp_dir}")
    
    try:
        # 1. Calculate and print model ranges
        model_ranges = calculate_model_ranges(config)
        print(Style.BRIGHT + Fore.CYAN + "\n--- Device Model Memory Map ---")
        if not model_ranges:
            print(Style.DIM + "  No custom device models are assigned to any peripherals.")
        else:
            for model, ranges in model_ranges.items():
                print(Fore.MAGENTA + Style.BRIGHT + f"  Model: [{model}] will handle:")
                for r in ranges:
                    print(f"    - {r}")
        print(Style.BRIGHT + Fore.CYAN + "--------------------------------")

        # 2. Generate config files in the temp directory
        temp_files = generate_config_files(config, temp_dir, irqmap, symbols_dict)
        
        # 3. Build the full QEMU command, passing in the calculated ranges
        qemu_command = build_qemu_command(config, temp_files, model_ranges)
        
        # 4. Print the command
        if DEBUG:
            print(Style.BRIGHT + Fore.CYAN + "\n--- Assembled QEMU Command ---")
            print(" \\\n    ".join(shlex.quote(arg) for arg in qemu_command))
            print(Style.BRIGHT + Fore.CYAN + "--------------------------------\n")
        with open("out/qemu_command.txt", "w") as f:
            f.write(Style.BRIGHT + Fore.CYAN + "\n--- Assembled QEMU Command ---\n")
            f.write(" \\\n    ".join(shlex.quote(arg) for arg in qemu_command) + "\n")
            f.write(Style.BRIGHT + Fore.CYAN + "--------------------------------\n")
        
        # 5. Execute or perform a dry run
        if dry_run:
            print(Fore.YELLOW + Style.BRIGHT + "Dry run successful. QEMU was not executed.")
        else:
            print(Fore.GREEN + Style.BRIGHT + "Executing FastDyn plugin...")
            qemu_process = subprocess.Popen(qemu_command)

        # 6. Launch GDB if requested
        if launch_gdb:
            gdb_script_path = os.path.join(temp_dir, 'gdb_init.txt')
            with open(gdb_script_path, 'w') as f:
                f.write("target remote localhost:1234\n")

            binary_path = config.get('CPU', {}).get('binary')
            # Using xterm as a common terminal emulator
            gdb_command = [
                'xterm',
                '-e',
                f"gdb-multiarch -x {gdb_script_path} {binary_path}"
            ]
            print(Fore.GREEN + Style.BRIGHT + "\nLaunching GDB in a new terminal...")
            subprocess.Popen(gdb_command)
        else:
            print(Fore.YELLOW + Style.BRIGHT + f"Run: gdb-multiarch {config.get('CPU', {}).get('binary')}")

        qemu_process.wait()
            
    except Exception as e:
        print(Fore.RED + Style.BRIGHT + f"\n❌ An error occurred during the launch process: {e}")
    finally:
        # 6. Clean up the temporary directory
        shutil.rmtree(temp_dir)
        print(Fore.BLUE + f"\nCleaned up temporary directory: {temp_dir}")

###########################
######   ##     ## ##    ## 
##   ##  ##     ## ###   ## 
##   ##  ##     ## ####  ## 
######   ##     ## ## ## ## 
##   ##  ##     ## ##  #### 
##   ##  ##     ## ##   ### 
##   ##   #######  ##    ##
###########################
if __name__ == "__main__":
    # Load variables from .env
    load_dotenv()
    parser = argparse.ArgumentParser(
        description="Parse, display, and generate configurations from a FastDyn TOML file.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("config_path", help="Path to the TOML configuration file.")
    parser.add_argument("map_file", help="Path to the symbol map file.")
    parser.add_argument(
        "-o", "--output",
        default='out',
        metavar="OUTPUT_DIR",
        help="Directory to place the generated files (default: './out')."
    )

    args = parser.parse_args()

    config = load_config(sys.argv[1])
    map_file = sys.argv[2]
    svd_file_map = discover_svd_files()
    parse_and_print_config(config)
    cpu_config = config.get('CPU', {})
    platform = cpu_config.get('platform')
    if platform not in svd_file_map:
        print(Style.BRIGHT + Fore.RED + f"[ERROR] Platform '{platform}' not found."); sys.exit(1)

    print(svd_file_map[platform])
    parser = SVDParser.for_xml_file(svd_file_map[platform])
    svd_device = parser.get_device()
    irqmap = svd_irq_map(svd_device)
    print(Fore.GREEN + "✅ SVD file loaded and parsed.\n")

    symbols_dict = load_symbol_addresses(map_file)
#    generated_files = generate_config_files(config, args.output, irqmap, symbols_dict)
    run_qemu(config)
    

    
