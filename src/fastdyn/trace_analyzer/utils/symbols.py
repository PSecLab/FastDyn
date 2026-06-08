from __future__ import annotations

from pathlib import Path
from typing import Any

from ..models import ResolvedFunction


def parse_int(value: Any) -> int:
    if isinstance(value, bool):
        raise ValueError(f"cannot parse boolean as integer: {value!r}")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip()
        if not text:
            raise ValueError("cannot parse empty string as integer")
        return int(text, 0)
    raise TypeError(f"cannot parse integer from {type(value).__name__}")


def format_hex(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:x}"


def resolve_pc(
    pc: int | str,
    *,
    functions: list[dict[str, Any]],
    symbols: list[dict[str, Any]],
    source_map: list[dict[str, Any]] | None = None,
    max_nearest_symbol_distance: int = 0x10000,
) -> ResolvedFunction:
    pc_int = parse_int(pc)
    function_match = _resolve_from_functions(pc_int, functions)
    symbol_match = _resolve_from_symbols(
        pc_int,
        symbols,
        max_nearest_symbol_distance=max_nearest_symbol_distance,
    )

    if function_match is not None:
        name = function_match.get("name")
        start = parse_int(function_match.get("start"))
        end = _function_end(function_match, start)
        size = end - start if end is not None else _safe_parse_int(function_match.get("size"))
        source_entry = _source_entry_for_function(name, source_map or [])
        return ResolvedFunction(
            pc=pc_int,
            pc_hex=format_hex(pc_int) or "0x0",
            name=name,
            symbol_name=symbol_match.get("name") if symbol_match else name,
            start_address=start,
            start_address_hex=format_hex(start),
            size=size,
            offset=pc_int - start,
            source_path=_source_path(source_entry),
            source_root_relative=source_entry.get("source_root_relative") if source_entry else None,
            line=source_entry.get("line") if source_entry else None,
            confidence=str(function_match.get("confidence") or "function_interval"),
            source_map_entry=source_entry or {},
        )

    if symbol_match is not None:
        name = symbol_match.get("name")
        start = parse_int(symbol_match.get("address"))
        size = _safe_parse_int(symbol_match.get("size"))
        source_entry = _source_entry_for_function(name, source_map or [])
        return ResolvedFunction(
            pc=pc_int,
            pc_hex=format_hex(pc_int) or "0x0",
            name=name,
            symbol_name=name,
            start_address=start,
            start_address_hex=format_hex(start),
            size=size,
            offset=pc_int - start,
            source_path=_source_path(source_entry),
            source_root_relative=source_entry.get("source_root_relative") if source_entry else None,
            line=source_entry.get("line") if source_entry else None,
            confidence="nearest_symbol",
            source_map_entry=source_entry or {},
        )

    return ResolvedFunction(
        pc=pc_int,
        pc_hex=format_hex(pc_int) or "0x0",
        confidence="unresolved",
    )


def resolve_pc_name(
    pc: int | str,
    *,
    functions: list[dict[str, Any]],
    symbols: list[dict[str, Any]],
) -> str:
    resolved = resolve_pc(pc, functions=functions, symbols=symbols)
    return resolved.name or resolved.pc_hex


def _resolve_from_functions(pc: int, functions: list[dict[str, Any]]) -> dict[str, Any] | None:
    best: tuple[int, dict[str, Any]] | None = None
    for entry in functions:
        start = _safe_parse_int(entry.get("start"))
        if start is None or start > pc:
            continue
        end = _function_end(entry, start)
        if end is None or pc >= end:
            continue
        distance = pc - start
        if best is None or distance < best[0]:
            best = (distance, entry)
    return best[1] if best else None


def _resolve_from_symbols(
    pc: int,
    symbols: list[dict[str, Any]],
    *,
    max_nearest_symbol_distance: int,
) -> dict[str, Any] | None:
    best: tuple[int, dict[str, Any]] | None = None
    for entry in symbols:
        address = _safe_parse_int(entry.get("address"))
        if address is None or address > pc:
            continue
        if address == 0 and pc != 0:
            continue
        size = _safe_parse_int(entry.get("size"))
        if size is not None and size > 0 and pc >= address + size:
            continue
        distance = pc - address
        if size is None and distance > max_nearest_symbol_distance:
            continue
        if best is None or distance < best[0]:
            best = (distance, entry)
    return best[1] if best else None


def _function_end(entry: dict[str, Any], start: int) -> int | None:
    end = _safe_parse_int(entry.get("end"))
    if end is not None:
        return end
    size = _safe_parse_int(entry.get("size"))
    if size is not None:
        return start + size
    return None


def _source_entry_for_function(
    function_name: str | None,
    source_map: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if not function_name:
        return None
    for entry in source_map:
        if entry.get("function") == function_name and entry.get("exists_locally"):
            return entry
    for entry in source_map:
        if entry.get("function") == function_name:
            return entry
    return None


def _source_path(entry: dict[str, Any] | None) -> Path | None:
    if not entry:
        return None
    resolved_path = entry.get("resolved_path")
    return Path(resolved_path) if resolved_path else None


def _safe_parse_int(value: Any) -> int | None:
    if value is None:
        return None
    try:
        return parse_int(value)
    except (TypeError, ValueError):
        return None
