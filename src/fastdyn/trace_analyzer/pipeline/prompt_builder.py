from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ..models import (
    ExecTraceContext,
    IOTraceContext,
    MacroContext,
    SourceContext,
    TraceAnalysisContext,
)
from fastdyn.verifier.prompt_gen import (
    framework_rules,
    observability_guidance,
    inter_peripheral_guidance,
    qemu_base_api_list,
    qemu_api_list,
    search_replace_prompting,
)


# ---------------------------------------------------------------------------
# JSON routing schema the LLM must always emit at the end of its response.
# ---------------------------------------------------------------------------
ROUTING_JSON_SCHEMA = """\
```json
{
  "veto_pipeline_guess": <true | false>,
  "reasoning": "<one or two sentences explaining your peripheral/model decision>",

  "request_existing_models": [
    // You may request multiple models by adding more objects to this array.
    {
      "name": "<filename.c>",
      "intent": "<context_only | update>",
      "reference_functions_needed": ["<func1>", "<func2>"] 
    }
  ],

  "create_new_models": [
    // You may create multiple models by adding more objects to this array.
    {
      "name": "<filename.c or filename.py>",
      "category": "<peripheral | slave | endpoint>",
      "bus_type": "<SPI | I2C | UART | null>",
      "mmio_range": "<0xSTART-0xEND or null if category is slave/endpoint>",
      "attach_to_peripheral": "<parent peripheral name, e.g. spi2, or null>",
      "connection_id": "<how it connects, e.g. 'CS: PD10', 'Addr: 0x77', 'Baud: 115200', or null>",
      "reference_functions_needed": ["<func1>"],
      "details": "<brief description of what this model must implement>"
    }
  ]
}
```

### Field Reference
| Field | Description |
|-------|-------------|
| `veto_pipeline_guess` | `true` if the pipeline's peripheral guess is wrong and you are redirecting to different files. |
| `request_existing_models[].name` | Filename of an existing `.c` or `.py` model you need passed to the *implementation* phase. |
| `request_existing_models[].intent` | `"context_only"` if you just need to read its interfaces. The LLM will receive the current C model file and the minimized IO trace for this peripheral, but NO driver source code or macros. `"update"` if you intend to rewrite it. The LLM will receive the model file, the minimized IO trace, AND the dynamically extracted firmware driver source code and macros. |
| `request_existing_models[].reference_functions_needed` | Optional. If `intent` is `"update"`, specify an array of firmware function names you need reference code for. If omitted, the pipeline defaults to fetching *all* functions that accessed its MMIOs. |
| `create_new_models[].reference_functions_needed` | Optional. If you need firmware source code for this new device, specify an array of function names from the Execution Trace. **CRITICAL:** For slave/attached devices, since they have no direct MMIO ranges, the pipeline cannot auto-detect their functions. You MUST explicitly list the functions here if you want their source code. |
| `create_new_models[].category` | `"peripheral"` for a new MMIO device; `"slave"` for a C-modeled device on a bus; `"endpoint"` for a host-side plugin (e.g., a `.py` script connecting to a PTY or Gazebo). Note: Requesting an `"endpoint"` with a `.py` extension allows you to write the python script. |
| `create_new_models[].bus_type` | `"SPI"`, `"I2C"`, or `"UART"` if `category` is `"slave"`. This allows the pipeline to auto-generate the correct SDK callbacks (e.g., `api_spi_transfer` vs `api_i2c_send`). Use `null` for peripherals or endpoints. |
| `create_new_models[].mmio_range` | Required for `peripheral`; omit (null) for `slave` or `endpoint`. |
| `create_new_models[].attach_to_peripheral` | Required for `slave` or `endpoint`; the bus-master peripheral name. |
| `create_new_models[].connection_id` | The CS pin, I2C address, baud rate, or other connection discriminator. |
"""


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _build_api_and_rules_block() -> list[str]:
    """Common API / rules preamble shared by both prompt modes."""
    parts: list[str] = []
    parts.append("## Available APIs")
    parts.append(
        "You **must** use the following APIs to construct the device model. "
        "Pay close attention to the read/write callback signatures.\n"
        "Including `<device.h>` and `<boardrunner/vio.h>` will give you access to all APIs mentioned. "
        "You MUST ensure these headers are included in any C files you create or update."
    )
    parts.append(qemu_api_list)
    parts.append("")
    parts.append("### Missing API Policy (HARD BLOCKER)")
    parts.append(
        "If the model requires an API that is not listed above, you MUST stop "
        "and NOT generate the device model. Instead:"
    )
    parts.append("1. Clearly name the missing API and explain why it is required.")
    parts.append("2. Propose a precise signature for the missing API.")
    parts.append("3. STOP here. Do not generate the model.\n")
    parts.append("## Critical Framework Rules")
    parts.append(framework_rules)
    parts.append("")
    parts.append("## Observability & Host I/O")
    parts.append(observability_guidance)
    parts.append("")
    parts.append("## Inter-Peripheral Communication")
    parts.append(inter_peripheral_guidance)
    parts.append("")
    return parts


def _build_execution_trace_block(exec_trace: ExecTraceContext) -> list[str]:
    parts: list[str] = []
    parts.append("## Execution Trace Summary")
    
    if exec_trace.panic_string:
        parts.append("> [!CRITICAL]")
        parts.append("> **Software Panic Detected!** The firmware explicitly aborted with the following message:")
        parts.append(f"> `{exec_trace.panic_string}`\n")
        
    if exec_trace.fault_stacked_pc:
        parts.append("> [!WARNING]")
        parts.append("> **Hardware Fault Detected!** The CPU triggered an exception handler.")
        parts.append(f"> - Stacked PC (Faulting Instruction): `{exec_trace.fault_stacked_pc}`")
        if exec_trace.fault_cfsr:
            parts.append(f"> - CFSR (Fault Status): `{exec_trace.fault_cfsr}`")
        if exec_trace.fault_bfar:
            parts.append(f"> - BFAR (Bus Fault Addr): `{exec_trace.fault_bfar}`\n")

    if exec_trace.timeout_registers:
        parts.append("### CPU Register State (Timeout / Infinite Loop)")
        parts.append("The firmware got stuck. Here is the CPU register state at the moment of exit:")
        parts.append("```text")
        for reg, val in exec_trace.timeout_registers.items():
            parts.append(f"{reg}: {val}")
        parts.append("```\n")

    parts.append("### Global Execution Path (Compressed)")
    compressed_steps = [item["name"] for item in exec_trace.collapsed_sequence]
    parts.append(" -> ".join(compressed_steps))
    parts.append("")
    return parts


def _format_rtos_thread(thread: Any) -> str:
    if not isinstance(thread, dict):
        return "unavailable"
    name = thread.get("name") or "(unnamed)"
    addr = thread.get("addr") or "unknown"
    priority = thread.get("priority", "?")
    state = thread.get("state", "?")
    scheduled_count = thread.get("scheduled_count")
    suffix = ""
    if scheduled_count is not None:
        suffix = f", scheduled={scheduled_count}"
    return f"{name} addr={addr} prio={priority} state={state}{suffix}"


def _build_rtos_context_block(context: TraceAnalysisContext) -> list[str]:
    parts: list[str] = []
    probe_rtos = context.run_artifacts.probe_result.get("rtos")
    if not isinstance(probe_rtos, dict):
        probe_rtos = {}
    summary_rtos = context.run_artifacts.rtos_summary or {}
    identity = context.static_artifacts.rtos_identity or {}
    schema = context.static_artifacts.rtos_schema or {}

    if not probe_rtos and not summary_rtos and not identity.get("available"):
        return parts

    runtime = summary_rtos if summary_rtos else probe_rtos
    parts.append("## RTOS Introspection Context")

    rtos_name = runtime.get("name") or identity.get("rtos") or "unknown"
    available = identity.get("available")
    parts.append(f"**Detected RTOS:** {rtos_name}")
    if available is not None:
        parts.append(f"**Static Introspection Available:** {available}")
    if schema.get("schema_path"):
        parts.append(f"**Schema Artifact:** `{schema.get('schema_path')}`")

    if runtime:
        parts.append(f"**Runtime Mode:** {runtime.get('mode', 'unknown')}")
        parts.append(f"**Context Switches Observed:** {runtime.get('switch_count', 0)}")
        parts.append(f"**Stored Recent Switches:** {runtime.get('stored_switch_count', 0)}")
        parts.append(f"**Current Thread:** {_format_rtos_thread(runtime.get('current_thread'))}")

        if context.run_artifacts.rtos_summary_path:
            parts.append(f"**RTOS Summary File:** `{context.run_artifacts.rtos_summary_path}`")
        recent_path = context.run_artifacts.rtos_recent_switches_path
        if recent_path:
            parts.append(f"**Recent Switch Log:** `{recent_path}`")

        threads = runtime.get("threads")
        if isinstance(threads, list) and threads:
            sorted_threads = sorted(
                [t for t in threads if isinstance(t, dict)],
                key=lambda item: int(item.get("scheduled_count") or 0),
                reverse=True,
            )
            parts.append("### Top Known Threads")
            for thread in sorted_threads[:8]:
                parts.append(f"- {_format_rtos_thread(thread)}")

        recent = runtime.get("recent_switches")
        if isinstance(recent, list) and recent:
            parts.append("### Recent Context Switches")
            for event in recent[-8:]:
                if not isinstance(event, dict):
                    continue
                out_thread = _format_rtos_thread(event.get("out"))
                in_thread = _format_rtos_thread(event.get("in"))
                parts.append(
                    f"- seq={event.get('seq', '?')} icount={event.get('icount', '?')}: "
                    f"{out_thread} -> {in_thread}"
                )

    parts.append("")
    return parts


def _build_source_block(source_contexts: list[SourceContext]) -> list[str]:
    parts: list[str] = []
    parts.append("## Source Code Context\n")
    if not source_contexts:
        parts.append("*Source code unavailable.*")
    else:
        seen_headers = set()
        for sc in source_contexts:
            if sc.header_text and sc.header_path not in seen_headers:
                seen_headers.add(sc.header_path)
                parts.append(f"Header definitions from `{sc.header_path}`:")
                parts.append("```c")
                parts.append(sc.header_text)
                parts.append("```\n")

            if sc.text:
                parts.append(
                    f"Source snippet from `{sc.source_root_relative}` "
                    f"(lines {sc.start_line}-{sc.end_line}):"
                )
                parts.append("```c")
                parts.append(sc.text)
                parts.append("```\n")
            elif sc.warnings:
                function = sc.function or "unknown"
                parts.append(f"*Source unavailable for `{function}`:*")
                for warning in sc.warnings:
                    parts.append(f"- {warning}")
                parts.append("")
                
            if sc.class_methods_menu:
                parts.append(f"**Other methods available in this class:**")
                for method in sc.class_methods_menu:
                    parts.append(f"- `{method}`")
                parts.append("")
    return parts


def _build_macro_block(macro_context: MacroContext) -> list[str]:
    parts: list[str] = []
    parts.append("\n## Macro Definitions\n")
    parts.append(
        "> **Note:** These macros are EXACTLY as evaluated for this specific "
        "compile-unit. They include active #ifdef branches and numeric values."
    )
    if macro_context.warnings:
        parts.append("\n> [!WARNING]")
        for w in macro_context.warnings:
            parts.append(f"> {w}")
        parts.append("")
    prov = "DWARF" if macro_context.provider == "dwarf_debug_macro" else str(macro_context.provider)
    parts.append(f"**Provenance:** {prov} (Context Artifact: `{macro_context.context_artifact}`)")
    if macro_context.selected_macros:
        parts.append("```c")
        for name in macro_context.selected_macro_names:
            raw = macro_context.selected_macros[name]
            parts.append(f"{raw}")
        parts.append("```")
    else:
        parts.append("*No specific macros identified.*")
    return parts


def _build_io_trace_block(io_trace: IOTraceContext) -> list[str]:
    parts: list[str] = []
    
    if not io_trace.peripherals_data:
        parts.append("## Execution Trace Summary\nNo peripherals were requested or identified for IO trace extraction.\n")
        return parts
        
    for p_data in io_trace.peripherals_data:
        parts.append(f"## Execution Trace Summary ({p_data.peripheral})")
        
        if p_data.mmio_functions_menu:
            parts.append(f"### MMIO Drivers Menu")
            parts.append("The following functions interacted with this peripheral's MMIO registers. Use the VETO command to request their source code if you need it to model the hardware:")
            for func_name in p_data.mmio_functions_menu:
                parts.append(f"- `{func_name}`")
            parts.append("")

        parts.append("### Initialization Sequence (`init.txt`):")
        parts.append(
            "This section contains all accesses that occur before the main runtime loop begins."
        )
        if p_data.init_accesses:
            parts.append("```")
            for line in p_data.init_accesses:
                parts.append(line.strip())
            parts.append("```\n")
        else:
            parts.append(f"No initialization sequence detected for {p_data.peripheral}.\n")

        parts.append("### Detected Runtime Loops (`loop_pattern_*.txt`):")
        parts.append(
            "This section contains the most common repeating sequences of operations during runtime."
        )
        if p_data.loop_patterns:
            parts.append("```")
            for lp in p_data.loop_patterns:
                parts.append(f"--- {lp['header']} ---")
                for acc in lp["accesses"]:
                    parts.append(acc.strip())
                parts.append("")
            parts.append("```\n")
        else:
            parts.append(f"No runtime loops detected for {p_data.peripheral}.\n")

        parts.append("### Rare Value Transitions (`rare_transitions.txt`):")
        parts.append(
            "These are read values from LOW-entropy (status/control) registers that did NOT "
            "appear in the dominant loop patterns above. They represent infrequent but important "
            "state changes that the model MUST handle correctly."
        )
        if p_data.rare_transitions:
            parts.append("```")
            for line in p_data.rare_transitions:
                parts.append(line.strip())
            parts.append("```\n")
        else:
            parts.append(f"No rare value transitions detected for {p_data.peripheral}.\n")

        parts.append("### Stateful Behavior Analysis (`state.txt`):")
        parts.append(
            "This section identifies programming patterns like Read-Modify-Write (RMW), "
            "which indicate stateful registers."
        )
        if p_data.state_behavior:
            parts.append("```")
            for line in p_data.state_behavior:
                parts.append(line.strip())
            parts.append("```\n\n")
        else:
            parts.append(f"No stateful behavior detected for {p_data.peripheral}.\n\n")
            
    return parts


def _build_memory_trace_block(context: TraceAnalysisContext, io_trace: IOTraceContext) -> list[str]:
    parts = []
    mem_log_path = context.run_artifacts.run_dir / "memory.log"
    if not mem_log_path.exists():
        from pathlib import Path
        fallback_path = Path("memory.log")
        if fallback_path.exists():
            mem_log_path = fallback_path

    if mem_log_path.exists():
        parts.append("## Memory & DMA Transfer Logs (`memory.log`)")
        parts.append("This section shows recent DMA and memory transfers prior to the execution exit.")
        parts.append("```text")
        lines = mem_log_path.read_text().splitlines()
        
        filtered_lines = lines
        if io_trace.target_peripheral:
            import re
            addr_to_periph = {}
            for p in context.static_artifacts.svd_map.get("peripherals", []):
                p_name = p.get("name")
                for r in p.get("registers", []):
                    r_addr_raw = r.get("address")
                    if r_addr_raw is not None:
                        r_addr = int(r_addr_raw, 16) if isinstance(r_addr_raw, str) else r_addr_raw
                        addr_to_periph[r_addr] = p_name
                        
            target_periph = io_trace.target_peripheral.upper()
            matched_lines = []
            for line in lines:
                m = re.search(r'\(Periph: (0x[0-9A-Fa-f]+)\)', line)
                if m:
                    addr = int(m.group(1), 16)
                    periph = addr_to_periph.get(addr)
                    if periph and periph.upper() == target_periph:
                        matched_lines.append(line)
            
            if matched_lines:
                filtered_lines = matched_lines

        for line in filtered_lines[-50:]:  # Tail last 50 lines
            parts.append(line.strip())
        parts.append("```\n")
    return parts


# ---------------------------------------------------------------------------
# Mode A: Pipeline has a peripheral guess (known peripheral)
# ---------------------------------------------------------------------------

def _build_known_peripheral_prompt(
    context: TraceAnalysisContext,
    exec_trace: ExecTraceContext,
    io_trace: IOTraceContext,
    source_contexts: list[SourceContext],
    macro_context: MacroContext,
    board_config_summary: str,
    modeling_dir: Path | None,
    platform_name: str,
    target_periph: str,
) -> str:
    parts: list[str] = []

    # Preamble
    parts.append("Take this prompt independent from previous prompt history.\n")
    parts.append(
        f"You are an expert reverse engineer specializing in embedded systems and "
        f"writing C emulation for peripherals. You have read the reference manual for "
        f"the {platform_name} platform with special familiarity with the "
        f"{target_periph} peripheral."
    )
    parts.append(
        "Your task is to analyze the following summary of MMIO trace data and "
        "generate a complete C device model.\n"
    )

    # APIs + Rules
    parts.extend(_build_api_and_rules_block())

    # VETO policy — empowers the LLM to override the pipeline's peripheral guess
    parts.append("### Peripheral Misattribution & Dependency Policy (VETO)")
    parts.append(
        "The pipeline has guessed that **`{periph}`** is the peripheral causing the "
        "current failure. However, **you must verify this independently** by examining "
        "the Global Execution Path, the IO trace, and the Board Hardware Configuration "
        "below.".format(periph=target_periph)
    )
    parts.append(
        "If your analysis shows that fixing the fault requires MORE than just modifying "
        "this single `{periph}` file—for example, if it requires modifying a *different* peripheral, "
        "or creating/modifying attached devices/endpoints, or updating dependent peripherals (like GPIO or DMA)—you MUST VETO this request."
        .format(periph=target_periph)
    )
    parts.append(
        "To VETO, do NOT write any C code. Instead, **start your response with the exact word `VETO:`** "
        "followed immediately by the JSON routing block wrapped in ```json ... ``` (schema at the end of this prompt) to pause the pipeline and request the missing models."
    )
    parts.append(
        "If the pipeline's guess is 100% correct and you have all the files you need to fix the issue in the context, "
        "do NOT output the JSON block. Just output the C code using the SEARCH/REPLACE format."
    )


    # Board config summary (key new addition)
    parts.append("--- START OF ANALYSIS DATA ---\n")
    parts.append(board_config_summary)

    # Platform / peripheral name
    parts.append("## Platform:")
    parts.append(f"{platform_name}")
    parts.append("")
    parts.append("## Pipeline-Selected Peripheral:")
    parts.append(
        f"`{target_periph}` *(verify this is actually the root cause — see VETO policy above)*"
    )
    parts.append("")

    # Target peripheral TOML config (for the guessed peripheral only)
    parts.append("## Target Peripheral TOML Configuration")
    parts.append(
        "This is the current TOML configuration for the pipeline-selected peripheral."
    )
    if context.request.config_path.exists():
        import re
        with open(context.request.config_path, "r") as f:
            content = f.read()
        target_lower = target_periph.lower()
        pattern = re.compile(
            rf"^\[Device\.{target_lower}\](.*?)(?=^\[Device\.|^\[Memory\.|^\[Machine\.|^\[Rehosting\.|\Z)",
            re.MULTILINE | re.DOTALL,
        )
        match = pattern.search(content)
        if match:
            parts.append("```toml")
            parts.append(f"[Device.{target_lower}]")
            parts.append(match.group(1).strip())
            parts.append("```\n")
        else:
            parts.append("*Configuration not found.*\n")
    else:
        parts.append("*Configuration not found.*\n")

    # Exit reason
    parts.append("## Exit Reason & Target Register:")
    parts.append(
        f"**Exit Reason:** {context.run_artifacts.probe_result.get('exit_reason', 'UNKNOWN')}"
    )
    if io_trace.target_address:
        parts.append(
            f"**Faulting Address:** {io_trace.target_address_hex} ({io_trace.target_register})"
        )
    parts.append(f"**Final Function:** {exec_trace.final_function or exec_trace.final_pc}")
    parts.append("")

    # RTOS context
    parts.extend(_build_rtos_context_block(context))

    # IO trace
    parts.extend(_build_io_trace_block(io_trace))
    parts.extend(_build_memory_trace_block(context, io_trace))

    # Exec trace
    parts.extend(_build_execution_trace_block(exec_trace))

    # Source context
    parts.extend(_build_source_block(source_contexts))

    # Macros
    parts.extend(_build_macro_block(macro_context))

    # Current model code (if it exists)
    if modeling_dir and io_trace.target_peripheral:
        model_path = modeling_dir / f"{io_trace.target_peripheral.lower()}.c"
        if model_path.exists():
            with open(model_path, "r") as f:
                model_code = f.read()
            parts.append("\n## Current Device Model Source\n")
            parts.append(
                "The current device model source code is provided below. "
                "You MUST base your analysis on this code.\n"
            )
            parts.append("```c")
            parts.append(model_code.strip())
            parts.append("```")

    # Task + SEARCH/REPLACE format
    parts.append("\n---\n")
    parts.append(search_replace_prompting)
    parts.append("\n")
    parts.append(
        "Please analyze the failure and provide the corrected device model "
        "using the SEARCH/REPLACE block format."
    )

    # Structured output schema (always last)
    parts.append("\n---\n")
    parts.append(ROUTING_JSON_SCHEMA)

    return "\n".join(parts)

# ---------------------------------------------------------------------------
# Mode C: Post-Routing Implementation Phase
# ---------------------------------------------------------------------------

def _build_implementation_prompt(
    context: TraceAnalysisContext,
    exec_trace: ExecTraceContext,
    io_trace: IOTraceContext,
    source_contexts: list[SourceContext],
    macro_context: MacroContext,
    board_config_summary: str,
    modeling_dir: Path | None,
    platform_name: str,
    routing_context: dict,
) -> str:
    parts: list[str] = []

    # Preamble
    parts.append("Take this prompt independent from previous prompt history.\n")
    parts.append(
        f"You are an expert reverse engineer specializing in embedded systems and "
        f"writing C emulation for peripherals."
    )
    
    reasoning = routing_context.get("reasoning", "")
    parts.append("In a previous diagnostic step, the LLM in the previous iteration determined the following:")
    if reasoning:
        parts.append(f"> {reasoning}\n")

    # APIs + Rules
    parts.extend(_build_api_and_rules_block())

    # VETO policy for Implementation Phase
    parts.append("### Implementation Phase VETO Policy")
    parts.append(
        "You are currently in the Implementation Phase. The files provided below are marked as either "
        "`[CONTEXT ONLY]` (read-only reference) or `[CONTEXT AND UPDATE]` (you must provide SEARCH/REPLACE blocks for them)."
    )
    parts.append(
        "However, if while reviewing the files you realize that a `[CONTEXT ONLY]` model actually needs to be updated, "
        "or you discover you are missing a dependent model entirely, you MUST VETO again."
    )
    parts.append(
        "To VETO, do NOT write any C code. Instead, **start your response with the exact word `VETO:`** "
        "followed immediately by the JSON routing block wrapped in ```json ... ``` (schema at the end of this prompt)."
    )
    parts.append(
        "**CRITICAL PIPELINE RULE:** If you VETO, your new JSON block completely resets the pipeline state. "
        "You **MUST re-request ALL models you need** for the next iteration. If you want to keep a file that is "
        "currently provided in this prompt, you MUST explicitly include it in your new JSON block (e.g., as `\"intent\": \"update\"`), "
        "otherwise the pipeline will drop it in the next run."
    )
    parts.append(
        "If you have all the necessary files to complete the implementation, do NOT output the JSON block. "
        "Just output the C code using the SEARCH/REPLACE format."
    )

    parts.append("--- START OF ANALYSIS DATA ---\n")
    parts.append(board_config_summary)

    parts.append("## Platform:")
    parts.append(f"{platform_name}")
    parts.append("")
    
    # Exit reason
    parts.append("## Exit Reason & Target Register:")
    parts.append(
        f"**Exit Reason:** {context.run_artifacts.probe_result.get('exit_reason', 'UNKNOWN')}"
    )
    if io_trace.target_address:
        parts.append(
            f"**Faulting Address:** {io_trace.target_address_hex} ({io_trace.target_register})"
        )
    parts.append(f"**Final Function:** {exec_trace.final_function or exec_trace.final_pc}")
    parts.append("")

    # RTOS context
    parts.extend(_build_rtos_context_block(context))

    # IO trace
    parts.extend(_build_io_trace_block(io_trace))
    parts.extend(_build_memory_trace_block(context, io_trace))

    # Exec trace
    parts.extend(_build_execution_trace_block(exec_trace))

    # Source context
    parts.extend(_build_source_block(source_contexts))

    # Macros
    parts.extend(_build_macro_block(macro_context))

    # Task + SEARCH/REPLACE format
    parts.append("\n---\n")
    parts.append(search_replace_prompting)
    parts.append("\n")

    parts.append("You have been provided with the requested source files to implement the necessary logic to fix the crash.")
    parts.append("Please provide the corrected device models using the SEARCH/REPLACE block format.\n")
    parts.append("## Provided Source Files\n")
    
    if modeling_dir:
        for ex in routing_context.get("request_existing_models", []):
            name = ex.get("name", "")
            intent = ex.get("intent", "context_only").lower()
            tag = "[CONTEXT AND UPDATE]" if intent == "update" else "[CONTEXT ONLY]"
            if name:
                epath = modeling_dir / name
                if epath.exists():
                    parts.append(f"// FILE: {name} {tag}\n```c\n{epath.read_text()}\n```\n")
                else:
                    parts.append(f"// FILE: {name} {tag} (NOT FOUND)\n")
                    
        for new_mod in routing_context.get("create_new_models", []):
            name = new_mod.get("name", "")
            category = new_mod.get("category")
            if name:
                if category == "endpoint" and not name.endswith(".py"):
                    continue # Skip non-python endpoints like Gazebo
                
                fname = name if (name.endswith(".c") or name.endswith(".py")) else f"{name}.c"
                epath = modeling_dir / fname
                lang = "python" if fname.endswith(".py") else "c"
                
                if epath.exists():
                    parts.append(f"// FILE: {fname} [CONTEXT AND UPDATE]\n```{lang}\n{epath.read_text()}\n```\n")

    # Structured output schema (always last, allowing VETO during implementation)
    parts.append("\n---\n")
    parts.append(ROUTING_JSON_SCHEMA)

    return "\n".join(parts)


# ---------------------------------------------------------------------------
# Mode B: Unknown peripheral / panic — pipeline is blind
# ---------------------------------------------------------------------------

def _build_unknown_peripheral_prompt(
    context: TraceAnalysisContext,
    exec_trace: ExecTraceContext,
    io_trace: IOTraceContext,
    source_contexts: list[SourceContext],
    macro_context: MacroContext,
    board_config_summary: str,
    platform_name: str,
) -> str:
    parts: list[str] = []

    # Preamble
    parts.append("Take this prompt independent from previous prompt history.\n")
    parts.append(
        f"You are an expert reverse engineer specializing in embedded systems "
        f"and firmware emulation for the {platform_name} platform."
    )
    parts.append(
        "The automated rehosting pipeline was **unable to identify which peripheral "
        "is causing the current firmware failure**. The firmware either panicked, hit "
        "an assert, or stalled in a way that did not map to a known MMIO address.\n"
    )
    parts.append(
        "**Your task is purely diagnostic and structural**: analyze the evidence below, "
        "identify the root-cause peripheral(s) or missing model(s), and tell the "
        "pipeline exactly what files to fetch and/or create for the next modeling phase. "
        "**Do NOT write any C code in this response.**\n"
    )

    # APIs (for awareness / VETO context, not for code generation)
    parts.extend(_build_api_and_rules_block())

    # Board config summary
    parts.append("--- START OF ANALYSIS DATA ---\n")
    parts.append(board_config_summary)

    # Platform
    parts.append("## Platform:")
    parts.append(f"{platform_name}")
    parts.append("")

    # Exit reason
    exit_reason = context.run_artifacts.probe_result.get("exit_reason", "UNKNOWN")
    parts.append("## Exit Reason:")
    parts.append(f"**{exit_reason}** — the pipeline could not map this to an MMIO peripheral.")
    parts.append(f"**Final PC:** {exec_trace.final_pc}")
    parts.append(f"**Final Function:** {exec_trace.final_function or 'unresolved'}")
    parts.append("")
    parts.append(
        "> The above function/PC is where execution stopped. Focus your analysis "
        "on *why* it stopped — the MMIO trace and execution path below provide the evidence."
    )
    parts.append("")

    # RTOS context
    parts.extend(_build_rtos_context_block(context))

    # IO trace
    parts.extend(_build_io_trace_block(io_trace))
    parts.extend(_build_memory_trace_block(context, io_trace))

    # Exec trace (the most important clue in the unknown case)
    parts.append("## Execution Trace Summary")
    parts.append(
        "### Global Execution Path (Compressed)\n"
        "> **This is the most important section.** Trace the path from the last known "
        "good state to the crash/panic. The root-cause peripheral is likely the one "
        "being initialized or accessed just before the path terminates."
    )
    compressed_steps = [item["name"] for item in exec_trace.collapsed_sequence]
    parts.append(" -> ".join(compressed_steps))
    parts.append("")

    # Source context
    parts.extend(_build_source_block(source_contexts))

    # Macros
    parts.extend(_build_macro_block(macro_context))

    # Diagnostic instructions
    parts.append("\n---\n")
    parts.append("## Diagnostic Instructions")
    parts.append(
        "1. Trace the execution path from top to bottom. Identify the last peripheral "
        "subsystem the firmware was interacting with before it crashed."
    )
    parts.append(
        "2. Cross-reference with the **Board Hardware Configuration** table above. "
        "Determine if the peripheral is already modeled (has an entry), if it has any "
        "devices/connections configured, and if those attached devices are enabled."
    )
    parts.append(
        "3. Determine whether the fix requires:\n"
        "   - **Updating an existing model** (it exists but behaves incorrectly)\n"
        "   - **Creating a new peripheral model** (MMIO range not yet handled)\n"
        "   - **Creating a new attached device or endpoint** (a real C-modeled device, or a host-side plugin on an existing bus)\n"
        "   - **A combination of the above** (e.g., update an existing bus controller and create an attached device)"
    )
    parts.append(
        "4. Output your findings as plain English reasoning. **You MUST end your response with the exact JSON block below.** "
        "The pipeline's Python code parses this block to determine the next action."
    )

    # Structured output schema
    parts.append("\n---\n")
    parts.append(ROUTING_JSON_SCHEMA)

    return "\n".join(parts)


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------

def build_prompt(
    context: TraceAnalysisContext,
    exec_trace: ExecTraceContext,
    io_trace: IOTraceContext,
    source_contexts: list[SourceContext],
    macro_context: MacroContext,
    modeling_dir: Path | None = None,
    platform_name: str = "Unknown Platform",
    machine: Any = None,
    routing_context: dict | None = None,
) -> str:
    """
    Build the full LLM prompt.

    Two modes:
      - **Known peripheral** (io_trace.target_peripheral is set):
        Classic model-generation prompt + VETO mechanism + board config summary.
      - **Unknown peripheral / panic** (io_trace.target_peripheral is None):
        Diagnostic-only prompt asking the LLM to identify the root cause and
        return a structured JSON routing decision.

    `machine` is the FastDyn Machine object used to build the board config summary.
    """
    from .board_config_builder import build_board_config_summary

    board_config_summary = ""
    if machine is not None:
        try:
            board_config_summary = build_board_config_summary(machine=machine)
        except Exception as exc:
            board_config_summary = f"*Board config extraction failed: {exc}*\n"

    if routing_context:
        return _build_implementation_prompt(
            context=context,
            exec_trace=exec_trace,
            io_trace=io_trace,
            source_contexts=source_contexts,
            macro_context=macro_context,
            board_config_summary=board_config_summary,
            modeling_dir=modeling_dir,
            platform_name=platform_name,
            routing_context=routing_context,
        )

    target_periph = io_trace.target_peripheral

    if target_periph:
        return _build_known_peripheral_prompt(
            context=context,
            exec_trace=exec_trace,
            io_trace=io_trace,
            source_contexts=source_contexts,
            macro_context=macro_context,
            board_config_summary=board_config_summary,
            modeling_dir=modeling_dir,
            platform_name=platform_name,
            target_periph=target_periph,
        )
    else:
        return _build_unknown_peripheral_prompt(
            context=context,
            exec_trace=exec_trace,
            io_trace=io_trace,
            source_contexts=source_contexts,
            macro_context=macro_context,
            board_config_summary=board_config_summary,
            platform_name=platform_name,
        )
