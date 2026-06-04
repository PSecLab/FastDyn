from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def write_json_artifact(cache_dir: Path, filename: str, payload: Any) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    artifact_path = cache_dir / filename
    with artifact_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")
    return artifact_path
