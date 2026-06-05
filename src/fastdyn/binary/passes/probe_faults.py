from typing import Any
import json
from pathlib import Path

from ..binary_utils.artifact_io import write_json_artifact

def run(context: Any) -> None:
    cache_dir = context.cache_dir
    vector_table_path = cache_dir / "vector_table.json"
    
    probe_faults = {}
    faults_list = []
    default_handler = None
    reset_handler = None
    
    if vector_table_path.exists():
        with open(vector_table_path, "r") as f:
            vt = json.load(f)
            
        for entry in vt.get("entries", []):
            name = entry.get("name", "")
            if name in ["HardFault", "MemManage", "BusFault", "UsageFault"]:
                faults_list.append(entry)
            elif name == "Default_Handler":
                default_handler = entry.get("handler")
            elif name == "Reset_Handler":
                reset_handler = entry.get("handler")
                
    probe_faults["faults"] = faults_list
    if default_handler:
        probe_faults["default_handler"] = default_handler
    if reset_handler:
        probe_faults["reset_handler"] = reset_handler

    # Add panic symbols
    panics = []
    symbols_path = cache_dir / "symbols.json"
    if symbols_path.exists():
        with open(symbols_path, "r") as f:
            syms = json.load(f)
        for sym in syms:
            name = sym.get("name", "").lower()
            if any(p in name for p in ["panic", "assert", "syshalt", "error_handler"]):
                panics.append(sym.get("address"))
    probe_faults["panics"] = panics

    # Add valid bounds from segments (or memory map)
    bounds = []
    segments_path = cache_dir / "segments.json"
    if segments_path.exists():
        with open(segments_path, "r") as f:
            segs = json.load(f)
        for seg in segs:
            if seg.get("type") == "PT_LOAD" and (seg.get("flags", 0) & 1):
                vaddr_str = seg.get("virtual_address", "0x0")
                start = int(vaddr_str, 16) if vaddr_str.startswith("0x") else int(vaddr_str)
                memsz = seg.get("memory_size", 0)
                end = start + memsz
                bounds.append({"start": hex(start), "end": hex(end)})
    probe_faults["valid_bounds"] = bounds
                
    write_json_artifact(cache_dir, "probe_faults.json", probe_faults)
