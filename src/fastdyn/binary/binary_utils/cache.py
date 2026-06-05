from __future__ import annotations

import hashlib
import json
from dataclasses import asdict
from pathlib import Path
from typing import Any


def file_sha256(path: str | None) -> str | None:
    if path in (None, ""):
        return None

    file_path = Path(path).expanduser()
    if not file_path.exists() or not file_path.is_file():
        return None

    digest = hashlib.sha256()
    with file_path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)

    return digest.hexdigest()


def build_cache_inputs(config, required_artifacts: list[str], pipeline_version: int) -> dict[str, Any]:
    config_data = asdict(config)
    config_data.pop("cache_dir", None)
    config_data.pop("force", None)

    return {
        "pipeline_version": pipeline_version,
        "config": config_data,
        "binary_sha256": file_sha256(config.binary_path),
        "svd_sha256": file_sha256(config.svd_path),
        "required_artifacts": sorted(required_artifacts),
    }


def build_cache_key(cache_inputs: dict[str, Any]) -> str:
    payload = json.dumps(cache_inputs, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def read_manifest(cache_dir: Path) -> dict[str, Any] | None:
    manifest_path = cache_dir / "manifest.json"
    if not manifest_path.exists():
        return None

    try:
        with manifest_path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


def cache_is_valid(cache_dir: Path, cache_key: str, required_artifacts: list[str]) -> tuple[bool, str]:
    manifest = read_manifest(cache_dir)
    if manifest is None:
        return False, "missing or unreadable manifest"

    if manifest.get("cache_key") != cache_key:
        return False, "cache key mismatch"

    for artifact in required_artifacts:
        if not (cache_dir / artifact).is_file():
            return False, f"missing artifact: {artifact}"

    for pass_name, pass_result in manifest.get("passes", {}).items():
        if pass_result.get("status") != "ok":
            return False, f"previous pass failed: {pass_name}"

    return True, "valid"
