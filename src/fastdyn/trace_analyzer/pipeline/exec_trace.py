from __future__ import annotations

from typing import Any

from ..models import ExecTraceContext, ResolvedFunction
from ..utils.symbols import parse_int, resolve_pc


DEFAULT_RECENT_BBL_LIMIT = 100
MAX_EXPANDED_BBL_TRACE = 20000
MAX_REPEAT_PER_ENTRY = 1000


def build_exec_trace_context(
    probe_result: dict[str, Any],
    *,
    functions: list[dict[str, Any]],
    symbols: list[dict[str, Any]],
    source_map: list[dict[str, Any]] | None = None,
    recent_limit: int = DEFAULT_RECENT_BBL_LIMIT,
) -> ExecTraceContext:
    """Build a bounded execution-trace context from a probe result."""
    if recent_limit <= 0:
        raise ValueError("recent_limit must be positive")

    bbl_trace = _bbl_trace(probe_result)
    recent_bbl_trace = bbl_trace[-recent_limit:]
    
    address_to_func = {}
    function_sequence = []
    
    for address in bbl_trace:
        if address not in address_to_func:
            resolved = resolve_pc(
                address,
                functions=functions,
                symbols=symbols,
                source_map=source_map,
            )
            entry = _resolved_bbl_entry(address, resolved)
            address_to_func[address] = _function_label(entry)
        function_sequence.append(address_to_func[address])

    resolved_recent_bbls = [
        _resolved_bbl_entry(
            address,
            resolve_pc(
                address,
                functions=functions,
                symbols=symbols,
                source_map=source_map,
            ),
        )
        for address in recent_bbl_trace
    ]

    final_pc = _final_pc(probe_result=probe_result, bbl_trace=bbl_trace)
    final_resolved = resolve_pc(
        final_pc,
        functions=functions,
        symbols=symbols,
        source_map=source_map,
    )

    compressed_sequence = _compress_full_trace(function_sequence)
    # Keep only the last N for the prompt to avoid it being absurdly long if there are many unique phases,
    # but compressed trace is usually very short (e.g. 10-50 elements).
    
    return ExecTraceContext(
        final_pc=final_pc,
        final_function=final_resolved.name,
        raw_bbl_count=len(bbl_trace),
        recent_bbl_trace=recent_bbl_trace,
        resolved_recent_bbls=resolved_recent_bbls,
        function_sequence=function_sequence,
        collapsed_sequence=[{"name": step, "count": 1} for step in compressed_sequence], # Store compressed steps in the existing field format
        loop_hint=_detect_loop(recent_bbl_trace, resolved_recent_bbls),
        panic_string=probe_result.get("panic_string"),
        fault_stacked_pc=probe_result.get("stacked_pc"),
        fault_cfsr=probe_result.get("cfsr"),
        fault_bfar=probe_result.get("bfar"),
        timeout_registers=probe_result.get("registers"),
    )


def _compress_full_trace(seq: list[str]) -> list[str]:
    compressed = []
    i = 0
    while i < len(seq):
        best_len = 1
        best_count = 1
        # To avoid O(N^2) hangs, restrict pattern length to say 50 max
        max_p = min(50, (len(seq) - i) // 2)
        for p in range(1, max_p + 1):
            pattern = seq[i : i+p]
            count = 1
            while i + count * p + p <= len(seq) and seq[i + count * p : i + count * p + p] == pattern:
                count += 1
            if count > 1 and count * p > best_len * best_count:
                best_len = p
                best_count = count
                
        if best_count > 1:
            if best_len == 1:
                # e.g. [Loop x100: stm32_clock_init]
                compressed.append(f"[Loop x{best_count}: {seq[i]}]")
            else:
                compressed.append(f"[Loop x{best_count}: {' -> '.join(seq[i : i+best_len])}]")
            i += best_len * best_count
        else:
            compressed.append(seq[i])
            i += 1
    
    # Cap to last 200 elements just in case the compressed trace is somehow still giant
    return compressed[-200:]


def _bbl_trace(probe_result: dict[str, Any]) -> list[str]:
    raw_trace = probe_result.get("bbl_trace") or []
    if not isinstance(raw_trace, list):
        raise TypeError("probe_result.bbl_trace must be a list")

    trace: list[str] = []
    for idx, item in enumerate(raw_trace):
        try:
            if isinstance(item, dict):
                pc_str = _format_pc(item["pc"])
                count = max(0, int(item.get("count", 1)))
                repeat_count = min(count, MAX_REPEAT_PER_ENTRY)
                if repeat_count:
                    trace.extend([pc_str] * repeat_count)
            else:
                trace.append(_format_pc(item))
            if len(trace) > MAX_EXPANDED_BBL_TRACE:
                del trace[:-MAX_EXPANDED_BBL_TRACE]
        except (TypeError, ValueError, KeyError) as exc:
            raise ValueError(f"invalid BBL address at index {idx}: {item!r}") from exc
    return trace


def _final_pc(*, probe_result: dict[str, Any], bbl_trace: list[str]) -> str:
    pc = probe_result.get("pc")
    if pc is not None:
        return _format_pc(pc)
    if bbl_trace:
        return bbl_trace[-1]
    return "0x0"


def _format_pc(value: Any) -> str:
    if isinstance(value, str):
        text = value.strip().lower()
        parse_int(text)
        if text.startswith("0x"):
            return f"0x{text[2:]}"
    return f"0x{parse_int(value):x}"


def _resolved_bbl_entry(address: str, resolved: ResolvedFunction) -> dict[str, Any]:
    return {
        "address": address,
        "function": resolved.name,
        "symbol": resolved.symbol_name,
        "offset": resolved.offset,
        "confidence": resolved.confidence,
        "source_root_relative": resolved.source_root_relative,
        "line": resolved.line,
    }


def _function_label(entry: dict[str, Any]) -> str:
    function = entry.get("function")
    if isinstance(function, str) and function:
        return function
    symbol = entry.get("symbol")
    if isinstance(symbol, str) and symbol:
        return symbol
    return str(entry["address"])


def _collapse_consecutive(items: list[str]) -> list[dict[str, Any]]:
    collapsed: list[dict[str, Any]] = []
    for item in items:
        if collapsed and collapsed[-1]["name"] == item:
            collapsed[-1]["count"] += 1
            continue
        collapsed.append({"name": item, "count": 1})
    return collapsed


def _detect_loop(
    recent_bbl_trace: list[str],
    resolved_recent_bbls: list[dict[str, Any]],
) -> dict[str, Any] | None:
    repeated = _detect_repeated_bbl(recent_bbl_trace, resolved_recent_bbls)
    if repeated is not None:
        return repeated
    return _detect_alternating_bbl_pair(recent_bbl_trace, resolved_recent_bbls)


def _detect_repeated_bbl(
    recent_bbl_trace: list[str],
    resolved_recent_bbls: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if len(recent_bbl_trace) < 3:
        return None
    final_bbl = recent_bbl_trace[-1]
    repeat_count = 0
    for address in reversed(recent_bbl_trace):
        if address != final_bbl:
            break
        repeat_count += 1
    if repeat_count < 3:
        return None

    final_entry = resolved_recent_bbls[-1] if resolved_recent_bbls else {}
    return {
        "kind": "repeated_bbl",
        "bbl": final_bbl,
        "function": _function_label(final_entry) if final_entry else final_bbl,
        "repeat_count": repeat_count,
        "tail_length": repeat_count,
    }


def _detect_alternating_bbl_pair(
    recent_bbl_trace: list[str],
    resolved_recent_bbls: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if len(recent_bbl_trace) < 6:
        return None
    pair = recent_bbl_trace[-2:]
    if pair[0] == pair[1]:
        return None

    repeat_count = 0
    cursor = len(recent_bbl_trace)
    while cursor >= 2 and recent_bbl_trace[cursor - 2:cursor] == pair:
        repeat_count += 1
        cursor -= 2

    if repeat_count < 3:
        return None

    pair_entries = resolved_recent_bbls[-2:] if len(resolved_recent_bbls) >= 2 else []
    function_pair = [_function_label(entry) for entry in pair_entries]
    return {
        "kind": "alternating_bbl_pair",
        "bbl_pair": pair,
        "function_pair": function_pair,
        "repeat_count": repeat_count,
        "tail_length": repeat_count * 2,
    }
