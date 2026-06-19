from __future__ import annotations

import re
from typing import Any
from collections import defaultdict, deque

from ..models import IOTraceContext, RunArtifacts, StaticArtifacts
from fastdyn.verifier.context_minimizer import MMIOAnalyzer

def build_io_trace_context(
    run_artifacts: RunArtifacts,
    static_artifacts: StaticArtifacts,
    svd_device: Any = None,
    target_peripherals_override: list[str] = None,
    reference_functions_override: list = None,
) -> IOTraceContext:
    probe_result = run_artifacts.probe_result
    target_address_raw = probe_result.get("extra_info")
    target_address = None
    target_address_hex = None
    
    if isinstance(target_address_raw, str) and target_address_raw.startswith("0x"):
        try:
            target_address = int(target_address_raw, 16)
            target_address_hex = target_address_raw
        except ValueError:
            pass
    elif isinstance(target_address_raw, int):
        target_address = target_address_raw
        target_address_hex = hex(target_address_raw)

    io_log_path = run_artifacts.io_log_path
    if not io_log_path or not io_log_path.exists():
        return IOTraceContext(
            target_address=target_address,
            target_address_hex=target_address_hex,
            warnings=["No io.log available"]
        )
        
    addr_to_periph = {}
    addr_to_reg = {}
    for p in static_artifacts.svd_map.get("peripherals", []):
        p_name = p.get("name")
        for r in p.get("registers", []):
            r_addr_raw = r.get("address")
            if r_addr_raw is not None:
                r_addr = int(r_addr_raw, 16) if isinstance(r_addr_raw, str) else r_addr_raw
                addr_to_reg[r_addr] = r.get("name")
                addr_to_periph[r_addr] = p_name
                
    primary_target_peripheral = addr_to_periph.get(target_address) if target_address is not None else None
    primary_target_register = addr_to_reg.get(target_address) if target_address is not None else None

    # Fallback for core ARM peripherals not in SVD
    if primary_target_peripheral is None and target_address is not None:
        if 0xE0001000 <= target_address <= 0xE0002000:
            primary_target_peripheral = "DWT"
            if target_address == 0xE0001004:
                primary_target_register = "CYCCNT"

    if primary_target_peripheral is None and not target_peripherals_override and io_log_path and io_log_path.exists():
        try:
            with open(io_log_path, 'r', encoding='utf-8', errors='replace') as f:
                tail = deque(f, maxlen=1)
            if tail:
                m = re.search(r'address\s*=\s*(0x[0-9a-fA-F]+)', tail[0])
                if m:
                    target_address = int(m.group(1), 16)
                    target_address_hex = m.group(1)
                    primary_target_peripheral = addr_to_periph.get(target_address)
                    primary_target_register = addr_to_reg.get(target_address)
        except OSError as e:
            return IOTraceContext(
                io_log_path=io_log_path,
                target_address=target_address,
                target_address_hex=target_address_hex,
                target_peripheral=primary_target_peripheral,
                target_register=primary_target_register,
                warnings=[f"Unable to read io.log tail: {e}"],
            )

    if not svd_device:
        ctx = IOTraceContext(
            io_log_path=io_log_path,
            target_address=target_address,
            target_address_hex=target_address_hex,
            target_peripheral=target_peripherals_override[0] if target_peripherals_override else primary_target_peripheral,
            target_register=primary_target_register,
        )
        ctx.warnings.append("No SVD device identified.")
        return ctx

    analyzer = MMIOAnalyzer(svd_device)
    all_accesses = analyzer.load_and_correlate_log(str(io_log_path)) if io_log_path and io_log_path.exists() else []

    # Determine all peripherals to analyze
    peripherals_to_analyze = set()
    if target_peripherals_override:
        for p in target_peripherals_override:
            peripherals_to_analyze.add(p)
    elif primary_target_peripheral:
        peripherals_to_analyze.add(primary_target_peripheral)

    ctx = IOTraceContext(
        io_log_path=io_log_path,
        target_address=target_address,
        target_address_hex=target_address_hex,
        target_peripheral=target_peripherals_override[0] if target_peripherals_override else primary_target_peripheral,
        target_register=primary_target_register,
    )
    
    if not peripherals_to_analyze:
        ctx.warnings.append("No target peripherals identified.")
        return ctx
        
    if not all_accesses:
        return ctx

    from ..models import PeripheralIOTrace
    
    for current_peripheral in sorted(peripherals_to_analyze):
        target_accesses = [a for a in all_accesses if a.peripheral == current_peripheral]
        if not target_accesses:
            # Create empty trace for this peripheral so the prompt knows it had 0 accesses
            ctx.peripherals_data.append(PeripheralIOTrace(peripheral=current_peripheral))
            continue
            
        init_accesses_objs, runtime_accesses_objs = analyzer.separate_init_and_runtime(target_accesses)
        init_accesses = [str(a) for a in init_accesses_objs]
        
        unique_pcs = set()
        for acc in init_accesses_objs:
            if acc.pc is not None:
                unique_pcs.add(acc.pc)
        relevant_pcs = sorted(list(unique_pcs))
        
        patterns = analyzer.find_repeating_patterns(runtime_accesses_objs, method="ngram", n=15, top_k=5)
        existing_loop_values = defaultdict(set)
        loop_patterns = []
        
        if patterns:
            runtime_sequence_simplified = [(a.peripheral, a.register, a.access_type) for a in runtime_accesses_objs]
            for i, (abstract_pattern, count) in enumerate(patterns):
                first_occurrence_index = -1
                for j in range(len(runtime_sequence_simplified) - len(abstract_pattern) + 1):
                    if tuple(runtime_sequence_simplified[j : j + len(abstract_pattern)]) == abstract_pattern:
                        first_occurrence_index = j
                        break
                
                min_len = len(abstract_pattern)
                for p in range(1, len(abstract_pattern) // 2 + 1):
                    if abstract_pattern[:p] * (len(abstract_pattern) // p) + abstract_pattern[:len(abstract_pattern) % p] == abstract_pattern:
                        min_len = p
                        break
                
                pattern_str_list = []
                if first_occurrence_index != -1:
                    for k in range(min_len):
                        acc = runtime_accesses_objs[first_occurrence_index + k]
                        pattern_str_list.append(str(acc))
                        if acc.access_type == "read":
                            existing_loop_values[(acc.peripheral, acc.register)].add(acc.value)
                
                loop_patterns.append({
                    "header": f"# Pattern #{i+1} (found {count} times)",
                    "accesses": pattern_str_list
                })
                
        entropy_findings, low_entropy_registers = analyzer.analyze_entropy(target_accesses)
        rare_str_list = analyzer.find_rare_value_transitions(target_accesses, low_entropy_registers, existing_loop_values)
        rare_transitions = [str(r) for r in rare_str_list] if rare_str_list else []
            
        state_behavior_str_list = analyzer.analyze_stateful_behavior(target_accesses)
        state_behavior = state_behavior_str_list if state_behavior_str_list else []
        
        ctx.peripherals_data.append(PeripheralIOTrace(
            peripheral=current_peripheral,
            relevant_pcs=relevant_pcs,
            init_accesses=init_accesses,
            loop_patterns=loop_patterns,
            rare_transitions=rare_transitions,
            state_behavior=state_behavior,
        ))
        
    return ctx
