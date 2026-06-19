from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.symmap.core import SymbolResolver
from fastdyn.binary.symmap.providers import DwarfProvider, ElfSymtabProvider


def run(context) -> None:
    resolver = SymbolResolver([
        DwarfProvider(include_variables=False),
        ElfSymtabProvider(),
    ])
    raw_symbols = resolver.resolve(context.config.binary_path)

    symbol_entries = []
    symbol_name_by_address = {}
    for symbol in raw_symbols.values():
        if isinstance(symbol.address, list):
            continue
        symbol_entries.append({
            "name": symbol.name,
            "address": hex(symbol.address),
            "size": symbol.size,
            "kind": symbol.kind,
            "provider": symbol.provider,
            "confidence": symbol.confidence,
        })
        symbol_name_by_address[symbol.address] = symbol.name

    symbol_entries.sort(key=lambda item: (int(item["address"], 16), item["name"]))

    context.shared["symbols"] = list(raw_symbols.values())
    context.shared["symbol_name_by_address"] = symbol_name_by_address
    write_json_artifact(context.cache_dir, "symbols.json", symbol_entries)
