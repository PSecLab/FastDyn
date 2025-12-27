from .machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier
import json
import os
from dataclasses import asdict
from pathlib import Path
from typing import List

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
