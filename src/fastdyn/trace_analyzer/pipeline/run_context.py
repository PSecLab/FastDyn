from __future__ import annotations

import shutil
from pathlib import Path
from typing import Any

from fastdyn import toml_parser
from fastdyn.binary.static_analyze import (
    build_static_analyze_config,
    validate_static_analyze_cache,
)
from fastdyn.utils import parse_config

from ..models import (
    RunArtifacts,
    StaticArtifacts,
    TraceAnalysisContext,
    TraceAnalyzeRequest,
)
from ..utils.artifacts import (
    ensure_dir,
    file_sha256,
    first_existing_path,
    load_json,
    load_optional_json,
    require_file,
    stable_hash,
)


DEFAULT_SVD_PATH = "third_party/common/cmsis-svd-data"


def load_trace_analysis_context(
    request: TraceAnalyzeRequest,
    prompt_kind: str,
    *,
    svd_path: str | Path = DEFAULT_SVD_PATH,
) -> TraceAnalysisContext:
    work_dir = request.work_dir.expanduser().resolve()
    latest_run_dir = (
        request.latest_run_dir.expanduser().resolve()
        if request.latest_run_dir is not None
        else None
    )

    _prepare_work_dir(
        work_dir=work_dir,
        latest_run_dir=latest_run_dir,
        reset_work_dir=request.reset_work_dir,
    )

    run_dir = latest_run_dir or work_dir
    run_artifacts = _load_run_artifacts(request=request, run_dir=run_dir, work_dir=work_dir)
    static_artifacts, machine, svd_device = _load_static_artifacts(
        request=request,
        work_dir=work_dir,
        svd_path=svd_path,
    )

    import datetime
    import os
    
    timestamp = None
    if run_artifacts.manifest and "source_run_time" in run_artifacts.manifest:
        # e.g. "2026-06-19T02:10:12Z" -> "20260619-021012"
        # We'll just do simple string extraction if it has the T format, else fallback
        dt_str = run_artifacts.manifest["source_run_time"]
        if isinstance(dt_str, str):
            timestamp = dt_str.replace("-", "").replace(":", "").replace("T", "-")[:15]

    if not timestamp and run_artifacts.probe_result_path.exists():
        mtime = os.path.getmtime(run_artifacts.probe_result_path)
        timestamp = datetime.datetime.fromtimestamp(mtime).strftime("%Y%m%d-%H%M%S")
        
    if not timestamp:
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        
    exit_reason = str(run_artifacts.probe_result.get("exit_reason", "unknown")).replace(" ", "_")
    pc = str(run_artifacts.probe_result.get("pc", "0x0"))
    short_hash = run_artifacts.run_id[-8:] if len(run_artifacts.run_id) >= 8 else run_artifacts.run_id
    
    dir_name = f"{timestamp}_{prompt_kind}_{exit_reason}_{pc}_{short_hash}"
    analysis_dir = work_dir / "trace_analysis" / dir_name

    if request.force and analysis_dir.exists():
        shutil.rmtree(analysis_dir)
    ensure_dir(analysis_dir)

    return TraceAnalysisContext(
        request=TraceAnalyzeRequest(
            config_path=request.config_path.expanduser().resolve(),
            work_dir=work_dir,
            latest_run_dir=latest_run_dir,
            out_prompt=request.out_prompt,
            force=request.force,
            reset_work_dir=request.reset_work_dir,
            routing_json=request.routing_json.expanduser().resolve() if request.routing_json else None,
            force_routing=request.force_routing,
            svd_path=request.svd_path.expanduser().resolve() if request.svd_path else None,
            io_log=request.io_log.expanduser().resolve() if request.io_log else None,
        ),
        run_artifacts=run_artifacts,
        static_artifacts=static_artifacts,
        analysis_dir=analysis_dir,
        prompt_path=work_dir / request.out_prompt,
        machine=machine,
        svd_device=svd_device,
    )


def _prepare_work_dir(
    *,
    work_dir: Path,
    latest_run_dir: Path | None,
    reset_work_dir: bool,
) -> None:
    if reset_work_dir:
        if latest_run_dir is None:
            raise ValueError("--reset-work-dir requires --latest-run-dir")
        if latest_run_dir == work_dir:
            raise ValueError("--reset-work-dir cannot be used when latest-run-dir and work-dir are the same path")
        if work_dir.exists():
            shutil.rmtree(work_dir)
    ensure_dir(work_dir)


def _load_run_artifacts(*, request: TraceAnalyzeRequest, run_dir: Path, work_dir: Path) -> RunArtifacts:
    probe_result_path = require_file(run_dir / "probe_result.json", "probe result")
    probe_result = load_json(probe_result_path)
    if not isinstance(probe_result, dict):
        raise TypeError(f"probe result must be a JSON object: {probe_result_path}")

    manifest_path = run_dir / "run_manifest.json"
    manifest = load_optional_json(manifest_path)
    if manifest is None:
        manifest = {}
        manifest_path_value = None
    elif isinstance(manifest, dict):
        manifest_path_value = manifest_path
    else:
        raise TypeError(f"run manifest must be a JSON object: {manifest_path}")

    run_id = _run_id(
        manifest=manifest,
        probe_result_path=probe_result_path,
        run_dir=run_dir,
    )

    qemu_log_path = first_existing_path([
        run_dir / "qemu.log",
        work_dir / "qemu.log",
    ])
    io_log_path = _discover_io_log(run_dir=run_dir, work_dir=work_dir, qemu_log_path=qemu_log_path, explicit_io_log=request.io_log)
    dev_config_path = first_existing_path([
        run_dir / "dev_config.json",
        work_dir / "dev_config.json",
    ])
    virtuals_dir = first_existing_path([
        run_dir / "virtuals",
        work_dir / "virtuals",
    ])

    return RunArtifacts(
        run_id=run_id,
        run_dir=run_dir,
        probe_result_path=probe_result_path,
        manifest_path=manifest_path_value,
        io_log_path=io_log_path,
        qemu_log_path=qemu_log_path,
        dev_config_path=dev_config_path,
        virtuals_dir=virtuals_dir,
        probe_result=probe_result,
        manifest=manifest,
    )


def _run_id(*, manifest: dict[str, Any], probe_result_path: Path, run_dir: Path) -> str:
    manifest_run_id = manifest.get("run_id")
    if isinstance(manifest_run_id, str) and manifest_run_id.strip():
        return manifest_run_id.strip()
    digest = stable_hash([
        file_sha256(probe_result_path),
        str(run_dir.resolve()),
    ])
    return f"run_{digest[:12]}"


def _discover_io_log(*, run_dir: Path, work_dir: Path, qemu_log_path: Path | None, explicit_io_log: Path | None = None) -> Path | None:
    paths_to_check = []
    if explicit_io_log is not None:
        paths_to_check.append(explicit_io_log)
    paths_to_check.extend([
        run_dir / "io.log",
        work_dir / "io.log",
        work_dir.parent / "io.log",
        Path("io.log").resolve(), # repo root
    ])
    direct = first_existing_path(paths_to_check)
    if direct is not None:
        return direct
    if qemu_log_path is not None and _looks_like_mmio_log(qemu_log_path):
        return qemu_log_path
    return None


def _looks_like_mmio_log(path: Path, *, max_lines: int = 200) -> bool:
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for idx, line in enumerate(f):
                if idx >= max_lines:
                    break
                if "address" in line and ("Read:" in line or "Write:" in line):
                    return True
    except OSError:
        return False
    return False


def _load_static_artifacts(
    *,
    request: TraceAnalyzeRequest,
    work_dir: Path,
    svd_path: str | Path,
) -> tuple[StaticArtifacts, Any, Any]:
    fastdyn_handle = toml_parser.parser(
        str(work_dir),
        machine_name="machine0",
        toml_config=str(request.config_path),
        svd_path=str(svd_path),
        load_fmu=False,
    )
    machine = fastdyn_handle.machines.get("machine0")
    if machine is None:
        raise ValueError("unable to create machine0 from FastDyn config")
    if not machine.cpus:
        raise ValueError("trace analysis requires at least one CPU in the FastDyn config")

    svd_device = getattr(machine, "svd_device", None)
    if svd_device is None and getattr(machine, "svd_file", None):
        svd_device = parse_config.get_svd_device(machine.svd_file)

    analysis_cfg = build_static_analyze_config(
        machine=machine,
        cpu=machine.cpus[0],
        config_path=str(request.config_path),
        force=False,
    )
    valid, reason, cache_dir_str = validate_static_analyze_cache(analysis_cfg)
    if not valid:
        raise RuntimeError(f"trace analysis requires a valid static cache: {reason}")

    cache_dir = Path(cache_dir_str).expanduser().resolve()
    symbols_path = require_file(cache_dir / "symbols.json", "static symbols")
    functions_path = require_file(cache_dir / "functions.json", "static functions")
    source_map_path = require_file(cache_dir / "source_map.json", "static source map")
    compile_units_path = require_file(cache_dir / "compile_units.json", "static compile units")
    memory_map_path = require_file(cache_dir / "memory_map.json", "static memory map")
    svd_map_path = require_file(cache_dir / "svd_map.json", "static SVD map")

    macro_context_path = cache_dir / "macro_context.json"
    macros_index_path = cache_dir / "macros" / "index.json"
    macro_context = load_optional_json(macro_context_path) or {}
    macros_index = load_optional_json(macros_index_path) or {}

    return StaticArtifacts(
        cache_dir=cache_dir,
        symbols_path=symbols_path,
        functions_path=functions_path,
        source_map_path=source_map_path,
        compile_units_path=compile_units_path,
        memory_map_path=memory_map_path,
        svd_map_path=svd_map_path,
        macro_context_path=macro_context_path if macro_context_path.exists() else None,
        macros_index_path=macros_index_path if macros_index_path.exists() else None,
        symbols=_load_list(symbols_path, "symbols"),
        functions=_load_list(functions_path, "functions"),
        source_map=_load_list(source_map_path, "source map"),
        compile_units=_load_list(compile_units_path, "compile units"),
        memory_map=load_json(memory_map_path),
        svd_map=_load_dict(svd_map_path, "SVD map"),
        macro_context=_expect_dict(macro_context, "macro context"),
        macros_index=_expect_dict(macros_index, "macros index"),
    ), machine, svd_device


def _load_list(path: Path, label: str) -> list[dict[str, Any]]:
    payload = load_json(path)
    if not isinstance(payload, list):
        raise TypeError(f"{label} must be a JSON list: {path}")
    return payload


def _load_dict(path: Path, label: str) -> dict[str, Any]:
    payload = load_json(path)
    return _expect_dict(payload, label)


def _expect_dict(payload: Any, label: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise TypeError(f"{label} must be a JSON object")
    return payload
