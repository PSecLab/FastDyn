"""Environment interpolation shared by FastDyn TOML readers."""

from __future__ import annotations

import os
import re


_ENV_PATTERN = re.compile(
    r"\$\{(?P<braced>[A-Za-z_][A-Za-z0-9_]*)(?::-(?P<default>[^}]*))?\}"
)


def expand_env_defaults(value: object, env: dict[str, str] | None = None) -> object:
    """Recursively expand ${VAR} and ${VAR:-default} in config values."""

    merged = os.environ if env is None else env
    if isinstance(value, str):
        def replace(match: re.Match[str]) -> str:
            current = merged.get(match.group("braced"))
            if current not in (None, ""):
                return current
            default = match.group("default")
            return default if default is not None else ""

        return _ENV_PATTERN.sub(replace, value)
    if isinstance(value, list):
        return [expand_env_defaults(item, merged) for item in value]
    if isinstance(value, dict):
        return {key: expand_env_defaults(item, merged) for key, item in value.items()}
    return value
