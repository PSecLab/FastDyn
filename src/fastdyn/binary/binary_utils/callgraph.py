from __future__ import annotations

from collections import defaultdict

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from capstone.arm import ARM_OP_IMM, ARM_OP_REG

from fastdyn import fastdyn_log as fastdyn_log_conf
from fastdyn.binary.binary_utils.elf import hex_addr, read_vaddr_bytes


fastdyn_log = fastdyn_log_conf.getFastdynLogger()

_MAX_FUNCTION_SIZE = 1_000_000


def _make_disassembler() -> Cs:
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    disassembler.detail = True
    return disassembler


def _resolve_callee(target_addr: int, symbol_index: dict[int, str]) -> tuple[str | None, int | None]:
    name = symbol_index.get(target_addr)
    if name is not None:
        return name, target_addr

    cleaned = target_addr & ~1
    name = symbol_index.get(cleaned)
    if name is not None:
        return name, cleaned

    return None, None


def _classify_instruction(instruction) -> str | None:
    mnemonic = instruction.mnemonic.lower()

    if mnemonic == "bl":
        return "direct-bl"

    if mnemonic == "blx":
        for operand in instruction.operands:
            if operand.type == ARM_OP_REG:
                return "indirect-blx-register"
            if operand.type == ARM_OP_IMM:
                return "direct-blx"
        return "indirect-blx-register"

    return None


def build_callgraph(binary_path: str, functions: list[dict], symbol_index: dict[int, str]) -> list[dict]:
    disassembler = _make_disassembler()
    edges = []

    total = len(functions)
    for index, function in enumerate(functions):
        function_name = function["name"]
        function_start = function["start"]
        function_size = function["size"]

        if function_size <= 0 or function_size >= _MAX_FUNCTION_SIZE:
            continue

        if index > 0 and index % 1000 == 0:
            fastdyn_log.info("Callgraph: processed %d / %d functions", index, total)

        code = read_vaddr_bytes(binary_path, function_start, function_size)
        if not code:
            continue

        for instruction in disassembler.disasm(code, function_start):
            kind = _classify_instruction(instruction)
            if kind is None:
                continue

            edge = {
                "caller": function_name,
                "caller_addr": hex_addr(function_start),
                "callsite": hex_addr(instruction.address),
                "callee": None,
                "callee_addr": None,
                "kind": kind,
                "confidence": "unresolved",
            }

            if kind in ("direct-bl", "direct-blx"):
                target_addr = None
                for operand in instruction.operands:
                    if operand.type == ARM_OP_IMM:
                        target_addr = operand.imm
                        break

                if target_addr is not None:
                    callee_name, callee_addr = _resolve_callee(target_addr, symbol_index)
                    if callee_name is not None:
                        edge["callee"] = callee_name
                        edge["callee_addr"] = hex_addr(callee_addr)
                        edge["confidence"] = "high"
                    else:
                        edge["callee_addr"] = hex_addr(target_addr)

            edges.append(edge)

    return edges


def build_reset_path(reset_addr: int, callgraph: list[dict], max_depth: int = 10) -> dict:
    caller_map: dict[str, list[dict]] = defaultdict(list)
    for edge in callgraph:
        if edge["confidence"] == "high":
            caller_map[edge["caller_addr"]].append(edge)

    reset_hex = hex_addr(reset_addr)
    path = []
    visited = set()
    limitations = []
    current_addr = reset_hex

    for depth in range(max_depth):
        outgoing = caller_map.get(current_addr, [])
        if not outgoing:
            limitations.append(f"No outgoing direct calls from {current_addr} at depth {depth}")
            break

        edge = sorted(outgoing, key=lambda item: item["callsite"])[0]
        path.append({
            "depth": depth,
            "caller": edge["caller"],
            "caller_addr": edge["caller_addr"],
            "callsite": edge["callsite"],
            "callee": edge["callee"],
            "callee_addr": edge["callee_addr"],
        })

        next_addr = edge.get("callee_addr")
        if next_addr is None:
            limitations.append(f"Callee address unknown at depth {depth}")
            break

        if next_addr in visited:
            limitations.append(f"Cycle detected at {next_addr} (depth {depth})")
            break

        visited.add(next_addr)
        current_addr = next_addr

    if len(path) == max_depth:
        limitations.append(f"Reached max_depth={max_depth}")

    return {
        "method": "static-direct-call-follow",
        "reset_handler": reset_hex,
        "path": path,
        "max_depth": max_depth,
        "limitations": limitations,
    }
