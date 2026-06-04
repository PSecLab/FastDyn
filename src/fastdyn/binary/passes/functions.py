from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact


def run(context) -> None:
    symbols = context.shared.get("symbols", [])

    function_entries = []
    function_ranges = []
    for symbol in symbols:
        if symbol.kind != "function" or symbol.size is None or symbol.size <= 0:
            continue

        function_ranges.append({
            "name": symbol.name,
            "start": symbol.address,
            "size": symbol.size,
        })
        function_entries.append({
            "name": symbol.name,
            "start": hex(symbol.address),
            "end": hex(symbol.address + symbol.size),
            "size": symbol.size,
            "provider": symbol.provider,
            "confidence": "high" if symbol.confidence >= 0.9 else "medium",
        })

    function_entries.sort(key=lambda item: int(item["start"], 16))
    function_ranges.sort(key=lambda item: item["start"])
    context.shared["functions"] = function_ranges
    write_json_artifact(context.cache_dir, "functions.json", function_entries)
