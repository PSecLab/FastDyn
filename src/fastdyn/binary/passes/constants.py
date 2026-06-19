from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.constants import (
    build_peripheral_hint_summary,
    extract_constants,
    extract_literal_pools,
    extract_mmio_constants,
)


def run(context) -> None:
    functions = context.shared.get("functions", [])
    constants = extract_constants(
        context.config.binary_path,
        functions,
    )
    literal_pools = extract_literal_pools(
        context.config.binary_path,
        functions,
    )

    register_index = context.shared.get("svd_register_index", {})
    peripheral_ranges = context.shared.get("svd_peripheral_ranges", [])
    mmio_constants = extract_mmio_constants(
        constants,
        literal_pools,
        register_index,
        peripheral_ranges,
    ) if register_index or peripheral_ranges else []

    peripheral_hint_summary = build_peripheral_hint_summary(mmio_constants)

    context.shared["constants"] = constants
    context.shared["literal_pools"] = literal_pools
    context.shared["mmio_constants"] = mmio_constants
    context.shared["peripheral_hint_summary"] = peripheral_hint_summary

    write_json_artifact(context.cache_dir, "constants.json", constants)
    write_json_artifact(context.cache_dir, "literal_pools.json", literal_pools)
    write_json_artifact(context.cache_dir, "mmio_constants.json", mmio_constants)
    write_json_artifact(context.cache_dir, "peripheral_hint_summary.json", peripheral_hint_summary)
