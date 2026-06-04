from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.strings import extract_strings


def run(context) -> None:
    strings_info = extract_strings(context.config.binary_path)
    context.shared["strings"] = strings_info
    write_json_artifact(context.cache_dir, "strings.json", strings_info)
