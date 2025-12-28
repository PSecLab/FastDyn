from .machine import Machine, CPUConfig, MemoryConfig, VirtualInstruction, InstructionModifier, DeviceSection, DeviceModelDefaults, DeviceConfig, DeviceHandler, SlaveDevice
import json
import os
from dataclasses import asdict
from pathlib import Path
from typing import List, Tuple, Any, Dict, Optional

from .utils import helper

# =============================================================================
# CPU lifecycle tools
# =============================================================================

def create_cpu() -> CPUConfig:
    cpu = CPUConfig()
    return cpu

def populate_cpu_config(cpu_config: CPUConfig, param, val):
    if not hasattr(cpu_config, param):
        print(f"Unknown CPUConfig field: {param}")
        return False
    try:
        setattr(cpu_config, param, val)
        return True
    except (AttributeError, TypeError, ValueError) as e:
        print(f"Unable to set '{param}' to {val!r}: {e}")
        return False

def machine_add_cpu(machine: Machine, cpu_config: CPUConfig) -> bool:
    try:
        machine.cpus.append(cpu_config)
        return True
    except Exception as e:
        print(f"Unable to access: {e}")
        return False

# =============================================================================
# Virtual Instructions and Modifiers
# =============================================================================
'''
Takes the virtual instruction and updates the cpu.virtuals
'''
def add_virtual_instruction(vi: VirtualInstruction, cpu_config: CPUConfig) -> bool:
    field_name = "virtuals"

    if not hasattr(cpu_config, field_name):
        print(f"Unknown CPUConfig field: {field_name}")
        return False

    try:
        lst = getattr(cpu_config, field_name)

        # initialize if needed
        if lst is None:
            lst = []
            setattr(cpu_config, field_name, lst)

        if not isinstance(lst, list):
            print(f"CPUConfig.{field_name} is not a list (got {type(lst)})")
            return False

        # build: "at instruction args"
        at_str = hex(vi.at) if isinstance(vi.at, int) else str(vi.at)

        args = vi.args if vi.args else []
        args_str = " ".join(str(a) for a in args).strip()

        line = f"{at_str} {vi.instruction}"
        if args_str:
            line += f" {args_str}"

        lst.append(line)
        return True

    except (AttributeError, TypeError, ValueError) as e:
        print(f"Unable to add virtual instruction {vi!r}: {e}")
        return False

def add_modifier_instruction(mod: InstructionModifier, cpu_config: CPUConfig) -> bool:
    field_name = "modifiers"

    if not hasattr(cpu_config, field_name):
        print(f"Unknown CPUConfig field: {field_name}")
        return False

    try:
        lst = getattr(cpu_config, field_name)

        # initialize if needed
        if lst is None:
            lst = []
            setattr(cpu_config, field_name, lst)

        if not isinstance(lst, list):
            print(f"CPUConfig.{field_name} is not a list (got {type(lst)})")
            return False

        # build: "at patch"
        at_str = hex(mod.at) if isinstance(mod.at, int) else str(mod.at)
        patch_str = str(mod.patch).strip()

        line = f"{at_str} {patch_str}" if patch_str else f"{at_str}"

        lst.append(line)
        return True

    except (AttributeError, TypeError, ValueError) as e:
        print(f"Unable to add instruction modifier {mod!r}: {e}")
        return False

# =============================================================================
# Memory Instructions and Modifiers
# =============================================================================

def populate_cpu_config(memory: MemoryConfig, param, val):
    if not hasattr(memory, param):
        print(f"Unknown MemoryConfig field: {param}")
        return False
    try:
        setattr(memory, param, val)
        return True
    except (AttributeError, TypeError, ValueError) as e:
        print(f"Unable to set '{param}' to {val!r}: {e}")
        return False

# =============================================================================
# Devices/Peripherals Information
# This api transforms the DeviceSection to Fastdyn understandable device format
# =============================================================================

def compile_device_routing(
    device_section: "DeviceSection",
    machine: Machine,
    *,
    include_models: Optional[set[str]] = None,
    check_scroll_paths: bool = True,
) -> bool:
    """
    Transform DeviceSection (canonical schema) into model-centric routing JSON.

    Returns:
      (routing_json, builtin_qemu)

    routing_json shape:
      {
        "<model>": {
          "overall": [...],
          "<devname>": {
             "range": [...],
             "irq": "...",
             # for elder: "scroll": "...",
             # optional: "slaves": {...}
          },
          "backend": <model_default_backend_or_None>
        },
        ...
      }

    builtin_qemu shape (optional utility):
      {
        "<qemu_type>": {"args": ...},
        ...
      }
    """
    routing: Dict[str, Any] = {}
    builtin_qemu: Dict[str, Any] = {}

    # Helper: model defaults like backend
    def _model_backend(model: str):
        md = device_section.models.get(model)
        if md is None:
            return None
        return md.params.get("backend")

    # -----------------------------
    # Pass 1: group devices by enabled handler model
    # -----------------------------
    for dev_name, dev in device_section.devices.items():
        # compile each enabled handler
        for h in dev.handlers:
            if not h.enabled:
                continue

            model = h.model

            # QEMU builtins are separated
            if model == "qemu":
                if not h.type:
                    # If enabled qemu handler lacks type, it's a config error
                    raise ValueError(f"[Device.{dev_name}] qemu handler enabled but missing 'type'")
                if h.type not in builtin_qemu:
                    builtin_qemu[h.type] = {}
                if h.args is not None:
                    builtin_qemu[h.type]["args"] = h.args
                continue

            # optional filter
            if include_models is not None and model not in include_models:
                continue

            # init model bucket
            bucket = routing.setdefault(model, {"overall": []})

            # overall ranges accumulate
            bucket["overall"].extend(dev.ranges)

            # per-device entry
            entry = bucket.setdefault(dev_name, {})
            entry["range"] = list(dev.ranges)
            if dev.irq is not None:
                entry["irq"] = dev.irq

            # elder needs scroll
            if model == "elder":
                if not h.scroll:
                    raise ValueError(f"[Device.{dev_name}] elder handler enabled but missing 'scroll'")
                entry["scroll"] = h.scroll

            #TODO: Add support for read_priority for differential testing in future

    # add backend + dedup overall ranges
    for model, bucket in routing.items():
        bucket["overall"] = helper._dedup_preserve_order(bucket["overall"])
        bucket["backend"] = _model_backend(model)

    # -----------------------------
    # Pass 2: attach slaves to every model they request (only if that model+dev exists)
    # -----------------------------
    for dev_name, dev in device_section.devices.items():
        if not dev.slaves:
            continue

        for slave in dev.slaves:
            for model in slave.model:
                if include_models is not None and model not in include_models:
                    continue

                # attach only if that device is present under that model (i.e., model handler enabled)
                if model not in routing or dev_name not in routing[model]:
                    continue

                routing[model][dev_name].setdefault("slaves", {})
                slaves_out = routing[model][dev_name]["slaves"]

                # Start from whatever the slave declared (cs_id/address/bus/etc.)
                s_cfg: Dict[str, Any] = dict(slave.params)

                # scroll-path check
                if slave.device_scroll:
                    if check_scroll_paths and os.path.exists(slave.device_scroll):
                        s_cfg["scroll_path"] = slave.device_scroll
                        s_cfg["is_scroll_path"] = True
                    else:
                        s_cfg["is_scroll_path"] = False
                else:
                    s_cfg["is_scroll_path"] = False

                slaves_out[slave.device] = s_cfg

    machine.parsed_device = routing # dont need builtin qemu for now

    return True

# =============================================================================
# Machine registry
#
# Machines are identified ONLY by Machine.id.
# =============================================================================

def get_machine(machine_id: str, machines: list[Machine]) -> Machine:
    """
    Lookup a machine by its semantic identifier.
    """
    for m in machines:
        if m.id == machine_id:
            return m
    raise KeyError(f"Machine '{machine_id}' not found")


# =============================================================================
# Machine lifecycle tools
# =============================================================================

def create_machine(name: str | None = None) -> Machine:
    m = Machine(name=name)
    return m

def list_machines(machines: list[Machine]):
    """
    List all active machines.

    Returns:
        machines: List of machines with id and name.
    """
    return {
        "machines": [
            {"id": m.id, "name": m.name}
            for m in machines
        ]
    }



def rename_machine(machine_id: str, name: str, machines: list[Machine]):
    """
    Assign or update a human-friendly name for a machine.

    Args:
        machine_id: Canonical machine identifier.
        name: New name for the machine.
    """
    get_machine(machine_id, machines).name = name
    return {"ok": True}



def delete_machine(machine_id: str, machines: list[Machine]):
    """
    Delete a machine.

    Args:
        machine_id: Identifier of the machine to delete.
    """
    machines = [m for m in machines if m.id != machine_id]
    return {"ok": True}



def get_machine_state(machine_id: str, machines: list[Machine]):
    """
    Retrieve the full state of a machine.
    """
    return get_machine(machine_id, machines)
