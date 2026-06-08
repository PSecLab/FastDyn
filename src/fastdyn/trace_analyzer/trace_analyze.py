from __future__ import annotations

import json
from pathlib import Path

from .models import TraceAnalyzeRequest, TraceAnalysisResult
from .pipeline.run_context import load_trace_analysis_context
from .pipeline.exec_trace import build_exec_trace_context
from .pipeline.source_context import build_source_context
from .pipeline.macro_context import build_macro_context
from .pipeline.io_log_minimize import build_io_trace_context
from .pipeline.prompt_builder import build_prompt
from .utils.symbols import resolve_pc

try:
    import tomllib
except ImportError:
    import tomli as tomllib

def run_trace_analysis(request: TraceAnalyzeRequest) -> TraceAnalysisResult:
    # Stage 4: Load context
    context = load_trace_analysis_context(request)
    
    # Extract modeling_dir from config
    modeling_dir = None
    if context.request.config_path.exists():
        with open(context.request.config_path, "rb") as f:
            cfg = tomllib.load(f)
            mdir = cfg.get("Rehosting", {}).get("directories", {}).get("modeling_dir")
            if mdir:
                modeling_dir = Path(mdir)

    # Stage 6: Exec Trace
    exec_trace = build_exec_trace_context(
        probe_result=context.run_artifacts.probe_result,
        functions=context.static_artifacts.functions,
        symbols=context.static_artifacts.symbols,
        source_map=context.static_artifacts.source_map,
    )
    from fastdyn import toml_parser
    from fastdyn.utils import parse_config

    # Parse config to get svd_device
    fastdyn_handle = toml_parser.parser(
        out_dir=str(context.request.work_dir),
        machine_name="machine0",
        toml_config=str(context.request.config_path),
        svd_path="third_party/common/cmsis-svd-data"
    )
    machine = fastdyn_handle.machines["machine0"]
    svd_device = parse_config.get_svd_device(machine.svd_file)
    
    # Try to extract firmware info
    firmware_info = ""
    with open(context.request.config_path, "rb") as f:
        cfg = tomllib.load(f)
        fw_roots = cfg.get("Rehosting", {}).get("directories", {}).get("firmware_source_roots", [])
        if fw_roots:
            if "ardupilot" in fw_roots[0].lower():
                firmware_info = " (ArduPilot Firmware)"
    
    platform_name = f"{machine.platform}{firmware_info}"
    
    # Stage 9: IO Log Minimize
    io_trace = build_io_trace_context(
        run_artifacts=context.run_artifacts,
        static_artifacts=context.static_artifacts,
        svd_device=svd_device,
    )
    
    # Stage 7: Source Contexts
    source_contexts = []
    
    # 1. Peripheral Initialization Contexts
    if io_trace.relevant_pcs:
        for pc_val in io_trace.relevant_pcs:
            r_func = resolve_pc(
                hex(pc_val),
                functions=context.static_artifacts.functions,
                symbols=context.static_artifacts.symbols,
                source_map=context.static_artifacts.source_map,
            )
            sc = build_source_context(
                resolved_function=r_func,
                source_map=context.static_artifacts.source_map,
            )
            if sc.text and not any(s.text == sc.text for s in source_contexts):
                source_contexts.append(sc)
                
    # 2. Final PC Context
    final_pc = context.run_artifacts.probe_result.get("pc")
    if not final_pc and exec_trace.recent_bbl_trace:
        final_pc = exec_trace.recent_bbl_trace[-1]
    if not final_pc:
        final_pc = "0x0"
        
    resolved_func = resolve_pc(
        final_pc,
        functions=context.static_artifacts.functions,
        symbols=context.static_artifacts.symbols,
        source_map=context.static_artifacts.source_map,
    )
    
    final_source_context = build_source_context(
        resolved_function=resolved_func,
        source_map=context.static_artifacts.source_map,
    )
    
    if final_source_context.text and not any(s.text == final_source_context.text for s in source_contexts):
        source_contexts.append(final_source_context)
    
    # Choose primary source context for macro extraction
    primary_sc = source_contexts[0] if source_contexts else final_source_context
    
    # Stage 8: Macro Context
    macro_context = build_macro_context(
        source_context=primary_sc,
        static_artifacts=context.static_artifacts,
    )
    
    # Stage 10: Prompt Builder
    prompt = build_prompt(
        context=context,
        exec_trace=exec_trace,
        io_trace=io_trace,
        source_contexts=source_contexts,
        macro_context=macro_context,
        modeling_dir=modeling_dir,
        platform_name=platform_name,
    )
    
    # Stage 11: Output Files
    analysis_data = {
        "run_id": context.run_artifacts.run_id,
        "exit_reason": context.run_artifacts.probe_result.get("exit_reason"),
        "final_pc": exec_trace.final_pc,
        "final_function": exec_trace.final_function,
        "target_peripheral": io_trace.target_peripheral,
        "macro_context_artifact": macro_context.context_artifact,
        "source_root_relative": primary_sc.source_root_relative if primary_sc else None,
    }
    
    if modeling_dir and io_trace.target_peripheral:
        model_path = modeling_dir / f"{io_trace.target_peripheral.lower()}.c"
        if model_path.exists():
            analysis_data["target_model_file"] = str(model_path)
    
    with open(context.analysis_dir / "analysis.json", "w") as f:
        json.dump(analysis_data, f, indent=2)
        
    with open(context.analysis_dir / "prompt.txt", "w") as f:
        f.write(prompt)
        
    with open(context.analysis_dir / "selected_source.c", "w") as f:
        f.write("\n\n".join(sc.text for sc in source_contexts if sc.text))
        
    with open(context.analysis_dir / "selected_macros.json", "w") as f:
        json.dump(macro_context.selected_macros, f, indent=2)
        
    with open(context.analysis_dir / "io_context.json", "w") as f:
        json.dump(io_trace.to_dict(), f, indent=2)
        
    with open(context.analysis_dir / "exec_trace.json", "w") as f:
        json.dump(exec_trace.to_dict(), f, indent=2)
        
    with open(context.prompt_path, "w") as f:
        f.write(prompt)
        
    with open(context.request.work_dir / "analysis.json", "w") as f:
        json.dump(analysis_data, f, indent=2)
        
    return TraceAnalysisResult(
        run_id=context.run_artifacts.run_id,
        status="success",
        analysis_dir=context.analysis_dir,
        prompt_path=context.prompt_path,
        run_artifacts=context.run_artifacts,
        static_artifacts=context.static_artifacts,
        resolved_function=resolved_func,
        source_context=primary_sc,
        macro_context=macro_context,
        exec_trace=exec_trace,
        io_trace=io_trace,
    )
