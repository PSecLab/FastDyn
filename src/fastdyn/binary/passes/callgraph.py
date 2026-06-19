from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.callgraph import build_callgraph, build_reset_path


def run(context) -> None:
    functions = context.shared.get("functions", [])
    symbol_index = context.shared.get("symbol_name_by_address", {})

    edges = build_callgraph(context.config.binary_path, functions, symbol_index)
    write_json_artifact(context.cache_dir, "callgraph.json", edges)

    vector_table = context.shared.get("vector_table", {})
    reset_vector = vector_table.get("reset_vector")
    if reset_vector is None:
        reset_path = {
            "method": "static-direct-call-follow",
            "reset_handler": None,
            "path": [],
            "limitations": ["Could not determine reset handler address"],
        }
    else:
        reset_path = build_reset_path(int(reset_vector, 16), edges)

    context.shared["callgraph"] = edges
    context.shared["reset_path_candidates"] = reset_path
    write_json_artifact(context.cache_dir, "reset_path_candidates.json", reset_path)
