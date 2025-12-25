from fastmcp import FastMCP
from machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier

import json
import os
from dataclasses import asdict
from pathlib import Path

mcp = FastMCP("fastdyn-machine")

# =============================================================================
# Machine registry
#
# Machines are identified ONLY by Machine.id.
# =============================================================================

machines: list[Machine] = []


def get_machine(machine_id: str) -> Machine:
    """
    Lookup a machine by its semantic identifier.
    """
    for m in machines:
        if m.id == machine_id:
            return m
    raise KeyError(f"Machine '{machine_id}' not found")


# =============================================================================
# Persistence configuration
# =============================================================================

PERSIST_DIR = Path(
    os.environ.get("FASTDYN_MACHINE_STORE", "~/.fastdyn/machines")
).expanduser()
PERSIST_DIR.mkdir(parents=True, exist_ok=True)


# =============================================================================
# Serialization helpers
# =============================================================================

def machine_to_dict(machine: Machine) -> dict:
    return asdict(machine)


def machine_from_dict(data: dict) -> Machine:
    m = Machine(
        id=data["id"],
        name=data.get("name"),
    )

    mem = data["memory"]
    m.memory = type(m.memory)(**mem)

    for c in data["cpus"]:
        cpu = CPUConfig(**{k: c[k] for k in c if k not in ("virtuals", "modifiers")})

        for v in c["virtuals"]:
            cpu.virtuals.append(VirtualInstruction(**v))

        for mod in c["modifiers"]:
            cpu.modifiers.append(InstructionModifier(**mod))

        m.cpus.append(cpu)

    return m


# =============================================================================
# Machine lifecycle tools
# =============================================================================

@mcp.tool
def create_machine(name: str | None = None) -> dict:
    """
    Create a new machine.

    Args:
        name: Optional human-friendly name (e.g., 'stm32f469_rtos').

    Returns:
        machine_id: Canonical machine identifier.
        name: Assigned machine name.
    """
    m = Machine(name=name)
    machines.append(m)
    return {
        "machine_id": m.id,
        "name": m.name,
    }


@mcp.tool
def list_machines():
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


@mcp.tool
def rename_machine(machine_id: str, name: str):
    """
    Assign or update a human-friendly name for a machine.

    Args:
        machine_id: Canonical machine identifier.
        name: New name for the machine.
    """
    get_machine(machine_id).name = name
    return {"ok": True}


@mcp.tool
def delete_machine(machine_id: str):
    """
    Delete a machine.

    Args:
        machine_id: Identifier of the machine to delete.
    """
    global machines
    machines = [m for m in machines if m.id != machine_id]
    return {"ok": True}


@mcp.tool
def get_machine_state(machine_id: str):
    """
    Retrieve the full state of a machine.
    """
    return get_machine(machine_id)


# =============================================================================
# Persistence tools
# =============================================================================

@mcp.tool
def save_machine(machine_id: str):
    """
    Persist a machine to disk.

    Args:
        machine_id: Identifier of the machine.
    """
    m = get_machine(machine_id)
    path = PERSIST_DIR / f"{m.id}.json"

    with open(path, "w") as f:
        json.dump(machine_to_dict(m), f, indent=2)

    return {
        "ok": True,
        "path": str(path),
        "name": m.name,
    }


@mcp.tool
def load_machine(machine_id: str):
    """
    Load a machine from disk.

    Args:
        machine_id: Identifier of the saved machine.
    """
    path = PERSIST_DIR / f"{machine_id}.json"
    if not path.exists():
        raise FileNotFoundError(machine_id)

    with open(path, "r") as f:
        data = json.load(f)

    m = machine_from_dict(data)
    machines.append(m)
    return {
        "machine_id": m.id,
        "name": m.name,
    }


@mcp.tool
def list_saved_machines():
    """
    List persisted machines.

    Returns:
        machines: List of saved machine IDs.
    """
    return {"machines": [p.stem for p in PERSIST_DIR.glob("*.json")]}


# =============================================================================
# Example mutator (pattern applies to all others)
# =============================================================================

@mcp.tool
def set_main_ram_size(machine_id: str, bytes: int):
    """
    Set main RAM size in BYTES.
    """
    get_machine(machine_id).memory.main_ram_size = bytes
    return {"ok": True}

