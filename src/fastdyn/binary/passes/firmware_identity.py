from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.identity import extract_firmware_identity


def run(context) -> None:
    firmware_identity = extract_firmware_identity(context.config.binary_path)
    context.shared["firmware_identity"] = firmware_identity
    write_json_artifact(context.cache_dir, "firmware_identity.json", firmware_identity)
