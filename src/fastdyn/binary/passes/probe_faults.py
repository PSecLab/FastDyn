from typing import Any
import json
from pathlib import Path

from ..binary_utils.artifact_io import write_json_artifact

def run(context: Any) -> None:
    cache_dir = context.cache_dir
    vector_table_path = cache_dir / "vector_table.json"
    
    probe_faults = []
    if vector_table_path.exists():
        with open(vector_table_path, "r") as f:
            vt = json.load(f)
            
        for entry in vt.get("entries", []):
            name = entry.get("name", "")
            if name in ["HardFault", "MemManage", "BusFault", "UsageFault", "Default_Handler"]:
                probe_faults.append(entry)
                
    write_json_artifact(cache_dir, "probe_faults.json", probe_faults)
