from __future__ import annotations

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
        with open(binary_path, "rb") as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                return []

            dwarf = elf.get_dwarf_info()
            symbols: List[SymbolInfo] = []

            for cu in dwarf.iter_CUs():
                top = cu.get_top_DIE()
                if not top:
                    continue
                for die in top.iter_children():
                    symbols.extend(self._walk_die(die))

            return symbols

    # -------------------------------------------------------------------------

    def _walk_die(self, die) -> List[SymbolInfo]:
        out: List[SymbolInfo] = []

        if die.tag == "DW_TAG_subprogram":
            sym = self._from_subprogram(die)
            if sym:
                out.append(sym)

        elif self._include_variables and die.tag == "DW_TAG_variable":
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
        if not loc or not isinstance(loc.value, (bytes, bytearray)):
            return None

        # Only handle DW_OP_addr (simple, unambiguous case)
        if loc.value[0] != 0x03:
            return None

        addr_size = die.cu["address_size"]
        if len(loc.value) < 1 + addr_size:
            return None

        addr = int.from_bytes(
            loc.value[1 : 1 + addr_size], "little"
        )

        return SymbolInfo(
            name=name,
            address=addr,
            size=None,
            kind="variable",
            provider=self.name,
            confidence=0.95,
        )

