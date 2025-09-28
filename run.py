#!/usr/bin/env python3
#!/usr/bin/env python3
import tomli  # For Python < 3.11, install with: pip install tomli
# For Python 3.11+, you can use: import tomllib
import sys
from utils.svd_helper import *

# Import colorama for highlighted output
# If you don't have it, install with: pip install colorama
from colorama import init, Fore, Style

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

def generate_config_files(config, output_dir, irq_map):
    """
    Generates the flat configuration files required by the QEMU plugin.
    """
    init(autoreset=True)
    print(Style.BRIGHT + Fore.BLUE + f"\n--- Generating Configuration Files in '{output_dir}' ---")

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"  Created directory: {output_dir}")

    cpu_conf = config.get('CPU', {})

    # --- Generate virtuals.txt ---
    virtuals = cpu_conf.get('virtuals', [])
    if virtuals:
        virtuals_path = os.path.join(output_dir, 'virtuals.txt')
        with open(virtuals_path, 'w') as f:
            for virt in virtuals:
                args_str = " ".join(virt.get('args', []))
                if (virt.get('instruction') == "raise_irq"):
                    if args_str not in irq_map:
                        print(Fore.RED + Style.BRIGHT + "❌ Error: Invalid Interrupt for IRQ")
                        sys.exit()
                    args_str = str(irq_map[args_str])
                f.write(f"{virt.get('at')} {virt.get('instruction')} {args_str}\n")
        print(Fore.GREEN + f"  ✅ Wrote {len(virtuals)} instructions to {virtuals_path}")

    # --- Generate modifiers.txt ---
    modifiers = cpu_conf.get('modifiers', [])
    if modifiers:
        modifiers_path = os.path.join(output_dir, 'modifiers.txt')
        with open(modifiers_path, 'w') as f:
            for mod in modifiers:
                # Note: This writes the TOML format directly.
                # A translation step could be added here if the plugin needs a different format.
                f.write(f"{mod.get('at')} {mod.get('patch')}\n")
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
            print(Fore.GREEN + f"  ✅ Wrote combined configuration for {len(final_scroll_content)} devices to {full_scroll_path}")

    print(Style.BRIGHT + Fore.BLUE + "--------------------------------------------------")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Parse, display, and generate configurations from a FastDyn TOML file.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("config_path", help="Path to the TOML configuration file.")
    parser.add_argument(
        "-o", "--output",
        default='out',
        metavar="OUTPUT_DIR",
        help="Directory to place the generated files (default: './out')."
    )
    
    args = parser.parse_args()

    config = load_config(sys.argv[1])
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

    generate_config_files(config, args.output, irqmap)



