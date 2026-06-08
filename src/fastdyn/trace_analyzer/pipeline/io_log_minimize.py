from __future__ import annotations

import re
from pathlib import Path
from typing import Any
from collections import defaultdict

from ..models import IOTraceContext, RunArtifacts, StaticArtifacts
from fastdyn.verifier.context_minimizer import MMIOAnalyzer, MMIOAccess

def build_io_trace_context(
    run_artifacts: RunArtifacts,
    static_artifacts: StaticArtifacts,
    svd_device: Any = None,
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
                
    target_peripheral = addr_to_periph.get(target_address) if target_address is not None else None
    target_register = addr_to_reg.get(target_address) if target_address is not None else None

    # Fallback for core ARM peripherals not in SVD
    if target_peripheral is None and target_address is not None:
        if 0xE0001000 <= target_address <= 0xE0002000:
            target_peripheral = "DWT"
            if target_address == 0xE0001004:
                target_register = "CYCCNT"

    # If target_address was lost due to abrupt exit (e.g. utils_die calling exit(1)), 
    # extract the last target_address from the last line of io.log
    if target_peripheral is None and io_log_path and io_log_path.exists():
        try:
            with open(io_log_path, 'r') as f:
                lines = f.readlines()
                if lines:
                    last_line = lines[-1]
                    import re
                    m = re.search(r'address\s*=\s*(0x[0-9a-fA-F]+)', last_line)
                    if m:
                        target_address = int(m.group(1), 16)
                        target_address_hex = m.group(1)
                        target_peripheral = addr_to_periph.get(target_address)
                        target_register = addr_to_reg.get(target_address)
        except Exception:
            pass

    # Fallback for core ARM peripherals not in SVD (again, in case it was updated from io.log)
    if target_peripheral is None and target_address is not None:
        if 0xE0001000 <= target_address <= 0xE0002000:
            target_peripheral = "DWT"
            if target_address == 0xE0001004:
                target_register = "CYCCNT"
                
    ctx = IOTraceContext(
        io_log_path=io_log_path,
        target_address=target_address,
        target_address_hex=target_address_hex,
        target_peripheral=target_peripheral,
        target_register=target_register,
    )
    
    print(f"DEBUG: target_address={target_address}, target_peripheral={target_peripheral}, io_log_path={io_log_path}")
    if not svd_device or not target_peripheral:
        print(f"DEBUG: Returning early because svd_device={bool(svd_device)} or target_peripheral={target_peripheral}")
        ctx.warnings.append("No SVD device or target peripheral identified.")
        return ctx
        
    analyzer = MMIOAnalyzer(svd_device)
    
    all_accesses = analyzer.load_and_correlate_log(str(io_log_path))
    if not all_accesses:
        return ctx
        
    target_accesses = [a for a in all_accesses if a.peripheral == target_peripheral]
    if not target_accesses:
        return ctx
        
    init_accesses_objs, runtime_accesses_objs = analyzer.separate_init_and_runtime(target_accesses)
    ctx.init_accesses = [str(a) for a in init_accesses_objs]
    
    unique_pcs = set()
    for acc in init_accesses_objs:
        if acc.pc is not None:
            unique_pcs.add(acc.pc)
    ctx.relevant_pcs = sorted(list(unique_pcs))
    
    patterns = analyzer.find_repeating_patterns(runtime_accesses_objs, method="ngram", n=15)
    
    existing_loop_values = defaultdict(set)
    
    if patterns:
        runtime_sequence_simplified = [(a.peripheral, a.register, a.access_type) for a in runtime_accesses_objs]
        for i, (abstract_pattern, count) in enumerate(patterns):
            first_occurrence_index = -1
            for j in range(len(runtime_sequence_simplified) - len(abstract_pattern) + 1):
                if tuple(runtime_sequence_simplified[j : j + len(abstract_pattern)]) == abstract_pattern:
                    first_occurrence_index = j
                    break
            
            # Find minimal repeating sub-pattern to avoid overkill
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
            
            ctx.loop_patterns.append({
                "header": f"# Pattern #{i+1} (found {count} times)",
                "accesses": pattern_str_list
            })
            
    entropy_findings, low_entropy_registers = analyzer.analyze_entropy(target_accesses)
    
    rare_str_list = analyzer.find_rare_value_transitions(target_accesses, low_entropy_registers, existing_loop_values)
    if rare_str_list:
        ctx.rare_transitions = [str(r) for r in rare_str_list]
        
    state_behavior_str_list = analyzer.analyze_stateful_behavior(target_accesses)
    if state_behavior_str_list:
        ctx.state_behavior = state_behavior_str_list
        
    return ctx
