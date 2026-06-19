from __future__ import annotations

import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import tomllib
except ImportError:
    import tomli as tomllib


DEFAULT_MODELING_DIR = Path("boardrunner/boardrunner_sdk/model")


@dataclass
class RoutingMaterializationPlan:
    config_path: Path
    modeling_dir: Path
    actions: list[dict[str, Any]] = field(default_factory=list)

    @property
    def requires_changes(self) -> bool:
        return bool(self.actions)

    def describe(self) -> str:
        if not self.actions:
            return "No routing materialization changes are required."
        lines = ["Routing materialization is required:"]
        for idx, action in enumerate(self.actions, start=1):
            lines.append(f"  {idx}. {action['description']}")
        return "\n".join(lines)

    def to_dict(self) -> dict[str, Any]:
        return {
            "config_path": str(self.config_path),
            "modeling_dir": str(self.modeling_dir),
            "actions": self.actions,
            "requires_changes": self.requires_changes,
        }


def plan_routing_materialization(
    routing: dict[str, Any],
    config_path: Path,
) -> RoutingMaterializationPlan:
    config_path = config_path.expanduser().resolve()
    config_data = _load_toml(config_path)
    modeling_dir_raw = _modeling_dir_from_config(config_data)
    modeling_dir = _resolve_repo_path(modeling_dir_raw)
    plan = RoutingMaterializationPlan(
        config_path=config_path,
        modeling_dir=modeling_dir,
    )

    for model in routing.get("create_new_models", []) or []:
        if not isinstance(model, dict):
            raise ValueError("create_new_models entries must be objects.")

        category = str(model.get("category", "") or "").strip().lower()
        name = _safe_filename(str(model.get("name", "") or "").strip())
        if not name:
            raise ValueError("create_new_models entry is missing name.")

        if category not in {"peripheral", "slave", "endpoint"}:
            raise ValueError(f"Unsupported create_new_models category for {name}: {category}")

        filename = _filename_for_category(name, category)
        device_name = _device_name_from_filename(filename)

        if _category_has_file(filename, category):
            model_path = modeling_dir / filename
            if not model_path.exists():
                action: dict[str, Any] = {
                    "kind": "create_file",
                    "category": category,
                    "name": name,
                    "filename": filename,
                    "path": str(model_path),
                    "description": f"create {category} stub {model_path}",
                }
                if category == "slave":
                    action["bus_type"] = model.get("bus_type")
                plan.actions.append(action)

        if category == "peripheral":
            range_start, range_end = _parse_mmio_range(model.get("mmio_range"))
            if not _device_exists(config_data, device_name):
                scroll = _scroll_path_for(modeling_dir_raw, device_name)
                plan.actions.append(
                    {
                        "kind": "append_peripheral",
                        "category": category,
                        "device": device_name,
                        "range_start": range_start,
                        "range_end": range_end,
                        "scroll": scroll,
                        "description": (
                            f"append [Device.{device_name}] to {config_path.name}"
                        ),
                    }
                )
        else:
            parent = str(model.get("attach_to_peripheral", "") or "").strip().lower()
            if not parent:
                raise ValueError(
                    f"{category} {name} requires attach_to_peripheral in routing.json"
                )
            if not _device_exists(config_data, parent):
                raise ValueError(
                    f"{category} {name} attaches to unknown [Device.{parent}]"
                )
            if not _connection_exists(config_data, parent, category, name):
                action: dict[str, Any] = {
                    "kind": "append_connection",
                    "category": category,
                    "parent": parent,
                    "name": name,
                    "filename": filename,
                    "connection_id": model.get("connection_id"),
                    "description": (
                        f"append [[Device.{parent}.connections]] for {category} {name}"
                    ),
                }
                if category == "slave":
                    action["bus_type"] = model.get("bus_type")
                    action["device_scroll"] = _scroll_path_for(modeling_dir_raw, device_name)
                plan.actions.append(action)

    return plan


def apply_routing_materialization(
    routing: dict[str, Any],
    config_path: Path,
) -> dict[str, Any]:
    plan = plan_routing_materialization(routing, config_path)
    result = {
        "applied": [],
        "skipped": [],
        "initial_plan": plan.to_dict(),
    }

    if not plan.requires_changes:
        result["remaining_plan"] = plan.to_dict()
        return result

    if not sys.stdin.isatty():
        raise RuntimeError("--apply-routing requires an interactive terminal.")

    for action in plan.actions:
        if not _confirm(action["description"]):
            result["skipped"].append(action)
            continue
        _apply_action(action, plan)
        result["applied"].append(action)

    remaining = plan_routing_materialization(routing, config_path)
    result["remaining_plan"] = remaining.to_dict()
    return result


def _apply_action(action: dict[str, Any], plan: RoutingMaterializationPlan) -> None:
    kind = action["kind"]
    if kind == "create_file":
        _create_stub_file(Path(action["path"]), action["category"], action.get("bus_type"))
    elif kind == "append_peripheral":
        _append_peripheral(plan.config_path, action)
    elif kind == "append_connection":
        _append_connection(plan.config_path, action)
    else:
        raise ValueError(f"Unsupported routing materialization action: {kind}")


def _confirm(description: str) -> bool:
    answer = input(f"Apply routing action: {description}? [y/N]: ").strip().lower()
    return answer in {"y", "yes"}


def _load_toml(path: Path) -> dict[str, Any]:
    with open(path, "rb") as f:
        return tomllib.load(f)


def _modeling_dir_from_config(config_data: dict[str, Any]) -> Path:
    dirs = (config_data.get("Rehosting", {}) or {}).get("directories", {}) or {}
    raw = dirs.get("modeling_dir") or str(DEFAULT_MODELING_DIR)
    return Path(str(raw)).expanduser()


def _resolve_repo_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return (Path.cwd() / path).resolve()


def _safe_filename(name: str) -> str:
    if not name:
        return ""
    path = Path(name)
    if path.is_absolute() or path.name != name or ".." in path.parts:
        raise ValueError(f"Unsafe model filename in routing.json: {name}")
    return name


def _filename_for_category(name: str, category: str) -> str:
    if category == "endpoint":
        return name
    if name.endswith(".c"):
        return name
    return f"{name}.c"


def _category_has_file(filename: str, category: str) -> bool:
    if category == "endpoint":
        return filename.endswith(".py")
    return True


def _device_name_from_filename(filename: str) -> str:
    return Path(filename).stem.lower()


def _device_exists(config_data: dict[str, Any], device_name: str) -> bool:
    devices = config_data.get("Device", {}) or {}
    return device_name in devices


def _connection_exists(
    config_data: dict[str, Any],
    parent: str,
    category: str,
    name: str,
) -> bool:
    devices = config_data.get("Device", {}) or {}
    parent_cfg = devices.get(parent, {}) or {}
    for conn in parent_cfg.get("connections", []) or []:
        if not isinstance(conn, dict):
            continue
        conn_type = str(conn.get("type", "") or "").strip().lower()
        conn_name = str(
            conn.get("name") or conn.get("device_or_topic") or ""
        ).strip()
        if conn_type == category and conn_name == name:
            return True
    return False


def _parse_mmio_range(value: Any) -> tuple[str, str]:
    if not value:
        raise ValueError("peripheral create_new_models entries require mmio_range.")
    text = str(value).strip()
    match = re.fullmatch(r"\s*(0x[0-9a-fA-F]+|\d+)\s*-\s*(0x[0-9a-fA-F]+|\d+)\s*", text)
    if not match:
        raise ValueError(f"Invalid mmio_range in routing.json: {value}")
    return match.group(1), match.group(2)


def _scroll_path_for(modeling_dir: Path, device_name: str) -> str:
    build_dir = modeling_dir.parent / "build"
    return str(build_dir / f"{device_name}.so")


def _toml_string(value: Any) -> str:
    return json.dumps(str(value))


def _append_peripheral(config_path: Path, action: dict[str, Any]) -> None:
    device = action["device"]
    with open(config_path, "a", encoding="utf-8") as f:
        f.write(f"\n[Device.{device}]\n")
        f.write(
            "    ranges = "
            f"[[{_toml_string(action['range_start'])}, {_toml_string(action['range_end'])}]]\n"
        )
        f.write("    irq = [[1, 150]]\n")
        f.write(f"    description = {_toml_string(device.upper())}\n")
        f.write("    read_priority = \"elder\"\n")
        f.write(f"    [[Device.{device}.handlers]]\n")
        f.write("        model   = \"elder\"\n")
        f.write("        enabled = true\n")
        f.write(f"        scroll = {_toml_string(action['scroll'])}\n")


def _append_connection(config_path: Path, action: dict[str, Any]) -> None:
    parent = action["parent"]
    category = action["category"]
    with open(config_path, "a", encoding="utf-8") as f:
        f.write(f"\n[[Device.{parent}.connections]]\n")
        f.write(f"    type    = {_toml_string(category)}\n")
        f.write(f"    name    = {_toml_string(action['name'])}\n")
        f.write("    enabled = true\n")
        if action.get("connection_id"):
            f.write(f"    connection_id = {_toml_string(action['connection_id'])}\n")
        if category == "slave":
            if action.get("bus_type"):
                f.write(f"    bus = {_toml_string(str(action['bus_type']).lower())}\n")
            f.write(f"    device_scroll = {_toml_string(action['device_scroll'])}\n")


def _create_stub_file(path: Path, category: str, bus_type: Any = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if category == "endpoint" and path.suffix == ".py":
        path.write_text(_python_endpoint_stub(path.name), encoding="utf-8")
        os.chmod(path, 0o755)
        return
    
    if category == "slave":
        bus_str = str(bus_type).strip().lower() if bus_type else ""
        if bus_str == "spi":
            path.write_text(_c_spi_slave_stub(path.stem), encoding="utf-8")
            return
        elif bus_str == "i2c":
            path.write_text(_c_i2c_slave_stub(path.stem), encoding="utf-8")
            return

    path.write_text(_c_model_stub(path.stem), encoding="utf-8")

def _c_spi_slave_stub(model_name: str) -> str:
    return f"""// SPI Slave implementation: {model_name}.c
#include <stdint.h>
#include <device.h>
#include <boardrunner/spi.h>

uint32_t slave_spi_transfer(uint32_t value) {{
    return 0; // return MISO data
}}

void slave_spi_set_cs(int level) {{
    // level 0 usually means selected
}}
"""

def _c_i2c_slave_stub(model_name: str) -> str:
    symbol = re.sub(r"[^0-9A-Za-z_]", "_", model_name.lower())
    return f"""// I2C Slave implementation: {model_name}.c
#include <stdint.h>
#include <device.h>
#include <boardrunner/i2c.h>

int {symbol}_send(uint8_t data) {{
    return 0; // 0 for ACK, 1 for NACK
}}

uint8_t {symbol}_receive(void) {{
    return 0; // Data to send to master
}}

int {symbol}_event(enum i2c_event event) {{
    return 0;
}}
"""

def _c_model_stub(model_name: str) -> str:
    symbol = re.sub(r"[^0-9A-Za-z_]", "_", model_name.lower())
    return f"""#include <stdint.h>
#include <device.h>

void* {symbol}_init(ConfigSection* model_info) {{
    (void)model_info;
    return NULL;
}}

uint64_t {symbol}_read(void *opaque, uint64_t addr, unsigned size) {{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}}

void {symbol}_write(void *opaque, uint64_t addr, uint64_t value, unsigned size) {{
    (void)opaque;
    (void)addr;
    (void)value;
    (void)size;
}}
"""


def _python_endpoint_stub(filename: str) -> str:
    return f"""#!/usr/bin/env python3
# Host-side endpoint stub for {filename}.

def main():
    pass


if __name__ == "__main__":
    main()
"""
