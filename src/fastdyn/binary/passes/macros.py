from __future__ import annotations

import gzip
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

from elftools.elf.elffile import ELFFile

from fastdyn import fastdyn_log as fastdyn_log_conf
from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.macros import (
    collect_readelf_definitions,
    compile_command_args,
    definition_map,
    final_definition_list,
    load_compile_database,
    match_compile_command,
    parse_preprocessor_defines,
    parse_readelf_macro_dump,
    prepare_preprocess_command,
    stable_unit_id,
)


fastdyn_log = fastdyn_log_conf.getFastdynLogger()

_BUILD_BOARD_RE = re.compile(r"(?:^|[/\\])build[/\\]([^/\\]+)")
_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".S", ".s"}


def run(context) -> None:
    options = context.config.macro_options
    macros_dir = context.cache_dir / "macros"
    units_dir = macros_dir / "units"
    contexts_dir = macros_dir / "contexts"
    macros_dir.mkdir(parents=True, exist_ok=True)
    units_dir.mkdir(parents=True, exist_ok=True)
    contexts_dir.mkdir(parents=True, exist_ok=True)

    compile_units = context.shared.get("compile_units", [])
    firmware_identity = context.shared.get("firmware_identity", {})

    if not options.enabled or options.strategy == "none":
        _write_disabled_context(context, options, len(compile_units))
        return

    strategy = options.strategy
    errors: list[str] = []
    if strategy in {"auto", "dwarf"}:
        result = _extract_from_dwarf(context, compile_units, units_dir, contexts_dir)
        if result["status"] == "ok":
            _write_index_and_context(
                context=context,
                options=options,
                provider="dwarf_debug_macro",
                confidence="exact",
                index_units=result["index_units"],
                identity=_infer_identity(options, compile_units, firmware_identity),
                limitations=result.get("limitations", []),
                errors=[],
            )
            return
        errors.extend(result.get("errors", []))
        if strategy == "dwarf":
            _write_index_and_context(
                context=context,
                options=options,
                provider="dwarf_debug_macro",
                confidence="unavailable",
                index_units=[],
                identity=_infer_identity(options, compile_units, firmware_identity),
                limitations=result.get("limitations", []),
                errors=errors,
                status="unavailable",
            )
            return

    if strategy in {"auto", "rebuild"}:
        result = _extract_from_rebuild(
            context=context,
            compile_units=compile_units,
            firmware_identity=firmware_identity,
            units_dir=units_dir,
            contexts_dir=contexts_dir,
        )
        _write_index_and_context(
            context=context,
            options=options,
            provider=result.get("provider", "rebuild_approx"),
            confidence=result.get("confidence", "approximate"),
            index_units=result.get("index_units", []),
            identity=result.get("identity")
            or _infer_identity(options, compile_units, firmware_identity),
            limitations=result.get("limitations", []),
            errors=errors + result.get("errors", []),
            status=result.get("status", "ok"),
            build=result.get("build"),
        )
        return

    _write_index_and_context(
        context=context,
        options=options,
        provider="none",
        confidence="unavailable",
        index_units=[],
        identity=_infer_identity(options, compile_units, firmware_identity),
        limitations=[f"unsupported macro strategy: {strategy}"],
        errors=errors,
        status="unavailable",
    )


def _write_disabled_context(context, options, units_total: int) -> None:
    write_json_artifact(context.cache_dir, "macro_context.json", {
        "schema_version": 1,
        "enabled": False,
        "strategy": options.strategy,
        "status": "disabled",
        "provider": "none",
        "confidence": "unavailable",
        "storage_layout": "unit_context_dedup_gzip_v1",
        "units_total": units_total,
        "units_with_macros": 0,
        "context_count": 0,
        "macros_dir": "macros",
        "limitations": ["macro extraction disabled by configuration"],
        "errors": [],
    })
    write_json_artifact(context.cache_dir / "macros", "index.json", {
        "schema_version": 1,
        "storage_layout": "unit_context_dedup_gzip_v1",
        "units": [],
        "contexts": [],
        "context_count": 0,
    })


def _write_index_and_context(
    *,
    context,
    options,
    provider: str,
    confidence: str,
    index_units: list[dict[str, Any]],
    identity: dict[str, Any],
    limitations: list[str],
    errors: list[str],
    status: str = "ok",
    build: dict[str, Any] | None = None,
) -> None:
    contexts = _context_index(index_units)
    write_json_artifact(context.cache_dir / "macros", "index.json", {
        "schema_version": 1,
        "storage_layout": "unit_context_dedup_gzip_v1",
        "provider": provider,
        "confidence": confidence,
        "context_encoding": "json.gz",
        "units": index_units,
        "contexts": contexts,
        "context_count": len(contexts),
    })
    payload = {
        "schema_version": 1,
        "enabled": True,
        "strategy": options.strategy,
        "status": status,
        "provider": provider,
        "confidence": confidence,
        "storage_layout": "unit_context_dedup_gzip_v1",
        "context_encoding": "json.gz",
        "identity": identity,
        "units_total": len(context.shared.get("compile_units", [])),
        "units_with_macros": sum(1 for unit in index_units if unit.get("macro_count", 0) > 0),
        "context_count": len(contexts),
        "macros_dir": "macros",
        "index": "macros/index.json",
        "limitations": limitations,
        "errors": errors,
    }
    if build:
        payload["build"] = build
    write_json_artifact(context.cache_dir, "macro_context.json", payload)


def _extract_from_dwarf(context, compile_units, units_dir: Path, contexts_dir: Path) -> dict[str, Any]:
    binary_path = Path(context.config.binary_path)
    readelf = shutil.which("readelf")
    if readelf is None:
        return {
            "status": "unavailable",
            "index_units": [],
            "limitations": ["readelf not found"],
            "errors": [],
        }

    try:
        with binary_path.open("rb") as f:
            elf_file = ELFFile(f)
            if elf_file.get_section_by_name(".debug_macro") is None:
                return {
                    "status": "unavailable",
                    "index_units": [],
                    "limitations": ["binary has no .debug_macro section"],
                    "errors": [],
                }
            offsets = _dwarf_macro_offsets(elf_file)
    except Exception as exc:
        return {
            "status": "unavailable",
            "index_units": [],
            "limitations": ["failed to inspect DWARF macro metadata"],
            "errors": [str(exc)],
        }

    if not offsets:
        return {
            "status": "unavailable",
            "index_units": [],
            "limitations": ["no DW_AT_macros attributes found in compile units"],
            "errors": [],
        }

    try:
        proc = subprocess.run(
            [readelf, "--debug-dump=macro", str(binary_path)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        return {
            "status": "unavailable",
            "index_units": [],
            "limitations": ["readelf failed to dump .debug_macro"],
            "errors": [str(exc)],
        }

    blocks = parse_readelf_macro_dump(proc.stdout)
    index_units: list[dict[str, Any]] = []
    unit_count = min(context.config.macro_options.max_units or len(compile_units), len(compile_units))
    limitations: list[str] = []
    if context.config.macro_options.max_units is not None:
        limitations.append(
            f"macro extraction limited to first {context.config.macro_options.max_units} compile units by configuration"
        )

    for idx in range(unit_count):
        unit = compile_units[idx]
        unit_id = stable_unit_id(unit, idx)
        if idx >= len(offsets):
            index_units.append(_unavailable_index_entry(unit, unit_id, "compile unit has no DWARF macro offset metadata"))
            continue
        macro_offset = offsets[idx]
        if macro_offset is None:
            index_units.append(_unavailable_index_entry(unit, unit_id, "compile unit has no DW_AT_macros attribute"))
            continue
        raw_definitions = collect_readelf_definitions(blocks, macro_offset)
        definitions = final_definition_list(raw_definitions)
        artifact = _write_unit_artifact(
            units_dir=units_dir,
            contexts_dir=contexts_dir,
            unit=unit,
            unit_id=unit_id,
            provider="dwarf_debug_macro",
            confidence="exact",
            strategy="dwarf",
            definitions=definitions,
            provenance={
                "macro_offset": hex(macro_offset),
                "source": "binary .debug_macro",
            },
            limitations=[],
        )
        index_units.append(_index_entry(unit, unit_id, artifact, "dwarf_debug_macro", "exact"))

    return {
        "status": "ok",
        "index_units": index_units,
        "limitations": limitations,
        "errors": [],
    }


def _dwarf_macro_offsets(elf_file: ELFFile) -> list[int | None]:
    if not elf_file.has_dwarf_info():
        return []
    offsets: list[int | None] = []
    dwarf_info = elf_file.get_dwarf_info()
    for cu in dwarf_info.iter_CUs():
        top_die = cu.get_top_DIE()
        if top_die is None:
            continue
        attr = top_die.attributes.get("DW_AT_macros")
        if attr is None:
            attr = top_die.attributes.get("DW_AT_macro_info")
        if attr is None:
            offsets.append(None)
            continue
        offsets.append(int(attr.value))
    return offsets


def _extract_from_rebuild(context, compile_units, firmware_identity, units_dir: Path, contexts_dir: Path) -> dict[str, Any]:
    options = context.config.macro_options
    identity = _infer_identity(options, compile_units, firmware_identity)
    project_root = _find_project_root(options, context.config.source_roots)
    board = identity.get("board")
    build_dir = _find_build_dir(options, project_root, board)

    limitations: list[str] = [
        "macros came from a local build or reconstructed local build context; they may differ from the original firmware build"
    ]
    errors: list[str] = []
    build_info = {
        "project_root": str(project_root) if project_root else None,
        "build_dir": str(build_dir) if build_dir else None,
        "reused_existing_build": False,
        "ran_build": False,
    }

    if project_root is None:
        return {
            "status": "unavailable",
            "provider": "rebuild_approx",
            "confidence": "unavailable",
            "identity": identity,
            "index_units": [],
            "limitations": limitations + ["source_project_root could not be inferred and was not configured"],
            "errors": errors,
            "build": build_info,
        }

    if build_dir is not None and build_dir.exists() and options.reuse_existing_build:
        build_info["reused_existing_build"] = True
    elif options.allow_build:
        build_result = _run_ardupilot_build(options, identity, project_root)
        build_info.update(build_result["build"])
        errors.extend(build_result["errors"])
        if build_result["status"] != "ok":
            return {
                "status": "unavailable",
                "provider": "rebuild_approx",
                "confidence": "unavailable",
                "identity": identity,
                "index_units": [],
                "limitations": limitations + build_result["limitations"],
                "errors": errors,
                "build": build_info,
            }
        build_dir = _find_build_dir(options, project_root, board)
        build_info["build_dir"] = str(build_dir) if build_dir else None
    else:
        return {
            "status": "unavailable",
            "provider": "rebuild_approx",
            "confidence": "unavailable",
            "identity": identity,
            "index_units": [],
            "limitations": limitations + [
                "matching build directory was not found or reuse was disabled, and allow_build is false"
            ],
            "errors": errors,
            "build": build_info,
        }

    if build_dir is None or not build_dir.exists():
        return {
            "status": "unavailable",
            "provider": "rebuild_approx",
            "confidence": "unavailable",
            "identity": identity,
            "index_units": [],
            "limitations": limitations + ["build directory does not exist after build-context discovery"],
            "errors": errors,
            "build": build_info,
        }

    compile_db = load_compile_database([
        build_dir / "compile_commands.json",
        project_root / "compile_commands.json",
    ])
    line_contexts = _dwarf_line_contexts(context.config.binary_path)

    index_units: list[dict[str, Any]] = []
    max_units = options.max_units or len(compile_units)
    for idx, unit in enumerate(compile_units[:max_units]):
        if not _is_source_unit(unit):
            continue
        unit_id = stable_unit_id(unit, idx)
        source_path = _local_rebuild_source_path(unit, project_root)
        if source_path is None:
            index_units.append(_unavailable_index_entry(unit, unit_id, "source file not found in local project root"))
            continue

        definitions, provenance, unit_errors = _preprocess_rebuild_unit(
            options=options,
            unit=unit,
            idx=idx,
            source_path=source_path,
            build_dir=build_dir,
            compile_db=compile_db,
            line_context=line_contexts.get(idx, {}),
        )
        errors.extend(unit_errors)
        if definitions:
            artifact = _write_unit_artifact(
                units_dir=units_dir,
                contexts_dir=contexts_dir,
                unit=unit,
                unit_id=unit_id,
                provider=provenance.get("provider", "rebuild_approx"),
                confidence="approximate",
                strategy="rebuild",
                definitions=definitions,
                provenance={
                    **provenance,
                    "project_root": str(project_root),
                    "build_dir": str(build_dir),
                    "identity": identity,
                },
                limitations=limitations,
            )
            index_units.append(_index_entry(unit, unit_id, artifact, provenance.get("provider", "rebuild_approx"), "approximate"))
        else:
            index_units.append(_unavailable_index_entry(unit, unit_id, "preprocessor macro extraction failed"))

    return {
        "status": "ok" if index_units else "unavailable",
        "provider": "rebuild_approx",
        "confidence": "approximate" if index_units else "unavailable",
        "identity": identity,
        "index_units": index_units,
        "limitations": limitations,
        "errors": errors[:50],
        "build": build_info,
    }


def _infer_identity(options, compile_units, firmware_identity) -> dict[str, Any]:
    summary = firmware_identity.get("summary", {}) if isinstance(firmware_identity, dict) else {}

    def summary_value(name: str) -> str | None:
        value = summary.get(name, {})
        return value.get("value") if isinstance(value, dict) else None

    board = options.board or _infer_board_from_compile_units(compile_units) or summary_value("board_or_mcu")
    return {
        "project": options.project or summary_value("project"),
        "vehicle": options.vehicle or summary_value("vehicle"),
        "version": options.version or summary_value("version"),
        "board": board,
        "board_inference": "config" if options.board else "compile_units_or_identity",
    }


def _infer_board_from_compile_units(compile_units) -> str | None:
    counts: dict[str, int] = {}
    for unit in compile_units:
        for key in ("comp_dir", "original_comp_dir"):
            comp_dir = unit.get(key)
            if not comp_dir:
                continue
            match = _BUILD_BOARD_RE.search(str(comp_dir))
            if match:
                board = match.group(1)
                counts[board] = counts.get(board, 0) + 1
    if not counts:
        return None
    return sorted(counts.items(), key=lambda item: (-item[1], item[0]))[0][0]


def _find_project_root(options, source_roots) -> Path | None:
    candidates: list[Path] = []
    if options.source_project_root:
        candidate = Path(options.source_project_root).expanduser().resolve()
        if candidate.exists():
            return candidate
        candidates.append(candidate)
    for source_root in source_roots:
        root = Path(source_root).expanduser()
        candidates.extend([
            root,
            root.parent / "ardupilot",
        ])
        if len(root.parents) >= 2:
            candidates.append(root.parents[1] / "ardupilot")
    for candidate in candidates:
        if (
            (candidate / "waf").is_file()
            or (candidate / "wscript").is_file()
            or (candidate / "west.yml").is_file()
            or (candidate / "zephyr" / "CMakeLists.txt").is_file()
        ):
            return candidate.resolve()
    return None


def _find_build_dir(options, project_root: Path | None, board: str | None) -> Path | None:
    if options.build_dir:
        return Path(options.build_dir).expanduser().resolve()
    if project_root is None or not board:
        return None
    return (project_root / "build" / board).resolve()


def _run_ardupilot_build(options, identity, project_root: Path) -> dict[str, Any]:
    board = identity.get("board")
    vehicle = identity.get("vehicle")
    vehicle_cmd = {
        "Rover": "rover",
        "Copter": "copter",
        "Plane": "plane",
        "Sub": "sub",
        "AntennaTracker": "antennatracker",
        "Blimp": "blimp",
    }.get(vehicle)

    build_info = {"ran_build": False, "commands": []}
    limitations: list[str] = []
    errors: list[str] = []

    if not board or not vehicle_cmd:
        return {
            "status": "error",
            "build": build_info,
            "limitations": ["could not infer board and vehicle command needed for ArduPilot rebuild"],
            "errors": [],
        }

    commands = [
        ["./waf", "configure", f"--board={board}"],
        ["./waf", vehicle_cmd],
    ]
    for command in commands:
        build_info["commands"].append(command)
        try:
            proc = subprocess.run(
                command,
                cwd=str(project_root),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=options.build_timeout_sec,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.append(str(exc))
            return {
                "status": "error",
                "build": build_info,
                "limitations": limitations,
                "errors": errors,
            }
        build_info["ran_build"] = True
        if proc.returncode != 0:
            errors.append((proc.stderr or proc.stdout or "").strip()[:4000])
            return {
                "status": "error",
                "build": build_info,
                "limitations": limitations,
                "errors": errors,
            }

    return {"status": "ok", "build": build_info, "limitations": limitations, "errors": errors}


def _dwarf_line_contexts(binary_path: str) -> dict[int, dict[str, Any]]:
    contexts: dict[int, dict[str, Any]] = {}
    try:
        with open(binary_path, "rb") as f:
            elf_file = ELFFile(f)
            if not elf_file.has_dwarf_info():
                return contexts
            dwarf_info = elf_file.get_dwarf_info()
            for idx, cu in enumerate(dwarf_info.iter_CUs()):
                top_die = cu.get_top_DIE()
                comp_dir = _die_attr_str(top_die, "DW_AT_comp_dir") if top_die is not None else None
                include_dirs: list[str] = []
                try:
                    line_program = dwarf_info.line_program_for_CU(cu)
                    if line_program and hasattr(line_program, "header"):
                        include_dirs = [
                            value.decode("utf-8", errors="replace")
                            if isinstance(value, bytes) else str(value)
                            for value in line_program.header.get("include_directory", [])
                        ]
                except Exception:
                    include_dirs = []
                contexts[idx] = {"comp_dir": comp_dir, "include_dirs": include_dirs}
    except Exception:
        return contexts
    return contexts


def _die_attr_str(die, attr_name: str) -> str | None:
    attr = die.attributes.get(attr_name)
    if attr is None:
        return None
    value = attr.value
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _is_source_unit(unit: dict[str, Any]) -> bool:
    path = unit.get("source_root_relative") or unit.get("original_file") or unit.get("name")
    if not path:
        return False
    return Path(str(path)).suffix in _SOURCE_SUFFIXES


def _local_rebuild_source_path(unit: dict[str, Any], project_root: Path) -> str | None:
    relative = unit.get("source_root_relative")
    if relative:
        candidate = project_root / relative
        if candidate.exists():
            return str(candidate.resolve())
    resolved = unit.get("resolved_path")
    if resolved and Path(resolved).exists():
        return str(Path(resolved).resolve())
    return None


def _preprocess_rebuild_unit(
    *,
    options,
    unit: dict[str, Any],
    idx: int,
    source_path: str,
    build_dir: Path,
    compile_db: list[dict[str, Any]],
    line_context: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, Any], list[str]]:
    entry = match_compile_command(unit, compile_db)
    errors: list[str] = []
    if entry is not None:
        args = compile_command_args(entry)
        command = prepare_preprocess_command(args, source_path)
        cwd = Path(str(entry.get("directory") or build_dir)).expanduser()
        provider = "compile_commands_approx"
    else:
        command = _reconstructed_preprocess_command(
            options=options,
            unit=unit,
            source_path=source_path,
            build_dir=build_dir,
            include_dirs=line_context.get("include_dirs", []),
        )
        cwd = build_dir
        provider = "reconstructed_build_approx"

    if not command:
        return [], {"provider": provider, "command": []}, ["preprocess command could not be built"]

    try:
        proc = subprocess.run(
            command,
            cwd=str(cwd),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=options.build_timeout_sec,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return [], {"provider": provider, "command": command, "cwd": str(cwd)}, [str(exc)]

    if proc.returncode != 0:
        errors.append((proc.stderr or proc.stdout or "").strip()[:1000])
        return [], {"provider": provider, "command": command, "cwd": str(cwd)}, errors

    definitions = final_definition_list(parse_preprocessor_defines(proc.stdout))
    return definitions, {"provider": provider, "command": command, "cwd": str(cwd)}, errors


def _reconstructed_preprocess_command(
    *,
    options,
    unit: dict[str, Any],
    source_path: str,
    build_dir: Path,
    include_dirs: list[str],
) -> list[str]:
    suffix = Path(source_path).suffix
    is_cxx = suffix in {".cc", ".cpp", ".cxx"}
    compiler = (
        options.compiler_cxx if is_cxx else options.compiler_c
    ) or ("arm-none-eabi-g++" if is_cxx else "arm-none-eabi-gcc")

    language = "c++" if is_cxx else ("assembler-with-cpp" if suffix in {".S", ".s"} else "c")
    command = [
        compiler,
        "-dM",
        "-E",
        "-x",
        language,
        "-DARDUPILOT_BUILD",
        "-D__USE_CMSIS",
        "-DCONFIG_HAL_BOARD=HAL_BOARD_CHIBIOS",
    ]

    if (build_dir / "ap_config.h").exists():
        command.extend(["-include", "ap_config.h"])

    command.extend(["-I", str(build_dir)])
    for include_dir in include_dirs:
        command.extend(["-I", include_dir])

    command.append(source_path)
    return command


def _write_unit_artifact(
    *,
    units_dir: Path,
    contexts_dir: Path,
    unit: dict[str, Any],
    unit_id: str,
    provider: str,
    confidence: str,
    strategy: str,
    definitions: list[dict[str, Any]],
    provenance: dict[str, Any],
    limitations: list[str],
) -> dict[str, Any]:
    context_metadata = _write_context_artifact(
        contexts_dir=contexts_dir,
        definitions=definitions,
    )
    payload = {
        "schema_version": 1,
        "unit_id": unit_id,
        "strategy": strategy,
        "provider": provider,
        "confidence": confidence,
        "compile_unit": unit,
        "macro_count": context_metadata["macro_count"],
        "context_hash": context_metadata["context_hash"],
        "context_artifact": context_metadata["context_artifact"],
        "context_encoding": context_metadata["context_encoding"],
        "context_uncompressed_size": context_metadata["context_uncompressed_size"],
        "context_compressed_size": context_metadata["context_compressed_size"],
        "provenance": provenance,
        "limitations": limitations,
    }
    path = units_dir / f"{unit_id}.json"
    with path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")
    return {
        **context_metadata,
        "unit_artifact": str(path.relative_to(units_dir.parent.parent)),
    }


def _write_context_artifact(*, contexts_dir: Path, definitions: list[dict[str, Any]]) -> dict[str, Any]:
    final_definitions = final_definition_list(definitions)
    payload = {
        "schema_version": 1,
        "macro_count": len(final_definitions),
        "definitions": final_definitions,
        "definition_map": definition_map(final_definitions),
    }
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    context_hash = hashlib.sha256(canonical).hexdigest()
    payload["context_hash"] = context_hash

    path = contexts_dir / f"{context_hash}.json.gz"
    if not path.exists():
        with gzip.open(path, "wt", encoding="utf-8") as f:
            json.dump(payload, f, sort_keys=True, separators=(",", ":"))
            f.write("\n")

    return {
        "context_hash": context_hash,
        "context_artifact": str(path.relative_to(contexts_dir.parent.parent)),
        "context_encoding": "json.gz",
        "macro_count": len(final_definitions),
        "context_uncompressed_size": len(canonical),
        "context_compressed_size": path.stat().st_size,
    }


def _index_entry(unit, unit_id, artifact, provider, confidence):
    return {
        "unit_id": unit_id,
        "artifact": artifact["unit_artifact"],
        "unit_artifact": artifact["unit_artifact"],
        "context_hash": artifact["context_hash"],
        "context_artifact": artifact["context_artifact"],
        "context_encoding": artifact["context_encoding"],
        "provider": provider,
        "confidence": confidence,
        "macro_count": artifact["macro_count"],
        "context_uncompressed_size": artifact["context_uncompressed_size"],
        "context_compressed_size": artifact["context_compressed_size"],
        "name": unit.get("name"),
        "source_root_relative": unit.get("source_root_relative"),
        "resolved_path": unit.get("resolved_path"),
        "exists_locally": unit.get("exists_locally"),
    }


def _unavailable_index_entry(unit, unit_id, reason):
    return {
        "unit_id": unit_id,
        "artifact": None,
        "provider": "unavailable",
        "confidence": "unavailable",
        "macro_count": 0,
        "name": unit.get("name"),
        "source_root_relative": unit.get("source_root_relative"),
        "resolved_path": unit.get("resolved_path"),
        "exists_locally": unit.get("exists_locally"),
        "reason": reason,
    }


def _context_index(index_units: list[dict[str, Any]]) -> list[dict[str, Any]]:
    contexts: dict[str, dict[str, Any]] = {}
    for unit in index_units:
        context_hash = unit.get("context_hash")
        if not context_hash:
            continue
        contexts.setdefault(context_hash, {
            "context_hash": context_hash,
            "context_artifact": unit.get("context_artifact"),
            "context_encoding": unit.get("context_encoding"),
            "macro_count": unit.get("macro_count", 0),
            "context_uncompressed_size": unit.get("context_uncompressed_size"),
            "context_compressed_size": unit.get("context_compressed_size"),
            "unit_count": 0,
        })
        contexts[context_hash]["unit_count"] += 1
    return sorted(contexts.values(), key=lambda item: item["context_hash"])
