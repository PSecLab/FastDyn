from __future__ import annotations

import hashlib
import json
import os
import re
import shlex
from pathlib import Path
from typing import Any


_MACRO_NAME_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*(?:\([^)]*\))?)(?:\s+(.*))?$")
_DEFINE_RE = re.compile(r"^\s*#\s*define\s+(.+?)\s*$")
_READ_ELF_OFFSET_RE = re.compile(r"^\s*Offset:\s+0x([0-9a-fA-F]+)\s*$")
_READ_ELF_DEFINE_RE = re.compile(
    r"^\s*DW_MACRO_define(?:_[a-z0-9]+)?\s+-\s+lineno\s*:\s*(\d+)\s+macro\s*:\s*(.*)$"
)
_READ_ELF_UNDEF_RE = re.compile(
    r"^\s*DW_MACRO_undef(?:_[a-z0-9]+)?\s+-\s+lineno\s*:\s*(\d+)\s+macro\s*:\s*(.*)$"
)
_READ_ELF_IMPORT_RE = re.compile(
    r"^\s*DW_MACRO_import\s+-\s+offset\s*:\s*0x([0-9a-fA-F]+)\s*$"
)
_READ_ELF_START_FILE_RE = re.compile(
    r"^\s*DW_MACRO_start_file\s+-\s+lineno:\s*(\d+)\s+filenum:\s*(\d+)\s+filename:\s*(.*)$"
)


def stable_unit_id(unit: dict[str, Any], index: int | None = None) -> str:
    identity = "|".join(
        str(part or "")
        for part in (
            unit.get("resolved_path"),
            unit.get("source_root_relative"),
            unit.get("original_file") or unit.get("name"),
            unit.get("original_comp_dir") or unit.get("comp_dir"),
            index if index is not None else "",
        )
    )
    digest = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:12]
    path_hint = (
        unit.get("source_root_relative")
        or unit.get("original_file")
        or unit.get("name")
        or "compile_unit"
    )
    stem = Path(str(path_hint)).name
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "_", stem).strip("._") or "compile_unit"
    return f"{digest}_{stem}"


def parse_macro_definition(text: str) -> dict[str, Any] | None:
    text = text.strip()
    if not text:
        return None
    match = _MACRO_NAME_RE.match(text)
    if not match:
        return None
    name = match.group(1)
    value = match.group(2) or ""
    base_name = name.split("(", 1)[0]
    return {
        "name": base_name,
        "signature": name,
        "value": value,
        "raw": text,
    }


def parse_preprocessor_defines(output: str) -> list[dict[str, Any]]:
    definitions: list[dict[str, Any]] = []
    for line in output.splitlines():
        match = _DEFINE_RE.match(line)
        if not match:
            continue
        parsed = parse_macro_definition(match.group(1))
        if parsed:
            parsed["kind"] = "define"
            definitions.append(parsed)
    return definitions


def final_definition_list(definitions: list[dict[str, Any]]) -> list[dict[str, Any]]:
    final: dict[str, dict[str, Any]] = {}
    for definition in definitions:
        name = definition.get("name")
        if not name:
            continue
        if definition.get("kind") == "undef":
            final.pop(name, None)
            continue
        final[name] = definition
    return [final[name] for name in sorted(final)]


def definition_map(definitions: list[dict[str, Any]]) -> dict[str, str]:
    return {
        item["name"]: item.get("value", "")
        for item in final_definition_list(definitions)
        if item.get("name")
    }


def parse_readelf_macro_dump(output: str) -> dict[int, list[dict[str, Any]]]:
    blocks: dict[int, list[dict[str, Any]]] = {}
    current_offset: int | None = None
    file_stack: list[str] = []

    for line in output.splitlines():
        offset_match = _READ_ELF_OFFSET_RE.match(line)
        if offset_match:
            current_offset = int(offset_match.group(1), 16)
            blocks.setdefault(current_offset, [])
            file_stack = []
            continue

        if current_offset is None:
            continue

        start_match = _READ_ELF_START_FILE_RE.match(line)
        if start_match:
            filename = start_match.group(3).strip()
            file_stack.append(filename)
            blocks[current_offset].append({
                "kind": "start_file",
                "line": int(start_match.group(1)),
                "file": filename,
            })
            continue

        if "DW_MACRO_end_file" in line:
            ended = file_stack.pop() if file_stack else None
            blocks[current_offset].append({"kind": "end_file", "file": ended})
            continue

        import_match = _READ_ELF_IMPORT_RE.match(line)
        if import_match:
            blocks[current_offset].append({
                "kind": "import",
                "offset": int(import_match.group(1), 16),
                "file": file_stack[-1] if file_stack else None,
            })
            continue

        define_match = _READ_ELF_DEFINE_RE.match(line)
        if define_match:
            parsed = parse_macro_definition(define_match.group(2))
            if parsed:
                parsed.update({
                    "kind": "define",
                    "line": int(define_match.group(1)),
                    "file": file_stack[-1] if file_stack else None,
                })
                blocks[current_offset].append(parsed)
            continue

        undef_match = _READ_ELF_UNDEF_RE.match(line)
        if undef_match:
            parsed = parse_macro_definition(undef_match.group(2))
            if parsed:
                parsed.update({
                    "kind": "undef",
                    "line": int(undef_match.group(1)),
                    "file": file_stack[-1] if file_stack else None,
                })
                blocks[current_offset].append(parsed)

    return blocks


def collect_readelf_definitions(
    blocks: dict[int, list[dict[str, Any]]],
    offset: int,
    visited: set[int] | None = None,
) -> list[dict[str, Any]]:
    visited = visited or set()
    if offset in visited:
        return []
    visited.add(offset)

    definitions: list[dict[str, Any]] = []
    for entry in blocks.get(offset, []):
        kind = entry.get("kind")
        if kind == "import":
            definitions.extend(
                collect_readelf_definitions(blocks, int(entry["offset"]), visited)
            )
        elif kind in {"define", "undef"}:
            definitions.append(entry)
    return definitions


def load_compile_database(paths: list[Path]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    seen: set[Path] = set()
    for path in paths:
        path = path.expanduser().resolve()
        if path in seen or not path.is_file():
            continue
        seen.add(path)
        try:
            with path.open("r", encoding="utf-8") as f:
                payload = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        if isinstance(payload, list):
            for entry in payload:
                if isinstance(entry, dict):
                    entry["_compile_commands_path"] = str(path)
                    entries.append(entry)
    return entries


def compile_command_args(entry: dict[str, Any]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list):
        return [str(arg) for arg in arguments]
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    return []


def _entry_source_abs(entry: dict[str, Any]) -> str | None:
    file_name = entry.get("file")
    if not file_name:
        return None
    file_path = Path(str(file_name))
    if file_path.is_absolute():
        return str(file_path.resolve())
    directory = Path(str(entry.get("directory") or ".")).expanduser()
    return str((directory / file_path).resolve())


def _norm_key(path: str | None) -> str | None:
    if not path:
        return None
    return os.path.normcase(os.path.normpath(str(Path(path).expanduser().resolve())))


def match_compile_command(unit: dict[str, Any], entries: list[dict[str, Any]]) -> dict[str, Any] | None:
    candidates = {
        _norm_key(unit.get("resolved_path")),
        _norm_key(unit.get("original_file")),
        _norm_key(unit.get("name")),
    }
    candidates.discard(None)

    relative = unit.get("source_root_relative")
    if relative:
        relative_norm = os.path.normcase(os.path.normpath(str(relative)))
    else:
        relative_norm = None

    for entry in entries:
        entry_abs = _norm_key(_entry_source_abs(entry))
        if entry_abs in candidates:
            return entry
        if relative_norm:
            entry_file = os.path.normcase(os.path.normpath(str(entry.get("file") or "")))
            if entry_file.endswith(relative_norm):
                return entry
    return None


def prepare_preprocess_command(args: list[str], source_path: str) -> list[str]:
    if not args:
        return []

    compiler = args[0]
    filtered: list[str] = []
    skip_next = False
    source_abs = _norm_key(source_path)
    source_name = Path(source_path).name

    options_with_value = {"-o", "-MF", "-MT", "-MQ"}
    remove_exact = {"-c", "-MMD", "-MD", "-MP"}

    for arg in args[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in options_with_value:
            skip_next = True
            continue
        if arg in remove_exact:
            continue
        if arg.startswith("-o") and len(arg) > 2:
            continue
        if source_abs and _norm_key(arg) == source_abs:
            continue
        if not arg.startswith("-") and Path(arg).name == source_name:
            continue
        filtered.append(arg)

    return [compiler, "-dM", "-E", *filtered, source_path]
