from __future__ import annotations

import gzip
import json
import re
from pathlib import Path
from typing import Any

from ..models import MacroContext, SourceContext, StaticArtifacts

ALWAYS_INCLUDE_MACROS = {
    "TRUE", "FALSE", "NULL", "STM32_NO_INIT", "HAL_USE_RTC",
    "RCC_APB1ENR_RTCAPBEN", "RCC_APB1ENR_PWREN", "STM32_HSECLK",
    "STM32_HSE_ENABLED", "STM32_PLLM_VALUE", "STM32_PLLN_VALUE", "STM32_SYSCLK"
}

def build_macro_context(
    source_context: SourceContext,
    static_artifacts: StaticArtifacts,
) -> MacroContext:
    if not source_context.source_root_relative:
        return MacroContext(warnings=["No source_root_relative in source_context"])

    index_data = static_artifacts.macros_index
    units = index_data.get("units", [])
    
    # map source file to macro unit via macros/index.json
    target_unit = None
    for unit in units:
        if unit.get("source_root_relative") == source_context.source_root_relative:
            target_unit = unit
            break
            
    if not target_unit:
        return MacroContext(
            source_root_relative=source_context.source_root_relative,
            warnings=["No macro unit found for source"]
        )
        
    context_artifact_rel = target_unit.get("context_artifact")
    if not context_artifact_rel:
        return MacroContext(
            source_root_relative=source_context.source_root_relative,
            warnings=["No context_artifact in macro unit"]
        )

    context_artifact_path = static_artifacts.cache_dir / context_artifact_rel
    if not context_artifact_path.exists():
        return MacroContext(
            source_root_relative=source_context.source_root_relative,
            warnings=[f"Macro context artifact does not exist: {context_artifact_path}"]
        )

    # load context_artifact gzip
    try:
        with gzip.open(context_artifact_path, 'rt') as f:
            context_data = json.load(f)
    except Exception as e:
        return MacroContext(
            source_root_relative=source_context.source_root_relative,
            warnings=[f"Failed to load macro context artifact: {e}"]
        )

    definitions = context_data.get("definitions", [])
    def_map = {d["name"]: d for d in definitions if "name" in d}

    # collect identifiers from selected source snippet/function
    identifiers = set(re.findall(r'\b[a-zA-Z_]\w*\b', source_context.text))
    
    # always include small always-useful build metadata and TRUE/FALSE
    identifiers.update(ALWAYS_INCLUDE_MACROS)

    selected_macros = {}
    selected_macro_names = []
    
    # recursively include macros referenced by selected macro values
    queue = list(identifiers)
    visited = set()
    
    while queue:
        ident = queue.pop(0)
        if ident in visited:
            continue
        visited.add(ident)
        
        if ident in def_map:
            d = def_map[ident]
            raw = d.get("raw", "")
            val = d.get("value", "")
            
            selected_macros[ident] = raw
            selected_macro_names.append(ident)
            
            # Extract new identifiers from the value
            if val:
                new_idents = set(re.findall(r'\b[a-zA-Z_]\w*\b', val))
                queue.extend(new_idents - visited)

    return MacroContext(
        source_root_relative=source_context.source_root_relative,
        unit_id=target_unit.get("unit_id"),
        provider=target_unit.get("provider"),
        confidence=target_unit.get("confidence"),
        context_artifact=context_artifact_rel,
        context_hash=target_unit.get("context_hash"),
        macro_count=len(definitions),
        selected_macros=selected_macros,
        selected_macro_names=sorted(selected_macro_names),
        selection={"count": len(selected_macros)}
    )
