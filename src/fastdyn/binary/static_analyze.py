from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional

from .binary_utils.artifact_io import write_json_artifact
from .passes import (
    binary_metadata,
    callgraph,
    compile_units,
    constants,
    firmware_identity,
    functions,
    memory_map,
    source_map,
    strings,
    svd_summary,
    symbols,
    unresolved_source_summary,
    vector_table,
)
from .. import fastdyn_log as fastdyn_log_conf


fastdyn_log = fastdyn_log_conf.getFastdynLogger()


@dataclass
class StaticAnalyzeConfig:
    binary_path: str
    svd_path: Optional[str]
    platform: Optional[str]
    config_path: str
    cache_dir: str = "fastdyn_static"
    source_roots: list[str] = field(default_factory=list)
    init_nsvtor: Optional[int] = None
    flash_base: Optional[int] = None
    flash_size: Optional[int] = None
    sram_base: int = 0x20000000
    force: bool = False


@dataclass
class AnalysisContext:
    config: StaticAnalyzeConfig
    cache_dir: Path
    pass_results: dict[str, dict[str, Any]] = field(default_factory=dict)
    shared: dict[str, Any] = field(default_factory=dict)


def _coerce_int(value, default=None):
    if value in (None, ""):
        return default
    if isinstance(value, int):
        return value
    return int(str(value).strip(), 0)


def _coerce_size(value, default=None):
    if value in (None, ""):
        return default
    if isinstance(value, int):
        return value

    text = str(value).strip().upper()
    multipliers = {
        "B": 1,
        "K": 1024,
        "KB": 1024,
        "M": 1024 * 1024,
        "MB": 1024 * 1024,
        "G": 1024 * 1024 * 1024,
        "GB": 1024 * 1024 * 1024,
    }

    for suffix in sorted(multipliers, key=len, reverse=True):
        if text.endswith(suffix):
            return int(text[:-len(suffix)].strip(), 0) * multipliers[suffix]
    return int(text, 0)


def _find_memory(machine, memory_name):
    memory = machine.memories.get(memory_name)
    if memory is not None:
        return memory

    for candidate in machine.memories.values():
        memory_type = getattr(candidate, "memory_type", None)
        if getattr(memory_type, "name", "").lower() == memory_name.lower():
            return candidate
    return None


def build_static_analyze_config(machine, cpu, config_path, force=False) -> StaticAnalyzeConfig:
    flash = _find_memory(machine, "flash")
    main = _find_memory(machine, "main")

    return StaticAnalyzeConfig(
        binary_path=str(Path(cpu.binary).expanduser().resolve()),
        svd_path=machine.svd_file,
        platform=machine.platform,
        config_path=str(Path(config_path).expanduser().resolve()),
        cache_dir=str(Path(machine.static_analysis_cache_dir or "fastdyn_static").expanduser()),
        source_roots=[str(Path(path).expanduser()) for path in machine.firmware_source_roots],
        init_nsvtor=_coerce_int(cpu.init_nsvtor, default=None),
        flash_base=_coerce_int(getattr(flash, "memory_start", None), default=None),
        flash_size=_coerce_size(getattr(flash, "memory_size", None), default=None),
        sram_base=_coerce_int(getattr(main, "memory_start", None), default=0x20000000),
        force=force,
    )


def run_static_analyze(config: StaticAnalyzeConfig) -> str:
    cache_dir = Path(config.cache_dir).expanduser().resolve()
    cache_dir.mkdir(parents=True, exist_ok=True)

    if config.force:
        for artifact in cache_dir.glob("*.json"):
            artifact.unlink()

    context = AnalysisContext(
        config=config,
        cache_dir=cache_dir,
    )

    fastdyn_log.info("Static analyze")
    fastdyn_log.info("  Binary: %s", config.binary_path)
    fastdyn_log.info("  SVD: %s", config.svd_path)
    fastdyn_log.info("  Cache: %s", cache_dir)
    if config.source_roots:
        fastdyn_log.info("  Source roots: %s", config.source_roots)

    passes: list[tuple[str, str, Callable[[AnalysisContext], None]]] = [
        ("binary_metadata", "Binary metadata", binary_metadata.run),
        ("svd_summary", "SVD summary", svd_summary.run),
        ("symbols", "Symbols", symbols.run),
        ("functions", "Functions", functions.run),
        ("compile_units", "Compile units", compile_units.run),
        ("source_map", "Source map", source_map.run),
        ("vector_table", "Vector table", vector_table.run),
        ("memory_map", "Memory map", memory_map.run),
        ("firmware_identity", "Firmware identity", firmware_identity.run),
        ("strings", "Strings", strings.run),
        ("callgraph", "Callgraph", callgraph.run),
        ("constants", "Constants", constants.run),
        ("unresolved_source_summary", "Unresolved source summary", unresolved_source_summary.run),
    ]

    for pass_name, pass_label, pass_fn in passes:
        _run_pass(context, pass_name, pass_label, pass_fn)

    _write_manifest(context)

    fastdyn_log.info("Static analyze complete: %s", cache_dir)
    return str(cache_dir)


def _run_pass(
    context: AnalysisContext,
    pass_name: str,
    pass_label: str,
    pass_fn: Callable[[AnalysisContext], None],
) -> None:
    fastdyn_log.info("Pass: %s", pass_label)
    try:
        pass_fn(context)
        context.pass_results[pass_name] = {"status": "ok"}
    except Exception as exc:
        fastdyn_log.exception("Pass failed: %s", pass_label)
        context.pass_results[pass_name] = {
            "status": "error",
            "error": str(exc),
        }


def _write_manifest(context: AnalysisContext) -> None:
    manifest = {
        "schema_version": 1,
        "command": "fastdyn static-analyze",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "config": asdict(context.config),
        "passes": context.pass_results,
        "artifacts": sorted(
            artifact.name
            for artifact in context.cache_dir.iterdir()
            if artifact.is_file() and artifact.suffix == ".json" and artifact.name != "manifest.json"
        ),
    }
    write_json_artifact(context.cache_dir, "manifest.json", manifest)
