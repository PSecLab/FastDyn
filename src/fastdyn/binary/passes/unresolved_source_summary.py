from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.unresolved_sources import summarize_unresolved_entries


def run(context) -> None:
    summary = {}

    source_map_entries = context.shared.get("source_map", [])
    if source_map_entries:
        summary["source_map_unresolved"] = summarize_unresolved_entries(source_map_entries)

    compile_units = context.shared.get("compile_units", [])
    if compile_units:
        summary["compile_units_unresolved"] = summarize_unresolved_entries(compile_units)

    write_json_artifact(context.cache_dir, "unresolved_source_summary.json", summary)
