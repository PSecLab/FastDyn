from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class SourceSlice:
    start_line: int | None
    end_line: int | None
    text: str
    extraction: str
    warnings: list[str] = field(default_factory=list)


def extract_source_context(
    path: Path,
    *,
    anchor_line: int,
    max_lines: int = 260,
    fallback_before: int = 40,
    fallback_after: int = 80,
    function_search_lines: int = 80,
) -> SourceSlice:
    if max_lines <= 0:
        raise ValueError("max_lines must be positive")
    if fallback_before < 0 or fallback_after < 0:
        raise ValueError("fallback line counts must be non-negative")
    if anchor_line <= 0:
        return SourceSlice(
            start_line=None,
            end_line=None,
            text="",
            extraction="unavailable",
            warnings=[f"invalid source anchor line: {anchor_line}"],
        )

    lines = read_source_lines(path)
    if not lines:
        return SourceSlice(
            start_line=None,
            end_line=None,
            text="",
            extraction="unavailable",
            warnings=[f"source file is empty: {path}"],
        )

    anchor_idx = min(anchor_line - 1, len(lines) - 1)
    
    # Brace parsing is broken because anchor_idx is the PC line inside the function body,
    # so it just finds the next nested block (e.g. an if-statement) and extracts only that.
    # Fallback to a fixed window around the PC line.
    return _line_window(
        lines,
        anchor_idx=anchor_idx,
        before=100,
        after=100,
        max_lines=max_lines,
        warnings=["using bounded line window around PC"],
    )




def read_source_lines(path: Path) -> list[str]:
    with path.open("r", encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def _find_body_start(
    lines: list[str],
    *,
    anchor_idx: int,
    search_lines: int,
) -> int | None:
    end_idx = min(len(lines), anchor_idx + search_lines + 1)
    in_block_comment = False
    for idx in range(anchor_idx, end_idx):
        delta, in_block_comment, saw_open = _brace_delta(
            lines[idx],
            in_block_comment=in_block_comment,
        )
        if saw_open:
            return idx
    return None


def _find_body_end(lines: list[str], body_start_idx: int) -> int | None:
    balance = 0
    saw_open = False
    in_block_comment = False
    for idx in range(body_start_idx, len(lines)):
        delta, in_block_comment, line_saw_open = _brace_delta(
            lines[idx],
            in_block_comment=in_block_comment,
        )
        if line_saw_open:
            saw_open = True
        balance += delta
        if saw_open and balance <= 0:
            return idx
    return None


def _find_declaration_start(lines: list[str], body_start_idx: int) -> int:
    start_idx = body_start_idx
    floor = max(0, body_start_idx - 40)
    for idx in range(body_start_idx - 1, floor - 1, -1):
        stripped = lines[idx].strip()
        if not stripped:
            break
        if stripped.endswith(";") or stripped.endswith("}"):
            break
        start_idx = idx
    return start_idx


def _line_window(
    lines: list[str],
    *,
    anchor_idx: int,
    before: int,
    after: int,
    max_lines: int,
    warnings: list[str],
) -> SourceSlice:
    start_idx = max(0, anchor_idx - before)
    end_idx = min(len(lines) - 1, anchor_idx + after)
    if end_idx - start_idx + 1 > max_lines:
        end_idx = start_idx + max_lines - 1
        warnings = [*warnings, f"line window truncated to {max_lines} lines"]
    return _slice_lines(
        lines,
        start_idx=start_idx,
        end_idx=end_idx,
        extraction="line_window",
        warnings=warnings,
    )


def _slice_lines(
    lines: list[str],
    *,
    start_idx: int,
    end_idx: int,
    extraction: str,
    warnings: list[str],
) -> SourceSlice:
    return SourceSlice(
        start_line=start_idx + 1,
        end_line=end_idx + 1,
        text="\n".join(lines[start_idx:end_idx + 1]),
        extraction=extraction,
        warnings=warnings,
    )


def _brace_delta(
    line: str,
    *,
    in_block_comment: bool,
) -> tuple[int, bool, bool]:
    delta = 0
    saw_open = False
    quote: str | None = None
    escaped = False
    idx = 0

    while idx < len(line):
        char = line[idx]
        next_char = line[idx + 1] if idx + 1 < len(line) else ""

        if in_block_comment:
            if char == "*" and next_char == "/":
                in_block_comment = False
                idx += 2
                continue
            idx += 1
            continue

        if quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            idx += 1
            continue

        if char == "/" and next_char == "*":
            in_block_comment = True
            idx += 2
            continue
        if char == "/" and next_char == "/":
            break
        if char in {"'", '"'}:
            quote = char
        elif char == "{":
            delta += 1
            saw_open = True
        elif char == "}":
            delta -= 1
        idx += 1

    return delta, in_block_comment, saw_open
