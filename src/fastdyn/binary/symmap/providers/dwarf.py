from __future__ import annotations
import capstone

from typing import Iterable, List, Optional

from elftools.elf.elffile import ELFFile
from elftools.dwarf.descriptions import describe_form_class

from fastdyn.binary.symmap.core import SymbolInfo, SymbolProvider


class DwarfProvider(SymbolProvider):
    """
    Symbol provider backed by DWARF debug information.

    Extracts:
      - Functions (DW_TAG_subprogram)
      - Simple global/static variables (DW_TAG_variable with DW_OP_addr)
      - Function epilogues (Return instructions appended as <name>_epi)
    """

    def __init__(self, include_variables: bool = True) -> None:
        self._include_variables = include_variables

    # -------------------------------------------------------------------------

    @property
    def name(self) -> str:
        return "dwarf"

    def is_applicable(self, binary_path: str) -> bool:
        try:
            with open(binary_path, "rb") as f:
                return ELFFile(f).has_dwarf_info()
        except Exception:
            return False

    def get_symbols(self, binary_path: str) -> Iterable[SymbolInfo]:
        # TODO: Make it generic Initialize Capstone for ARM Thumb mode (Standard for Cortex-M)
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)

        with open(binary_path, "rb") as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                return []

            # Helper to read raw bytes from the ELF based on Virtual Memory Address (VMA)
            def read_vaddr(vaddr: int, size: int) -> bytes:
                for segment in elf.iter_segments():
                    if segment['p_type'] == 'PT_LOAD':
                        start_vaddr = segment['p_vaddr']
                        end_vaddr = start_vaddr + segment['p_memsz']
                        if start_vaddr <= vaddr < end_vaddr:
                            file_offset = segment['p_offset'] + (vaddr - start_vaddr)
                            f.seek(file_offset)
                            return f.read(size)
                return b""

            dwarf = elf.get_dwarf_info()
            symbols: List[SymbolInfo] = []

            # 1. Standard DWARF Extraction Pass
            for cu in dwarf.iter_CUs():
                top = cu.get_top_DIE()
                if not top:
                    continue
                for die in top.iter_children():
                    symbols.extend(self._walk_die(die))

            # 2. Epilogue Scanning Pass
			#TODO: This won't capture context switch routines as they have weird ways to return, right now not needed, but in future if need rises something to note
            epilogue_symbols: List[SymbolInfo] = []

            for sym in symbols:
                if sym.kind == "function" and sym.address is not None and sym.size:
                    code_bytes = read_vaddr(sym.address, sym.size)
                    if not code_bytes:
                        continue


                    return_addresses = []

                    # Disassemble and gather ALL returns into a list
                    for insn in md.disasm(code_bytes, sym.address):
                        is_return = False

                        if insn.mnemonic == 'bx' and 'lr' in insn.op_str:
                            is_return = True
                        elif insn.mnemonic == 'pop' and 'pc' in insn.op_str:
                            is_return = True

                        if is_return:
                            return_addresses.append(insn.address)

                    # If we found any returns, emit exactly ONE SymbolInfo containing the list
                    if return_addresses:
                        epi_sym = SymbolInfo(
                            name=f"{sym.name}_epi",
                            address=return_addresses,  # This is now a list! e.g., [0x100, 0x10A]
                            size=0,
                            kind="epilogue",           # Updated kind to distinguish it
                            provider=self.name,
                            confidence=sym.confidence,
                        )
                        epilogue_symbols.append(epi_sym)

            
            # Append the newly discovered epilogues to the master symbol list
            symbols.extend(epilogue_symbols)

            return symbols

    # -------------------------------------------------------------------------

    def _walk_die(self, die) -> List[SymbolInfo]:
        out: List[SymbolInfo] = []

        if die.tag == "DW_TAG_subprogram":
            sym = self._from_subprogram(die)
            if sym:
                out.append(sym)

        elif self._include_variables and die.tag == "DW_TAG_variable":
            name_attr = die.attributes.get('DW_AT_name')
            sym = self._from_variable(die)
            if sym:
                out.append(sym)

        for child in die.iter_children():
            out.extend(self._walk_die(child))

        return out

    def _die_name(self, die) -> Optional[str]:
        attr = die.attributes.get("DW_AT_name")
        if not attr:
            return None
        val = attr.value
        return val.decode(errors="replace") if isinstance(val, bytes) else str(val)

    def _from_subprogram(self, die) -> Optional[SymbolInfo]:
        name = self._die_name(die)
        if not name:
            return None

        low_pc = die.attributes.get("DW_AT_low_pc")
        if not low_pc:
            return None

        addr = int(low_pc.value)
        size = None

        high_pc = die.attributes.get("DW_AT_high_pc")
        if high_pc:
            cls = describe_form_class(high_pc.form)
            val = int(high_pc.value)
            size = val - addr if cls == "address" else val

        return SymbolInfo(
            name=name,
            address=addr,
            size=size,
            kind="function",
            provider=self.name,
            confidence=1.0,
        )

    def _from_variable(self, die) -> Optional[SymbolInfo]:
        name = self._die_name(die)
        if not name:
            return None

        loc = die.attributes.get("DW_AT_location")
        if not loc or not isinstance(loc.value, (list, bytes, bytearray)):
            return None

        loc_data = loc.value

        # Only handle DW_OP_addr (simple, unambiguous case)
        if len(loc_data) == 0 or loc.value[0] != 0x03:
            return None

        addr_size = die.cu["address_size"]
        if len(loc.value) < 1 + addr_size:
            return None

        raw_addr_bytes = bytes(loc_data[1 : 1 + addr_size])
        addr = int.from_bytes(raw_addr_bytes, "little")

        return SymbolInfo(
            name=name,
            address=addr,
            size=None,
            kind="variable",
            provider=self.name,
            confidence=0.95,
        )
