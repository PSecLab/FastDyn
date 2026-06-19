from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.vector_table import extract_vector_table


def run(context) -> None:
    flash_base = context.config.flash_base if context.config.flash_base is not None else 0x08000000
    flash_end = flash_base + (context.config.flash_size if context.config.flash_size is not None else 0x200000)
    svd_irq_names = context.shared.get("svd_irq_names", {})

    vector_table = extract_vector_table(
        context.config.binary_path,
        init_nsvtor=context.config.init_nsvtor,
        flash_base=flash_base,
        flash_end=flash_end,
        svd_irq_names=svd_irq_names,
    )
    context.shared["vector_table"] = vector_table

    symbol_names = {}
    for symbol in context.shared.get("symbols", []):
        if isinstance(symbol.address, list):
            continue
        symbol_names[symbol.address] = symbol.name

    irq_handlers = []
    for entry in vector_table["entries"]:
        handler_address = int(entry["handler"], 16)
        irq_handlers.append({
            "index": entry["index"],
            "name": entry["name"],
            "handler_address": entry["handler"],
            "handler_symbol": symbol_names.get(handler_address),
            "irq_number": entry["irq_number"],
            "svd_name": entry["svd_name"],
        })

    write_json_artifact(context.cache_dir, "vector_table.json", vector_table)
    write_json_artifact(context.cache_dir, "irq_handlers.json", irq_handlers)
