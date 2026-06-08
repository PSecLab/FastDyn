from __future__ import annotations

from pathlib import Path
from typing import Any

from ..models import ResolvedFunction, SourceContext
from ..utils.source import extract_source_context
from ..utils.symbols import parse_int


def build_source_context(
    resolved_function: ResolvedFunction,
    *,
    source_map: list[dict[str, Any]] | None = None,
    max_lines: int = 260,
) -> SourceContext:
    source_entry = _source_entry(resolved_function, source_map or [])
    function_name = resolved_function.name or resolved_function.symbol_name
    source_path = _source_path(resolved_function, source_entry)
    source_root_relative = _source_root_relative(resolved_function, source_entry)
    line = _source_line(resolved_function, source_entry)
    provider = _provider(source_entry)
    warnings: list[str] = []

    if source_path is None:
        warnings.append("resolved function has no local source path")
        return _unavailable_context(
            function=function_name,
            source_path=None,
            source_root_relative=source_root_relative,
            line=line,
            provider=provider,
            warnings=warnings,
        )
    if not source_path.exists():
        warnings.append(f"source file does not exist: {source_path}")
        return _unavailable_context(
            function=function_name,
            source_path=source_path,
            source_root_relative=source_root_relative,
            line=line,
            provider=provider,
            warnings=warnings,
        )
    if line is None:
        warnings.append("resolved function has no source line")
        return _unavailable_context(
            function=function_name,
            source_path=source_path,
            source_root_relative=source_root_relative,
            line=None,
            provider=provider,
            warnings=warnings,
        )

    source_slice = extract_source_context(
        source_path,
        anchor_line=line,
        max_lines=max_lines,
    )

    return SourceContext(
        function=function_name,
        source_path=source_path,
        source_root_relative=source_root_relative,
        line=line,
        start_line=source_slice.start_line,
        end_line=source_slice.end_line,
        text=source_slice.text,
        extraction=source_slice.extraction,
        provider=provider,
        warnings=[*warnings, *source_slice.warnings],
    )


def _unavailable_context(
    *,
    function: str | None,
    source_path: Path | None,
    source_root_relative: str | None,
    line: int | None,
    provider: str | None,
    warnings: list[str],
) -> SourceContext:
    return SourceContext(
        function=function,
        source_path=source_path,
        source_root_relative=source_root_relative,
        line=line,
        start_line=None,
        end_line=None,
        text="",
        extraction="unavailable",
        provider=provider,
        warnings=warnings,
    )


def _source_entry(
    resolved_function: ResolvedFunction,
    source_map: list[dict[str, Any]],
) -> dict[str, Any]:
    if resolved_function.source_map_entry:
        return resolved_function.source_map_entry

    function_name = resolved_function.name or resolved_function.symbol_name
    if function_name is None:
        return {}
    for entry in source_map:
        if entry.get("function") == function_name and entry.get("exists_locally"):
            return entry
    for entry in source_map:
        if entry.get("function") == function_name:
            return entry
    return {}


def _source_path(
    resolved_function: ResolvedFunction,
    source_entry: dict[str, Any],
) -> Path | None:
    if resolved_function.source_path is not None:
        return resolved_function.source_path
    resolved_path = source_entry.get("resolved_path")
    if isinstance(resolved_path, str) and resolved_path:
        return Path(resolved_path)
    return None


def _source_root_relative(
    resolved_function: ResolvedFunction,
    source_entry: dict[str, Any],
) -> str | None:
    if resolved_function.source_root_relative:
        return resolved_function.source_root_relative
    source_root_relative = source_entry.get("source_root_relative")
    if isinstance(source_root_relative, str) and source_root_relative:
        return source_root_relative
    return None


def _source_line(
    resolved_function: ResolvedFunction,
    source_entry: dict[str, Any],
) -> int | None:
    if resolved_function.line is not None:
        return resolved_function.line
    line = source_entry.get("line")
    if line is None:
        return None
    try:
        return parse_int(line)
    except (TypeError, ValueError):
        return None


def _provider(source_entry: dict[str, Any]) -> str | None:
    provider = source_entry.get("provider")
    if isinstance(provider, str) and provider:
        return provider
    return None
