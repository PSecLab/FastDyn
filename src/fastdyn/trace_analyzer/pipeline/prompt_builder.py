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


def build_prompt(
    context: TraceAnalysisContext,
    exec_trace: ExecTraceContext,
    io_trace: IOTraceContext,
    source_contexts: list[SourceContext],
    macro_context: MacroContext,
    modeling_dir: Path | None = None,
    platform_name: str = "Unknown Platform",
) -> str:
    prompt_parts = []
    
    target_periph = io_trace.target_peripheral or "UNKNOWN PERIPHERAL"
    
    # 1. Preamble & System Context
    prompt_parts.append("Take this prompt independent from previous prompt history.\n")
    prompt_parts.append(f"You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for the {platform_name} platform with special familiarity with the {target_periph} peripheral.")
    prompt_parts.append("Your task is to analyze the following summary of MMIO trace data and generate a complete C device model.\n")
    
    # 2. APIs and Rules First (like 001_prompt.txt)
    prompt_parts.append("## Available APIs")
    prompt_parts.append("You **must** use the following APIs to construct the device model. Pay close attention to the read/write callback signatures.")
    prompt_parts.append(qemu_api_list)
    prompt_parts.append("")
    prompt_parts.append("### Missing API Policy (HARD BLOCKER)")
    prompt_parts.append("If the model requires an API that is not listed above, you MUST stop and NOT generate the device model. Instead:")
    prompt_parts.append("1. Clearly name the missing API and explain why it is required for this model.")
    prompt_parts.append("2. Propose a precise signature for the missing API")
    prompt_parts.append("3. STOP here. Do not generate the model.\n")
    
    prompt_parts.append("### Peripheral Misattribution Policy (VETO)")
    prompt_parts.append("Analyze the global execution path and the MMIO trace data. If your analysis indicates that the fault/hang is NOT caused by the target peripheral, but is instead due to a missing interrupt, an unmodeled external device, or a bug in a completely different peripheral:")
    prompt_parts.append("1. Start your response with the exact word `VETO:`")
    prompt_parts.append("2. Explain your reasoning based on the trace analysis (e.g., 'The firmware successfully initialized this peripheral and is currently stuck in UART_Init waiting for an RX interrupt. The target peripheral is fine.')")
    prompt_parts.append("3. STOP here. Do not generate the model.\n")
    
    prompt_parts.append("## Critical Framework Rules")
    prompt_parts.append(framework_rules)
    prompt_parts.append("")
    
    prompt_parts.append("## Observability & Host I/O")
    prompt_parts.append(observability_guidance)
    prompt_parts.append("")
    
    prompt_parts.append("## Inter-Peripheral Communication")
    prompt_parts.append(inter_peripheral_guidance)
    prompt_parts.append("")
    
    # 3. Start of Analysis Data
    prompt_parts.append("--- START OF ANALYSIS DATA ---\n")
    prompt_parts.append("## Platform:")
    prompt_parts.append(f"{platform_name}")
    prompt_parts.append("")
    
    prompt_parts.append("## Peripheral Name:")
    prompt_parts.append(target_periph)
    prompt_parts.append("")
    
    prompt_parts.append("## Target Peripheral Configuration")
    prompt_parts.append("This is the current TOML configuration for the peripheral in the system.")
    prompt_parts.append("If this peripheral is hanging because it is waiting for an attached sensor or external endpoint that is not modeled, you MUST use the VETO mechanism. In your VETO message, suggest the specific type of sensor or endpoint that is missing and provide detailed reasoning for your conclusion.")
    if io_trace.target_peripheral and context.request.config_path.exists():
        import re
        with open(context.request.config_path, "r") as f:
            content = f.read()
        target_lower = io_trace.target_peripheral.lower()
        pattern = re.compile(rf"^\[Device\.{target_lower}\](.*?)(?=^\[Device\.|^\[Memory\.|^\[Machine\.|^\[Rehosting\.|\Z)", re.MULTILINE | re.DOTALL)
        match = pattern.search(content)
        if match:
            prompt_parts.append("```toml")
            prompt_parts.append(f"[Device.{target_lower}]")
            prompt_parts.append(match.group(1).strip())
            prompt_parts.append("```\n")
        else:
            prompt_parts.append("*Configuration not found.*\n")
    else:
        prompt_parts.append("*Configuration not found.*\n")
    
    prompt_parts.append("## Exit Reason & Target Register:")
    prompt_parts.append(f"**Exit Reason:** {context.run_artifacts.probe_result.get('exit_reason', 'UNKNOWN')}")
    if io_trace.target_address:
        prompt_parts.append(f"**Faulting Address:** {io_trace.target_address_hex} ({io_trace.target_register})")
    prompt_parts.append(f"**Final Function:** {exec_trace.final_function or exec_trace.final_pc}")
    prompt_parts.append("")
    
    # 4. IO Trace Context (Init, Loops, Rare)
    prompt_parts.append("## Initialization Sequence (`init.txt`):")
    prompt_parts.append("This file contains all accesses that occur before the main runtime loop begins.")
    if io_trace.init_accesses:
        prompt_parts.append("```")
        for line in io_trace.init_accesses:
            prompt_parts.append(line.strip())
        prompt_parts.append("```\n")
    else:
        prompt_parts.append("No initialization sequence detected.\n")

    prompt_parts.append("## Detected Runtime Loops (`loop_pattern_*.txt`):")
    prompt_parts.append("These files contain the most common repeating sequences of operations during runtime.")
    if io_trace.loop_patterns:
        prompt_parts.append("```")
        for lp in io_trace.loop_patterns:
            prompt_parts.append(f"--- {lp['header']} ---")
            for acc in lp['accesses']:
                prompt_parts.append(acc.strip())
            prompt_parts.append("")
        prompt_parts.append("```\n")
    else:
        prompt_parts.append("No runtime loops detected.\n")

    prompt_parts.append("## Rare Value Transitions (`rare_transitions.txt`):")
    prompt_parts.append("These are read values from LOW-entropy (status/control) registers that did NOT appear in the dominant loop patterns above. They represent infrequent but important state changes that the model MUST handle correctly.")
    if io_trace.rare_transitions:
        prompt_parts.append("```")
        for line in io_trace.rare_transitions:
            prompt_parts.append(line.strip())
        prompt_parts.append("```\n")
    else:
        prompt_parts.append("No rare value transitions detected.\n")

    prompt_parts.append("## Stateful Behavior Analysis (`state.txt`):")
    prompt_parts.append("This file identifies programming patterns like Read-Modify-Write (RMW), which indicate stateful registers.")
    if io_trace.state_behavior:
        prompt_parts.append("```")
        for line in io_trace.state_behavior:
            prompt_parts.append(line.strip())
        prompt_parts.append("```\n")
    else:
        prompt_parts.append("No stateful behavior detected.\n")

    # 5. Exec Trace Sequence
    prompt_parts.append("## Execution Trace Summary")
    prompt_parts.append("### Global Execution Path (Compressed)")
    compressed_steps = [item["name"] for item in exec_trace.collapsed_sequence]
    prompt_parts.append(" -> ".join(compressed_steps))
    prompt_parts.append("")

    # 6. Source Context
    prompt_parts.append("## Source Code Context\n")
    if not source_contexts:
        prompt_parts.append("*Source code unavailable.*")
    else:
        for sc in source_contexts:
            if sc.text:
                prompt_parts.append(f"Source snippet from `{sc.source_root_relative}` (lines {sc.start_line}-{sc.end_line}):")
                prompt_parts.append("```c")
                prompt_parts.append(sc.text)
                prompt_parts.append("```\n")

    # 7. Macros
    prompt_parts.append("\n## Macro Definitions\n")
    prompt_parts.append("> **Note:** These macros are EXACTLY as evaluated for this specific compile-unit. They include active #ifdef branches and numeric values.")
    
    prov = "DWARF" if macro_context.provider == "dwarf_debug_macro" else str(macro_context.provider)
    prompt_parts.append(f"**Provenance:** {prov} (Context Artifact: `{macro_context.context_artifact}`)")
    
    if macro_context.selected_macros:
        prompt_parts.append("```c")
        for name in macro_context.selected_macro_names:
            raw = macro_context.selected_macros[name]
            prompt_parts.append(f"{raw}")
        prompt_parts.append("```")
    else:
        prompt_parts.append("*No specific macros identified.*")

    # 8. Model Code
    if modeling_dir and io_trace.target_peripheral:
        model_path = modeling_dir / f"{io_trace.target_peripheral.lower()}.c"
        if model_path.exists():
            with open(model_path, "r") as f:
                model_code = f.read()
            prompt_parts.append("\n## Current Device Model Source\n")
            prompt_parts.append("The current device model source code is provided below. You MUST base your analysis on this code.\n")
            prompt_parts.append("```c")
            prompt_parts.append(model_code.strip())
            prompt_parts.append("```")
            
    # 9. Task
    prompt_parts.append("\n---\n")
    prompt_parts.append(search_replace_prompting)
    prompt_parts.append("\n")
    prompt_parts.append("Please analyze the failure and provide the corrected device model using the SEARCH/REPLACE block format.")

    return "\n".join(prompt_parts)
