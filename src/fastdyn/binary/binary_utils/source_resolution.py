from __future__ import annotations

import os


def resolve_source_path(file_path: str, comp_dir: str | None, source_roots: list[str]) -> dict:
    result = {
        "original_file": file_path,
        "original_comp_dir": comp_dir,
        "resolved_path": None,
        "source_root": None,
        "source_root_relative": None,
        "exists_locally": False,
        "resolution_failure": None,
    }

    if not source_roots:
        result["resolution_failure"] = "no source_root provided"
        return result

    for source_root in source_roots:
        source_root_abs = os.path.abspath(source_root)
        candidates = []

        if os.path.isabs(file_path):
            candidates.append(file_path)

        if comp_dir:
            candidates.append(os.path.normpath(os.path.join(comp_dir, file_path)))

        parts = file_path.split("/")
        for i, part in enumerate(parts):
            if part in ("ardupilot", "ArduCopter", "libraries", "modules", "Rover", "Plane", "AP_HAL_ChibiOS"):
                suffix = "/".join(parts[i:])
                candidates.append(os.path.normpath(os.path.join(source_root_abs, suffix)))
                if part != "ardupilot":
                    candidates.append(os.path.normpath(os.path.join(source_root_abs, "..", suffix)))

        for candidate in candidates:
            if os.path.exists(candidate):
                result["resolved_path"] = candidate
                result["source_root"] = source_root_abs
                result["exists_locally"] = True
                try:
                    relative_path = os.path.relpath(candidate, source_root_abs)
                    if not relative_path.startswith(".."):
                        result["source_root_relative"] = relative_path
                except ValueError:
                    pass
                return result

    result["resolution_failure"] = "no matching file under any source_root"
    return result
