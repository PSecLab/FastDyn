from __future__ import annotations

from typing import Iterable

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

from fastdyn.binary.symmap.core import SymbolInfo, SymbolProvider


class ElfSymtabProvider(SymbolProvider):
    @property
    def name(self) -> str:
        return "elf_symtab"

    def is_applicable(self, binary_path: str) -> bool:
        try:
            with open(binary_path, "rb") as f:
                elf_file = ELFFile(f)
                return any(isinstance(section, SymbolTableSection) for section in elf_file.iter_sections())
        except Exception:
            return False

    def get_symbols(self, binary_path: str) -> Iterable[SymbolInfo]:
        with open(binary_path, "rb") as f:
            elf_file = ELFFile(f)
            for section in elf_file.iter_sections():
                if not isinstance(section, SymbolTableSection):
                    continue
                yield from self._extract_from_table(section)

    def _extract_from_table(self, table: SymbolTableSection) -> Iterable[SymbolInfo]:
        kind_map = {
            "STT_FUNC": "function",
            "STT_OBJECT": "variable",
            "STT_NOTYPE": "label",
        }

        for symbol in table.iter_symbols():
            if not symbol.name:
                continue

            address = symbol.entry.st_value
            if address == 0:
                continue

            kind = kind_map.get(symbol.entry.st_info.type, "unknown")
            if kind not in ("function", "variable"):
                continue

            yield SymbolInfo(
                name=symbol.name,
                address=address,
                size=symbol.entry.st_size or None,
                kind=kind,
                provider=self.name,
                confidence=0.9,
            )
