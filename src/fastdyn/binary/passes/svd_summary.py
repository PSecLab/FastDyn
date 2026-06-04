from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.svd import build_svd_indices
from fastdyn.utils import parse_config as parse_helper


def run(context) -> None:
    svd_device = parse_helper.get_svd_device(context.config.svd_path)
    context.shared["svd_device"] = svd_device

    svd_indices = build_svd_indices(svd_device)
    context.shared["svd_irq_names"] = svd_indices["irq_names"]
    context.shared["svd_register_index"] = svd_indices["register_index"]
    context.shared["svd_peripheral_ranges"] = svd_indices["peripheral_ranges"]

    summary = {
        "svd_file": context.config.svd_path,
        "device_name": getattr(svd_device, "name", None),
        "peripheral_count": len(svd_device.peripherals),
        "interrupt_count": len(svd_indices["irq_names"]),
        "peripheral_address_ranges": [
            {
                "name": entry["name"],
                "start": hex(entry["start"]),
                "end": hex(entry["end"]) if entry["end"] is not None else None,
            }
            for entry in svd_indices["peripheral_ranges"]
        ],
    }

    write_json_artifact(context.cache_dir, "svd_map.json", svd_indices["artifact"])
    write_json_artifact(context.cache_dir, "svd_summary.json", summary)
