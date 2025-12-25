from dataclasses import dataclass, field
from typing import List, Optional
import uuid


# =============================================================================
# CPU sub-structures
# =============================================================================

@dataclass
class VirtualInstruction:
    """Synthetic instruction injected at a concrete instruction address."""
    at: int
    instruction: str
    args: List[str] = field(default_factory=list)


@dataclass
class InstructionModifier:
    """Runtime instruction-level patch."""
    at: int
    patch: str


# =============================================================================
# Memory
# =============================================================================

@dataclass
class MemoryConfig:
    """
    Semantic memory layout.

    All numeric fields:
      - sizes are BYTES
      - addresses are PHYSICAL ADDRESSES
    """
    main_ram_size: int = 0
    shared_ram_size: int = 0
    ram_base_addr: int = 0
    shared_ram_base_addr: int = 0
    init_nsvtor: int = 0

    # Host-side backing configuration
    shared_mem_path: str = ""
    main_ram_file: str = ""
    shared_ram_file: str = ""


# =============================================================================
# CPU
# =============================================================================

@dataclass
class CPUConfig:
    """One CPU instance belonging to a machine."""
    arch: str = ""
    machine: str = ""
    cpu: str = ""
    platform: str = ""

    binary: str = ""
    qemu_path: str = ""
    finline: Optional[str] = None

    plugin_library: Optional[str] = None
    monitor_elf: Optional[str] = None

    enable_gdb: bool = False
    stop_on_start: bool = False
    monitor_port: Optional[int] = None
    qmp_socket: Optional[str] = None

    log_file: Optional[str] = None
    log_options: Optional[str] = None
    logger_content: Optional[str] = None

    virtuals: List[VirtualInstruction] = field(default_factory=list)
    modifiers: List[InstructionModifier] = field(default_factory=list)


# =============================================================================
# Machine
# =============================================================================

@dataclass
class Machine:
    """
    A full semantic machine description.

    Identity:
      - id   : globally unique, persistent identifier (UUID)
      - name : optional, human-friendly label
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    name: Optional[str] = None

    memory: MemoryConfig = field(default_factory=MemoryConfig)
    cpus: List[CPUConfig] = field(default_factory=list)

