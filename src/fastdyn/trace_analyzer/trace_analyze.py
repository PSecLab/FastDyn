from __future__ import annotations

import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

from .models import SourceContext, TraceAnalyzeRequest, TraceAnalysisResult
from .pipeline.run_context import load_trace_analysis_context
from .pipeline.exec_trace import build_exec_trace_context
from .pipeline.source_context import build_source_context
from .pipeline.macro_context import build_macro_context
from .pipeline.io_log_minimize import build_io_trace_context
from .pipeline.prompt_builder import build_prompt
from .utils.symbols import resolve_pc
from .pipeline.fallback_extractor import run_compile_db_fallback, demangle_cpp_symbol, extract_identity
from .pipeline.routing_apply import (
    apply_routing_materialization,
    plan_routing_materialization,
)

try:
    import tomllib
except ImportError:
    import tomli as tomllib


def _as_list(value):
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def _routing_item_name(item):
    if isinstance(item, str):
        return item
    if isinstance(item, dict):
        return str(item.get("name", "") or "")
    return ""


def _routing_item_refs(item):
    if not isinstance(item, dict):
        return []
    return [str(ref) for ref in _as_list(item.get("reference_functions_needed", [])) if ref]


def _normalize_function_key(name: str) -> str:
    text = " ".join(str(name).strip().split())
    text = re.sub(r"\s*::\s*", "::", text)
    return text


def _add_lookup_key(keys: list[str], value: str | None) -> None:
    if not value:
        return
    key = _normalize_function_key(value)
    if key and key not in keys:
        keys.append(key)


def _short_function_name(name: str) -> str:
    return str(name).split("(", 1)[0].strip()


def _function_lookup_keys(raw_name: str, demangled_name: str | None = None) -> list[str]:
    keys: list[str] = []
    _add_lookup_key(keys, raw_name)
    if demangled_name is None:
        demangled_name = demangle_cpp_symbol(raw_name)
    _add_lookup_key(keys, demangled_name)
    _add_lookup_key(keys, _short_function_name(demangled_name))
    class_name, method_name = extract_identity(demangled_name)
    if class_name and method_name:
        if "::" in demangled_name:
            _add_lookup_key(keys, f"{class_name}::{method_name}")
        _add_lookup_key(keys, method_name)
    return keys


def _demangle_many(names: list[str]) -> dict[str, str]:
    if not names or not shutil.which("c++filt"):
        return {name: name for name in names}
    try:
        proc = subprocess.run(
            ["c++filt"],
            input="\n".join(names) + "\n",
            capture_output=True,
            text=True,
            check=True,
        )
    except (subprocess.CalledProcessError, OSError):
        return {name: name for name in names}

    outputs = proc.stdout.splitlines()
    if len(outputs) != len(names):
        return {name: name for name in names}
    return dict(zip(names, outputs))


def _build_function_lookup(functions: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    names = [str(f.get("name") or "") for f in functions if f.get("name")]
    demangled = _demangle_many(names)
    lookup: dict[str, list[dict[str, Any]]] = {}
    for f in functions:
        raw_name = str(f.get("name") or "")
        if not raw_name:
            continue
        for key in _function_lookup_keys(raw_name, demangled.get(raw_name, raw_name)):
            lookup.setdefault(key, []).append(f)
    return lookup


def _select_requested_function(
    matches: list[dict[str, Any]],
    *,
    exec_trace_functions: set[str],
) -> tuple[dict[str, Any] | None, bool]:
    if not matches:
        return None, False
    trace_matches = [
        match for match in matches
        if str(match.get("name") or "") in exec_trace_functions
    ]
    candidates = trace_matches or matches
    return candidates[0], len(candidates) > 1


def _resolve_requested_function(
    request_name: str,
    function_lookup: dict[str, list[dict[str, Any]]],
    *,
    exec_trace_functions: set[str],
) -> tuple[dict[str, Any] | None, str | None, bool]:
    for key in _function_lookup_keys(request_name):
        matches = function_lookup.get(key, [])
        if matches:
            selected, ambiguous = _select_requested_function(
                matches,
                exec_trace_functions=exec_trace_functions,
            )
            return selected, key, ambiguous
    return None, None, False


def _missing_source_context(function_name: str, warning: str) -> SourceContext:
    return SourceContext(
        function=function_name,
        source_path=None,
        source_root_relative=None,
        line=None,
        start_line=None,
        end_line=None,
        warnings=[warning],
    )


def run_trace_analysis(request: TraceAnalyzeRequest) -> TraceAnalysisResult:
    # 1. Parse Routing JSON to determine prompt_kind
    routing_path = (request.routing_json or (request.work_dir / "routing.json")).expanduser()
    target_peripherals_override = []
    reference_functions_override = []
    routing_context = None
    routing = {}
    prompt_kind = "diagnostic"
    warnings = []
    routing_materialization = None
    
    if routing_path.exists():
        try:
            with open(routing_path, "r", encoding="utf-8") as f:
                routing = json.load(f)
            
            is_handled = routing.get("handled", False)
            if not is_handled or request.force_routing:
                prompt_kind = "routed"
                for ex in routing.get("request_existing_models", []):
                    name = _routing_item_name(ex)
                    if name:
                        periph = Path(name).stem.upper()
                        if periph not in target_peripherals_override:
                            target_peripherals_override.append(periph)
                    if isinstance(ex, dict) and ex.get("intent") == "update":
                        reference_functions_override.extend(_routing_item_refs(ex))
                
                for new_mod in routing.get("create_new_models", []):
                    name = _routing_item_name(new_mod)
                    if name:
                        periph = Path(name).stem.upper()
                        if periph not in target_peripherals_override:
                            target_peripherals_override.append(periph)
                    reference_functions_override.extend(_routing_item_refs(new_mod))
                
                routing_context = routing

                materialization_plan = plan_routing_materialization(
                    routing,
                    request.config_path,
                )
                if materialization_plan.requires_changes:
                    if not request.apply_routing:
                        raise ValueError(
                            materialization_plan.describe()
                            + "\nRerun trace-analyze with --apply-routing to "
                            "review and apply these changes interactively."
                        )

                    routing_materialization = apply_routing_materialization(
                        routing,
                        request.config_path,
                    )
                    remaining_plan = routing_materialization.get("remaining_plan", {})
                    if remaining_plan.get("requires_changes"):
                        remaining_actions = remaining_plan.get("actions", [])
                        details = "\n".join(
                            f"  - {item.get('description', item)}"
                            for item in remaining_actions
                        )
                        raise ValueError(
                            "Routing materialization is incomplete; routed prompt "
                            "generation would be missing requested files/config:\n"
                            f"{details}"
                        )
        except (OSError, json.JSONDecodeError) as e:
            warnings.append(f"Failed to parse routing JSON {routing_path}: {e}")

    # Stage 4: Load context
    svd_p = str(request.svd_path) if request.svd_path else "third_party/common/cmsis-svd-data"
    context = load_trace_analysis_context(request, prompt_kind, svd_path=svd_p)
    context.warnings.extend(warnings)
    
    # Extract directories from config.
    modeling_dir = None
    fw_build_roots = []
    if context.request.config_path.exists():
        with open(context.request.config_path, "rb") as f:
            cfg = tomllib.load(f)
            dirs = cfg.get("Rehosting", {}).get("directories", {})
            mdir = dirs.get("modeling_dir")
            if mdir:
                modeling_dir = Path(mdir)
            fw_build_roots = dirs.get("firmware_build_roots", []) or []

    # Stage 6: Exec Trace
    exec_trace = build_exec_trace_context(
        probe_result=context.run_artifacts.probe_result,
        functions=context.static_artifacts.functions,
        symbols=context.static_artifacts.symbols,
        source_map=context.static_artifacts.source_map,
    )
    machine = context.machine
    svd_device = context.svd_device
    if svd_device is None:
        context.warnings.append("No SVD device available from parsed FastDyn config.")
    
    platform_name = getattr(machine, "platform", None) or "Unknown Platform"
    
    # Routing already parsed above

    # Stage 9: IO Log Minimize
    io_trace = build_io_trace_context(
        run_artifacts=context.run_artifacts,
        static_artifacts=context.static_artifacts,
        svd_device=svd_device,
        target_peripherals_override=target_peripherals_override,
        reference_functions_override=reference_functions_override,
    )
    
    # Stage 7: Source Contexts
    source_contexts = []
    requested_source_contexts = []
    
    # 1. Peripheral Initialization / Explicitly Requested Contexts
    if reference_functions_override:
        function_lookup = _build_function_lookup(context.static_artifacts.functions)
        exec_trace_functions = set(exec_trace.function_sequence)

        for func_name in reference_functions_override:
            found = False
            sc = None
            matched_function, matched_key, ambiguous = _resolve_requested_function(
                func_name,
                function_lookup,
                exec_trace_functions=exec_trace_functions,
            )
            source_result: dict[str, Any] = {
                "request": func_name,
                "status": "unresolved",
                "matched_key": matched_key,
                "matched_function": matched_function.get("name") if matched_function else None,
                "provider": None,
                "source_root_relative": None,
                "warnings": [],
            }

            if ambiguous and matched_function:
                warning = (
                    f"Requested source context {func_name!r} matched multiple static functions; "
                    f"using {matched_function.get('name')!r}."
                )
                context.warnings.append(warning)
                source_result["warnings"].append(warning)

            if matched_function:
                found = True
                r_func = resolve_pc(
                    matched_function.get("start"),
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
            
            # Trigger fallback if DWARF source extraction failed or function wasn't in DWARF functions list
            if not found or not sc or not sc.text:
                fallback_sc, fallback_mc = run_compile_db_fallback(func_name, fw_build_roots)
                if fallback_sc and fallback_sc.text:
                    if not any(s.text == fallback_sc.text for s in source_contexts):
                        sc = fallback_sc
                        source_contexts.append(fallback_sc)
                        fallback_sc.fallback_macro_context = fallback_mc

            if sc and sc.text:
                source_result["status"] = "ok"
                source_result["provider"] = sc.provider
                source_result["source_root_relative"] = sc.source_root_relative
            else:
                warning = (
                    f"Requested source context not found for {func_name!r}."
                )
                if matched_function:
                    warning += f" Matched static function {matched_function.get('name')!r}, but no local source snippet was found."
                source_result["warnings"].append(warning)
                missing_sc = _missing_source_context(func_name, warning)
                source_contexts.append(missing_sc)

            requested_source_contexts.append(source_result)

            # Extract Class Methods Menu
            if sc:
                demangled = demangle_cpp_symbol(source_result["matched_function"] or func_name)
                class_name, _ = extract_identity(demangled)
                if class_name and class_name != func_name:
                    class_len = len(class_name)
                    mangled_substr1 = f"N{class_len}{class_name}"
                    mangled_substr2 = f"NK{class_len}{class_name}"
                    for f in context.static_artifacts.functions:
                        m_name = f.get("name", "")
                        if mangled_substr1 in m_name or mangled_substr2 in m_name:
                            d_m_name = demangle_cpp_symbol(m_name)
                            class_prefix = class_name + "::"
                            if class_prefix in d_m_name:
                                method_n = d_m_name.split(class_prefix)[-1].split('(')[0]
                                if method_n not in sc.class_methods_menu:
                                    sc.class_methods_menu.append(method_n)

    # MMIO Functions Menu
    for p_data in io_trace.peripherals_data:
        is_target = False
        if target_peripherals_override and p_data.peripheral in target_peripherals_override:
            is_target = True
        elif io_trace.target_peripheral and p_data.peripheral == io_trace.target_peripheral:
            is_target = True
            
        if is_target:
            p_menu = set()
            for pc_val in p_data.relevant_pcs:
                r_func = resolve_pc(
                    hex(pc_val),
                    functions=context.static_artifacts.functions,
                    symbols=context.static_artifacts.symbols,
                    source_map=context.static_artifacts.source_map,
                )
                if r_func.name:
                    demangled = demangle_cpp_symbol(r_func.name)
                    p_menu.add(demangled)
                elif r_func.symbol_name:
                    demangled = demangle_cpp_symbol(r_func.symbol_name)
                    p_menu.add(demangled)
            p_data.mmio_functions_menu = sorted(list(p_menu))
                
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
    if hasattr(primary_sc, "fallback_macro_context") and primary_sc.fallback_macro_context:
        macro_context = primary_sc.fallback_macro_context
    else:
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
        machine=machine,
        routing_context=routing_context,
    )
    
    # Stage 11: Output Files
    # (Move routing save to AFTER output files are written)

    # Stage 11: Output Files
    analysis_data = {
        "run_id": context.run_artifacts.run_id,
        "analysis_id": context.analysis_dir.name,
        "prompt_kind": prompt_kind,
        "exit_reason": context.run_artifacts.probe_result.get("exit_reason"),
        "final_pc": exec_trace.final_pc,
        "final_function": exec_trace.final_function,
        "target_peripheral": io_trace.target_peripheral,
        "macro_context_artifact": macro_context.context_artifact,
        "source_root_relative": primary_sc.source_root_relative if primary_sc else None,
        "config_path": str(context.request.config_path.absolute()) if context.request.config_path else None,
        "modeling_dir": str(modeling_dir.absolute()) if modeling_dir else None,
        "io_log_path": str(context.run_artifacts.io_log_path) if context.run_artifacts.io_log_path else None,
        "routing_used": bool(routing_context),
        "requested_source_contexts": requested_source_contexts,
        "warnings": [
            *context.warnings,
            *io_trace.warnings,
            *[warning for sc in source_contexts for warning in sc.warnings],
            *macro_context.warnings,
        ],
    }
    if routing_materialization is not None:
        analysis_data["routing_materialization"] = routing_materialization
    
    if modeling_dir:
        target_files = []
        routing_for_targets = routing_context or {}
        if "request_existing_models" in routing_for_targets:
            for em in routing_for_targets.get("request_existing_models", []):
                name = _routing_item_name(em)
                if not name:
                    continue
                p = modeling_dir / name
                if p.exists() and str(p) not in target_files:
                    target_files.append(str(p))
            for nm in routing_for_targets.get("create_new_models", []):
                if not isinstance(nm, dict):
                    continue
                name = str(nm.get("name", "") or "").strip()
                if not name:
                    continue
                category = str(nm.get("category", "") or "").strip().lower()
                if category == "endpoint" and not name.endswith(".py"):
                    continue
                filename = name if (name.endswith(".c") or name.endswith(".py")) else f"{name}.c"
                p = modeling_dir / filename
                if p.exists() and str(p) not in target_files:
                    target_files.append(str(p))
                    
        if not target_files and io_trace.target_peripheral:
            model_path = modeling_dir / f"{io_trace.target_peripheral.lower()}.c"
            if model_path.exists():
                target_files.append(str(model_path))
                
        if target_files:
            analysis_data["target_model_file"] = target_files
    
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
        
    # Now mark routing as handled and save original routing input
    if routing_context:
        try:
            with open(context.analysis_dir / "routing_input.json", "w") as f:
                json.dump(routing_context, f, indent=2)
            
            routing["handled"] = True
            with open(routing_path, "w") as f:
                json.dump(routing, f, indent=2)
        except OSError:
            pass

    # Update index.json
    index_path = context.request.work_dir / "trace_analysis" / "index.json"
    index_data = []
    if index_path.exists():
        try:
            with open(index_path, "r", encoding="utf-8") as f:
                index_data = json.load(f)
            if not isinstance(index_data, list):
                index_data = []
        except (OSError, json.JSONDecodeError):
            index_data = []
            
    import datetime
    now_str = datetime.datetime.now().isoformat()
    entry = {
        "analysis_id": context.analysis_dir.name,
        "run_id": context.run_artifacts.run_id,
        "created_at": now_str,
        "source_run_time": context.run_artifacts.manifest.get("source_run_time", now_str),
        "prompt_kind": prompt_kind,
        "exit_reason": context.run_artifacts.probe_result.get("exit_reason", "unknown"),
        "final_pc": exec_trace.final_pc,
        "run_dir": str(context.run_artifacts.run_dir.absolute()),
        "prompt_path": str(context.prompt_path.absolute()),
        "routing_used": bool(routing_context),
    }
    index_data = [
        existing for existing in index_data
        if isinstance(existing, dict) and existing.get("analysis_id") != entry["analysis_id"]
    ]
    index_data.insert(0, entry)
    index_data.sort(key=lambda item: item.get("created_at", ""), reverse=True)
    
    try:
        with open(index_path, "w", encoding="utf-8") as f:
            json.dump(index_data, f, indent=2)
    except OSError:
        pass
        
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
