from __future__ import annotations

from dataclasses import dataclass, field, fields, is_dataclass
from pathlib import Path
from typing import Any


def _json_ready(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if is_dataclass(value):
        if hasattr(value, "to_dict"):
            return value.to_dict()
        return {
            item.name: _json_ready(getattr(value, item.name))
            for item in fields(value)
            if item.metadata.get("serialize", True)
        }
    if isinstance(value, dict):
        return {str(key): _json_ready(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_ready(item) for item in value]
    return value


class SerializableModel:
    def to_dict(self) -> dict[str, Any]:
        return {
            item.name: _json_ready(getattr(self, item.name))
            for item in fields(self)
            if item.metadata.get("serialize", True)
        }


@dataclass(frozen=True)
class TraceAnalyzeRequest(SerializableModel):
    config_path: Path
    work_dir: Path
    latest_run_dir: Path | None = None
    out_prompt: str = "prompt.txt"
    force: bool = False
    reset_work_dir: bool = False
    routing_json: Path | None = None
    force_routing: bool = False
    apply_routing: bool = False
    svd_path: Path | None = None
    io_log: Path | None = None


@dataclass
class RunArtifacts(SerializableModel):
    run_id: str
    run_dir: Path
    probe_result_path: Path
    manifest_path: Path | None = None
    io_log_path: Path | None = None
    qemu_log_path: Path | None = None
    dev_config_path: Path | None = None
    virtuals_dir: Path | None = None
    probe_result: dict[str, Any] = field(default_factory=dict)
    manifest: dict[str, Any] = field(default_factory=dict)


@dataclass
class StaticArtifacts(SerializableModel):
    cache_dir: Path
    symbols_path: Path
    functions_path: Path
    source_map_path: Path
    compile_units_path: Path
    memory_map_path: Path
    svd_map_path: Path
    macro_context_path: Path | None = None
    macros_index_path: Path | None = None
    symbols: list[dict[str, Any]] = field(default_factory=list)
    functions: list[dict[str, Any]] = field(default_factory=list)
    source_map: list[dict[str, Any]] = field(default_factory=list)
    compile_units: list[dict[str, Any]] = field(default_factory=list)
    memory_map: Any = None
    svd_map: dict[str, Any] = field(default_factory=dict)
    macro_context: dict[str, Any] = field(default_factory=dict)
    macros_index: dict[str, Any] = field(default_factory=dict)


@dataclass
class ResolvedFunction(SerializableModel):
    pc: int
    pc_hex: str
    name: str | None = None
    symbol_name: str | None = None
    start_address: int | None = None
    start_address_hex: str | None = None
    size: int | None = None
    offset: int | None = None
    source_path: Path | None = None
    source_root_relative: str | None = None
    line: int | None = None
    confidence: str = "unknown"
    source_map_entry: dict[str, Any] = field(default_factory=dict)


@dataclass
class SourceContext(SerializableModel):
    function: str | None
    source_path: Path | None
    source_root_relative: str | None
    line: int | None
    start_line: int | None
    end_line: int | None
    text: str = ""
    extraction: str = "unavailable"
    provider: str | None = None
    warnings: list[str] = field(default_factory=list)
    header_path: Path | None = None
    header_text: str | None = None
    class_methods_menu: list[str] = field(default_factory=list)


@dataclass
class MacroContext(SerializableModel):
    source_root_relative: str | None = None
    unit_id: str | None = None
    provider: str | None = None
    confidence: str | None = None
    context_artifact: str | None = None
    context_hash: str | None = None
    macro_count: int = 0
    selected_macros: dict[str, str] = field(default_factory=dict)
    selected_macro_names: list[str] = field(default_factory=list)
    selection: dict[str, Any] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)


@dataclass
class ExecTraceContext(SerializableModel):
    final_pc: str
    final_function: str | None = None
    raw_bbl_count: int = 0
    recent_bbl_trace: list[str] = field(default_factory=list)
    resolved_recent_bbls: list[dict[str, Any]] = field(default_factory=list)
    function_sequence: list[str] = field(default_factory=list)
    collapsed_sequence: list[dict[str, Any]] = field(default_factory=list)
    loop_hint: dict[str, Any] | None = None
    panic_string: str | None = None
    fault_stacked_pc: str | None = None
    fault_cfsr: str | None = None
    fault_bfar: str | None = None
    timeout_registers: dict[str, str] | None = None


@dataclass
class PeripheralIOTrace(SerializableModel):
    peripheral: str
    relevant_pcs: list[int] = field(default_factory=list)
    init_accesses: list[str] = field(default_factory=list)
    loop_patterns: list[dict[str, Any]] = field(default_factory=list)
    rare_transitions: list[str] = field(default_factory=list)
    state_behavior: list[str] = field(default_factory=list)
    mmio_functions_menu: list[str] = field(default_factory=list)

@dataclass
class IOTraceContext(SerializableModel):
    io_log_path: Path | None = None
    target_address: int | None = None
    target_address_hex: str | None = None
    target_peripheral: str | None = None
    target_register: str | None = None
    
    # Per-peripheral traces
    peripherals_data: list[PeripheralIOTrace] = field(default_factory=list)
    
    warnings: list[str] = field(default_factory=list)


@dataclass
class TraceAnalysisResult(SerializableModel):
    run_id: str
    status: str
    analysis_dir: Path
    prompt_path: Path
    run_artifacts: RunArtifacts
    static_artifacts: StaticArtifacts
    resolved_function: ResolvedFunction | None = None
    source_context: SourceContext | None = None
    macro_context: MacroContext | None = None
    exec_trace: ExecTraceContext | None = None
    io_trace: IOTraceContext | None = None
    warnings: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


@dataclass
class TraceAnalysisContext(SerializableModel):
    request: TraceAnalyzeRequest
    run_artifacts: RunArtifacts
    static_artifacts: StaticArtifacts
    analysis_dir: Path
    prompt_path: Path
    warnings: list[str] = field(default_factory=list)
    machine: Any = field(default=None, repr=False, metadata={"serialize": False})
    svd_device: Any = field(default=None, repr=False, metadata={"serialize": False})
