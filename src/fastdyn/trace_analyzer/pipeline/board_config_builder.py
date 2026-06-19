"""
board_config_builder.py
-----------------------
Extracts a concise, token-efficient hardware configuration summary from the
FastDyn Machine object for inclusion in LLM prompts.

Reads exclusively from the already-parsed Machine/Device objects — no TOML
re-parsing is performed here.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any


def build_board_config_summary(machine: Any) -> str:
    """
    Produces a markdown-formatted string summarising the board hardware
    configuration from the FastDyn `machine` object.

    Reads only from machine.devices (Device objects), which are fully
    populated by toml_parser.py:
      - device.supported_ranges  -> MMIO ranges
      - device.description       -> human-readable name
      - device.handlers          -> loaded .so models (with scroll/enabled)
      - device.connections       -> raw connection dicts (type, cs_id, address, baud, enabled…)
      - device.slaves            -> parsed slave objects (device_id, model, params)

    Returns a ready-to-embed markdown string.
    """
    lines: list[str] = []
    lines.append("## Board Hardware Configuration")
    lines.append(
        "> This summary was automatically extracted from the parsed machine "
        "configuration. Use it to identify which peripheral models are already "
        "loaded, their MMIO address ranges, and any slaves/devices wired to buses."
    )
    lines.append("")

    machine_devices: dict[str, Any] = getattr(machine, "devices", {}) or {}

    if not machine_devices:
        lines.append("*No devices configured on this machine.*\n")
        return "\n".join(lines)

    lines.append("### Loaded Peripheral Models")
    lines.append("")
    lines.append("| Peripheral | MMIO Range(s) | Description | Slaves / Connections |")
    lines.append("|------------|--------------|-------------|----------------------|")

    for dev_name, dev_obj in sorted(machine_devices.items()):
        # ---- MMIO ranges from device.supported_ranges ----
        ranges_raw: list = getattr(dev_obj, "supported_ranges", []) or []
        if ranges_raw:
            range_strs = [f"`{r[0]:#010x}–{r[1]:#010x}`" for r in ranges_raw]
            ranges_col = ", ".join(range_strs)
        else:
            ranges_col = "*unknown*"

        # ---- Description ----
        description: str = getattr(dev_obj, "description", "") or ""

        # ---- Build slave/connection column ----
        slave_parts: list[str] = []

        # a) Slaves (SPI/I2C slave devices registered via `slaves` TOML table)
        dev_slaves: list = getattr(dev_obj, "slaves", None) or []
        for sl in dev_slaves:
            sl_device = getattr(sl, "device", "?")
            sl_models = getattr(sl, "model", []) or []
            sl_params: dict = getattr(sl, "params", {}) or {}
            sl_scroll = getattr(sl, "device_scroll", None)

            model_str = ", ".join(sl_models) if sl_models else "?"
            detail_parts = [f"model={model_str}"]
            for k, v in sl_params.items():
                detail_parts.append(f"{k}={v}")
            if sl_scroll:
                detail_parts.append(f"scroll={Path(sl_scroll).name}")

            slave_parts.append(f"slave `{sl_device}` ({'; '.join(detail_parts)})")

        # b) Connections (bus endpoints — parsed from `connections` TOML table)
        dev_conns: list = getattr(dev_obj, "connections", None) or []
        for conn in dev_conns:
            if not isinstance(conn, dict):
                continue
            conn_type = conn.get("type", "unknown")
            enabled = conn.get("enabled", True)
            topic = conn.get("device_or_topic", "")
            cs_id = conn.get("cs_id", None)
            address = conn.get("address", None)
            baud = conn.get("baud", None)
            name = conn.get("name", "")

            enabled_str = "" if enabled else " *(disabled)*"
            detail_parts = [f"type={conn_type}{enabled_str}"]
            if name:
                detail_parts.append(f"name={name}")
            if topic:
                detail_parts.append(f"topic/device={topic}")
            if cs_id is not None:
                detail_parts.append(f"cs_id={cs_id}")
            if address is not None:
                detail_parts.append(f"address={address}")
            if baud is not None:
                detail_parts.append(f"baud={baud}")

            slave_parts.append(f"conn ({'; '.join(detail_parts)})")

        slaves_col = "<br>".join(slave_parts) if slave_parts else "—"

        lines.append(f"| `{dev_name}` | {ranges_col} | {description} | {slaves_col} |")

    lines.append("")
    return "\n".join(lines)
