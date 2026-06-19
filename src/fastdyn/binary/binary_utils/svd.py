from __future__ import annotations

from fastdyn.binary.binary_utils.elf import hex_addr


def _register_size_bytes(register) -> int:
    size_bits = getattr(register, "size", None)
    if isinstance(size_bits, int) and size_bits > 0:
        return max(1, (size_bits + 7) // 8)
    return 4


def _iter_register_entries(registers, base_address: int, prefix: str = ""):
    for item in registers or []:
        item_name = getattr(item, "name", None)
        item_prefix = f"{prefix}{item_name}" if item_name else prefix

        if hasattr(item, "registers") and getattr(item, "registers", None) is not None:
            cluster_offset = getattr(item, "address_offset", 0) or 0
            nested_prefix = f"{item_prefix}." if item_prefix else ""
            yield from _iter_register_entries(
                item.registers,
                base_address + cluster_offset,
                nested_prefix,
            )
            continue

        address_offset = getattr(item, "address_offset", None)
        if address_offset is None or item_name is None:
            continue

        yield {
            "name": item_prefix,
            "address": base_address + address_offset,
            "size_bits": getattr(item, "size", None),
            "size_bytes": _register_size_bytes(item),
        }


def build_svd_indices(svd_device) -> dict:
    peripheral_ranges = []
    irq_names = {}
    register_index = {}
    peripheral_map_entries = []

    for peripheral in svd_device.peripherals:
        register_entries = list(_iter_register_entries(
            getattr(peripheral, "registers", None),
            peripheral.base_address,
        ))

        block_sizes = [
            block.size
            for block in (getattr(peripheral, "address_blocks", None) or [])
            if getattr(block, "size", None)
        ]

        end_address = None
        if block_sizes:
            end_address = peripheral.base_address + max(block_sizes)
        elif register_entries:
            end_address = max(
                entry["address"] + entry["size_bytes"]
                for entry in register_entries
            )

        peripheral_ranges.append({
            "name": peripheral.name,
            "start": peripheral.base_address,
            "end": end_address,
        })

        for interrupt in peripheral.interrupts or []:
            irq_names[interrupt.value] = interrupt.name

        artifact_registers = []
        for entry in register_entries:
            if entry["address"] not in register_index:
                register_index[entry["address"]] = {
                    "peripheral": peripheral.name,
                    "register": entry["name"],
                }
            artifact_registers.append({
                "name": entry["name"],
                "address": hex_addr(entry["address"]),
                "size_bits": entry["size_bits"],
            })

        peripheral_map_entries.append({
            "name": peripheral.name,
            "base_address": hex_addr(peripheral.base_address),
            "end_address": hex_addr(end_address) if end_address is not None else None,
            "interrupts": [
                {
                    "name": interrupt.name,
                    "value": interrupt.value,
                }
                for interrupt in (peripheral.interrupts or [])
            ],
            "registers": artifact_registers,
        })

    peripheral_map_entries.sort(key=lambda item: item["base_address"] or "")
    peripheral_ranges.sort(key=lambda item: item["start"])

    return {
        "device_name": getattr(svd_device, "name", None),
        "irq_names": irq_names,
        "register_index": register_index,
        "peripheral_ranges": peripheral_ranges,
        "artifact": {
            "device_name": getattr(svd_device, "name", None),
            "peripherals": peripheral_map_entries,
        },
    }


def address_to_register(register_index: dict[int, dict], address: int) -> dict | None:
    return register_index.get(address)


def address_to_peripheral(peripheral_ranges: list[dict], address: int) -> str | None:
    for entry in peripheral_ranges:
        end_address = entry["end"]
        if end_address is None:
            continue
        if entry["start"] <= address < end_address:
            return entry["name"]
    return None
