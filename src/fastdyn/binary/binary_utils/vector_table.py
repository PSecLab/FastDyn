from __future__ import annotations

import struct

from fastdyn.binary.binary_utils.elf import read_vaddr_bytes


_CORTEXM_EXCEPTIONS = [
    "InitialSP",
    "Reset",
    "NMI",
    "HardFault",
    "MemManage",
    "BusFault",
    "UsageFault",
    "Reserved_7",
    "Reserved_8",
    "Reserved_9",
    "Reserved_10",
    "SVCall",
    "DebugMonitor",
    "Reserved_13",
    "PendSV",
    "SysTick",
]

def _is_valid_handler(value: int, flash_base: int, flash_end: int) -> bool:
    if value == 0:
        return False
    address = value & ~1
    return flash_base <= address < flash_end


def extract_vector_table(
    binary_path: str,
    *,
    init_nsvtor: int | None = None,
    flash_base: int = 0x08000000,
    flash_end: int = 0x08200000,
    svd_irq_names: dict[int, str] | None = None,
    max_vectors: int = 256,
) -> dict:
    table_base = init_nsvtor if init_nsvtor is not None else flash_base
    raw = read_vaddr_bytes(binary_path, table_base, max_vectors * 4)
    if len(raw) < 8:
        return {
            "base_address": hex(table_base),
            "init_sp": None,
            "reset_vector": None,
            "entries": [],
        }

    entry_count = len(raw) // 4
    words = struct.unpack(f"<{entry_count}I", raw[: entry_count * 4])

    entries = []
    consecutive_invalid = 0
    svd_irq_names = svd_irq_names or {}

    for index, handler in enumerate(words):
        if index == 0:
            entries.append({
                "index": 0,
                "handler": hex(handler),
                "name": "InitialSP",
                "irq_number": None,
                "svd_name": None,
            })
            continue

        is_valid = _is_valid_handler(handler, flash_base, flash_end)
        if not is_valid:
            consecutive_invalid += 1
            if index >= 16 and consecutive_invalid > 8:
                break
        else:
            consecutive_invalid = 0

        if index < len(_CORTEXM_EXCEPTIONS):
            name = _CORTEXM_EXCEPTIONS[index]
        else:
            irq_number = index - 16
            name = svd_irq_names.get(irq_number, f"IRQ{irq_number}")

        irq_number = index - 16 if index >= 16 else None
        entries.append({
            "index": index,
            "handler": hex(handler & ~1),
            "name": name,
            "irq_number": irq_number,
            "svd_name": svd_irq_names.get(irq_number) if irq_number is not None else None,
        })

    return {
        "base_address": hex(table_base),
        "init_sp": hex(words[0]),
        "reset_vector": hex(words[1] & ~1),
        "entries": entries,
    }
