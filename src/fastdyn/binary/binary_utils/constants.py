from __future__ import annotations

import struct

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM

from fastdyn import fastdyn_log as fastdyn_log_conf
from fastdyn.binary.binary_utils.elf import hex_addr, read_text_section, read_vaddr_bytes
from fastdyn.binary.binary_utils.svd import address_to_peripheral, address_to_register


fastdyn_log = fastdyn_log_conf.getFastdynLogger()

_ADDR_MIN = 0x0800_0000
_ADDR_MAX = 0x6000_0000


def _u32(value: int) -> int:
    return value & 0xFFFF_FFFF


def _looks_like_address(value: int) -> bool:
    return _ADDR_MIN <= value < _ADDR_MAX


def _nearest_function(addr: int, functions: list[dict]) -> str | None:
    for function in functions:
        start = function.get("start", 0)
        end = function.get("end", start + function.get("size", 0))
        if start <= addr < end:
            return function.get("name")
    return None


def extract_constants(binary_path: str, functions: list[dict], max_constants: int = 5000) -> list[dict]:
    results = []
    seen = set()

    disassembler = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    disassembler.detail = True

    for function in functions:
        function_name = function.get("name", "<unknown>")
        function_start = function.get("start", 0)
        function_end = function.get("end", function_start + function.get("size", 0))
        function_size = function_end - function_start
        if function_size <= 0:
            continue

        blob = read_vaddr_bytes(binary_path, function_start, function_size)
        if not blob:
            continue

        movw_pending = {}

        for instruction in disassembler.disasm(blob, function_start):
            mnemonic = instruction.mnemonic.lower()
            operands = instruction.operands

            if mnemonic == "movw" and len(operands) == 2 and operands[1].type == ARM_OP_IMM:
                movw_pending[operands[0].reg] = operands[1].imm & 0xFFFF
                continue

            if mnemonic == "movt" and len(operands) == 2 and operands[1].type == ARM_OP_IMM:
                destination = operands[0].reg
                full_value = _u32(((operands[1].imm & 0xFFFF) << 16) | movw_pending.pop(destination, 0))
                if _looks_like_address(full_value):
                    key = (full_value, function_name)
                    if key not in seen:
                        seen.add(key)
                        results.append({
                            "value_hex": hex_addr(full_value),
                            "function": function_name,
                            "source_method": "immediate",
                            "instruction": "movw/movt pair",
                            "pc_hex": hex_addr(instruction.address),
                            "confidence": "low",
                        })
                        if len(results) >= max_constants:
                            return results
                continue

            for operand in operands:
                if operand.type != ARM_OP_IMM:
                    continue

                value = _u32(operand.imm)
                if not _looks_like_address(value):
                    continue

                key = (value, function_name)
                if key in seen:
                    continue

                seen.add(key)
                results.append({
                    "value_hex": hex_addr(value),
                    "function": function_name,
                    "source_method": "immediate",
                    "instruction": f"{instruction.mnemonic} {instruction.op_str}",
                    "pc_hex": hex_addr(instruction.address),
                    "confidence": "low",
                })
                if len(results) >= max_constants:
                    return results

            for written_register in getattr(instruction, "regs_write", []):
                movw_pending.pop(written_register, None)

    fastdyn_log.info("Constants: found %d address-like immediates", len(results))
    return results


def extract_literal_pools(binary_path: str, functions: list[dict], max_entries: int = 5000) -> list[dict]:
    text_result = read_text_section(binary_path)
    if text_result is None:
        return []

    text_data, text_base = text_result
    literal_targets = set()

    disassembler = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    disassembler.detail = True

    for instruction in disassembler.disasm(text_data, text_base):
        if not instruction.mnemonic.lower().startswith("ldr"):
            continue

        for operand in instruction.operands:
            if operand.type != ARM_OP_MEM:
                continue
            if operand.mem.base != 15:
                continue
            pc_aligned = (instruction.address + 4) & ~3
            literal_targets.add(pc_aligned + operand.mem.disp)

    results = []
    for offset in range(0, len(text_data) - 3, 4):
        word = struct.unpack_from("<I", text_data, offset)[0]
        if not _looks_like_address(word):
            continue

        literal_address = text_base + offset
        is_ldr_target = literal_address in literal_targets
        is_in_function = _nearest_function(literal_address, functions) is not None
        if not is_ldr_target and is_in_function:
            continue

        results.append({
            "literal_address_hex": hex_addr(literal_address),
            "literal_value_hex": hex_addr(word),
            "section": ".text",
            "nearby_function": _nearest_function(literal_address, functions),
            "is_peripheral_range": 0x4000_0000 <= word < 0x6000_0000,
        })
        if len(results) >= max_entries:
            break

    fastdyn_log.info("Constants: found %d literal-pool entries", len(results))
    return results


def extract_mmio_constants(
    constants: list[dict],
    literal_pools: list[dict],
    register_index: dict[int, dict],
    peripheral_ranges: list[dict],
) -> list[dict]:
    results = []
    seen = set()

    def try_match(value: int, evidence_type: str, function_name: str | None) -> None:
        dedup_key = (value, evidence_type, function_name)
        if dedup_key in seen:
            return
        seen.add(dedup_key)

        register_info = address_to_register(register_index, value)
        if register_info is not None:
            results.append({
                "value_hex": hex_addr(value),
                "peripheral": register_info["peripheral"],
                "register": register_info["register"],
                "evidence_type": evidence_type,
                "function": function_name,
                "confidence": "medium",
                "proven_access": False,
            })
            return

        peripheral_name = address_to_peripheral(peripheral_ranges, value)
        if peripheral_name is not None:
            results.append({
                "value_hex": hex_addr(value),
                "peripheral": peripheral_name,
                "register": None,
                "evidence_type": evidence_type,
                "function": function_name,
                "confidence": "low",
                "proven_access": False,
            })

    for entry in constants:
        try_match(int(entry["value_hex"], 16), "immediate", entry.get("function"))

    for entry in literal_pools:
        try_match(int(entry["literal_value_hex"], 16), "literal_pool", entry.get("nearby_function"))

    return results


def build_peripheral_hint_summary(mmio_constants: list[dict]) -> dict:
    summary = {}

    for entry in mmio_constants:
        peripheral_name = entry["peripheral"]
        bucket = summary.setdefault(
            peripheral_name,
            {
                "evidence_count": 0,
                "exact_register_constants": [],
                "functions_with_hints": [],
                "confidence": "medium",
                "note": "Static constants only; not runtime accesses.",
            },
        )

        bucket["evidence_count"] += 1

        register_name = entry.get("register")
        if register_name is not None:
            register_label = f"{register_name}@{entry['value_hex']}"
            if register_label not in bucket["exact_register_constants"]:
                bucket["exact_register_constants"].append(register_label)

        function_name = entry.get("function")
        if function_name is not None and function_name not in bucket["functions_with_hints"]:
            bucket["functions_with_hints"].append(function_name)

    return summary
