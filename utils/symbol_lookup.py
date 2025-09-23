#!/usr/bin/env python3
import sys
from elftools.elf.elffile import ELFFile

def load_symbols_from_elf(filename):
    symbols = {}
    with open(filename, "rb") as f:
        elf = ELFFile(f)
        # Look through all sections to find symbol tables
        for section in elf.iter_sections():
            if not isinstance(section, type(elf.get_section_by_name('.symtab'))):
                continue
            for symbol in section.iter_symbols():
                # Skip symbols with no address
                if symbol.entry.st_value == 0:
                    continue
                addr = f"0x{symbol.entry.st_value-1:08x}"
                name = symbol.name
                if name:  # store only named symbols
                    symbols[int(addr, 16)] = name
    return symbols

def repl(symbols):
    print("Symbol lookup from ELF. Enter an address (e.g. 0x0805fc2d), or 'quit' to exit.")
    while True:
        try:
            addr = input("> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print("\nExiting.")
            break
        if addr in ("quit", "exit"):
            break
        else:
            if not addr.startswith("0x"):
                print("Please enter addresses in hex format (e.g. 0x0805fc2d)")
                continue

        sym = symbols.get(int(addr, 16))
        if sym:
            print(sym)
        else:
            print("No symbol found for", addr)

import yaml

def read_yaml(filepath, symbols=None) -> list:
    """
    Reads a YAML file and returns the parsed data.
    Add processing logic where indicated.
    """
    with open(filepath, "r") as f:
        data = yaml.safe_load(f)

    modifiers_list = [] # Holds three tuples (address/symbol, register to be modified, value to set)

    for item in data:
        # Add any specific processing logic here if needed
        if 'PCMover' in item.get('class', ''):
            print(f"Found PCMover in function: {item.get('function', 'N/A')}")
            moveto = item.get('registration_args', {}).get('moveto')
            hook_addr = item.get('addr')
            if moveto and hook_addr:
                print(f"  Hook at: {hex(hook_addr)}")
                print(f"  Move to: {hex(moveto)}")
                modifiers_list.append((hex(hook_addr), 'r15', hex(moveto)))

        if 'ReturnZero' in item.get('class', ''):
            print(f"Found ReturnZero in function: {item.get('function', 'N/A')}")
            hook_addr = item.get('addr')
            if hook_addr:
                symbol_name = None
                print(f"  Hook at: {hex(hook_addr)}")
                if symbols and hook_addr in symbols:
                    print(f"  Function name: {symbols[hook_addr]}")
                    symbol_name = symbols[hook_addr]
                if not symbol_name:
                    symbol_name = hex(hook_addr)
                modifiers_list.append((symbol_name, 'r15', 'r14'))
                modifiers_list.append((symbol_name, 'r0', '0'))

        if 'ReturnConstant' in item.get('class', ''):
            print(f"Found ReturnConstant in function: {item.get('function', 'N/A')}")
            constant = item.get('registration_args', {}).get('ret_value')
            hook_addr = item.get('addr')
            if constant is not None and hook_addr:
                symbol_name = None
                print(f"  Hook at: {hex(hook_addr)}")
                print(f"  Returns constant: {constant}")
                if symbols and hook_addr in symbols:
                    print(f"  Function name: {symbols[hook_addr]}")
                    symbol_name = symbols[hook_addr]
                if not symbol_name:
                    symbol_name = hex(hook_addr)
                modifiers_list.append((symbol_name, 'r15', 'r14'))
                modifiers_list.append((symbol_name, 'r0', str(constant)))


        if 'SetRegister' in item.get('class', ''):
            print(f"Found SetRegister in function: {item.get('function', 'N/A')}")
            register = item.get('registration_args', {}).get('reg')
            value = item.get('registration_args', {}).get('value')
            hook_addr = item.get('addr')
            if register and value is not None and hook_addr:
                print(f"  Hook at: {hex(hook_addr)}")
                print(f"  Sets register {register} to {value}")
                if symbols and hook_addr in symbols:
                    print(f"  Function name: {symbols[hook_addr]}")
                    symbol_name = symbols[hook_addr]
                if not symbol_name:
                    symbol_name = hex(hook_addr)
                modifiers_list.append((symbol_name, register, str(value)))

    return modifiers_list

def output_modifiers_to_file(modifiers, output_filepath):
    with open(output_filepath, "w") as f:
        for hook_location, to_be_modified, modified_value in modifiers:
            f.write(f"{hook_location} {to_be_modified} {modified_value}\n")
    print(f"Wrote {len(modifiers)} modifiers to {output_filepath}")

def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print(f"Usage: {sys.argv[0]} <file.elf> <file.yaml: optional>")
        sys.exit(1)

    filename = sys.argv[1]
    symbols = load_symbols_from_elf(filename)
    print(f"Loaded {len(symbols)} symbols from {filename}")
    if len(sys.argv) == 2:
        repl(symbols)
    elif len(sys.argv) == 3:
        mod_list = read_yaml(sys.argv[2], symbols)
        output_file_path = input("Enter output file path (default: output.txt): ").strip()
        if not output_file_path:
            output_file_path = "output.txt"
        output_modifiers_to_file(mod_list, output_file_path)

if __name__ == "__main__":
    main()