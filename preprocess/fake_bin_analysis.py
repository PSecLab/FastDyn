from elftools.elf.elffile import ELFFile

def extract_symbols(elf_path, out_path="map.txt"):
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)

        symbols = []

        # Look through all sections for symbol tables
        for section in elf.iter_sections():
            if section['sh_type'] == 'SHT_SYMTAB' or section['sh_type'] == 'SHT_DYNSYM':
                symtab = section
                for sym in symtab.iter_symbols():
                    name = sym.name
                    addr = sym['st_value']
                    if name:  # ignore unnamed
                        symbols.append((name, addr))

        # Write out map.txt
        with open(out_path, "w") as out:
            for name, addr in symbols:
                out.write(f"{name}:{addr:x}\n")

    print(f"Wrote {len(symbols)} symbols to {out_path}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <elf-file>")
        sys.exit(1)

    extract_symbols(sys.argv[1])