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
                    symbols[addr] = name
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
                print("Please enter address in hex format (e.g. 0x0805fc2d)")
                continue

        sym = symbols.get(addr)
        if sym:
            print(sym)
        else:
            print("No symbol found for", addr)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.elf>")
        sys.exit(1)

    filename = sys.argv[1]
    symbols = load_symbols_from_elf(filename)
    print(f"Loaded {len(symbols)} symbols from {filename}")
    repl(symbols)

if __name__ == "__main__":
    main()