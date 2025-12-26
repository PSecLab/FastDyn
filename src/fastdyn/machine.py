from dataclasses import dataclass, field
from typing import List, Optional, Any, Dict
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
    launch_gdb: bool = False
    semihosting: bool = True
    semihosting_config: str = ""
    monitor_port: Optional[int] = None
    qmp_socket: Optional[str] = None

    log_file: Optional[str] = None
    log_options: Optional[str] = None
    logger_content: Optional[str] = None

    virtuals: List = field(default_factory=list)
    modifiers: List = field(default_factory=list)

# =============================================================================
# Device / Peripheral modeling
# =============================================================================

@dataclass
class DeviceModelDefaults:
    """
    Global defaults for a handler model type (from [Device.Models.<name>]).
    Example: passthrough.backend = "stlink"
    """
    # Keep this flexible: different models may define different keys.
    params: Dict[str, Any] = field(default_factory=dict)

@dataclass
class DeviceHandler:
    """
    One handler entry inside [[Device.<dev>.handlers]].
    """
    model: str                           # "qemu" | "elder" | "passthrough" | ...
    enabled: bool = True

    # QEMU-specific bits (optional)
    type: Optional[str] = None           # e.g., "stm32f2xx-usart"
    args: Optional[Any] = None           # could be str/list/dict depending on your parser

    # Plugin-specific bits (optional)
    scroll: Optional[str] = None         # path to .so (your example uses this for elder)

@dataclass
class DeviceConfig:
    """
    One peripheral/MMIO region description (from [Device.<name>]).
    """
    description: str = ""
    irq: Optional[str] = None            # keep as raw string (e.g. "0-15:20-25:41")
    ranges: List[str] = field(default_factory=list)  # raw "0x...-0x..." strings

    scroll_config: Optional[str] = None  # raw INI-like block string
    handlers: List[DeviceHandler] = field(default_factory=list)

@dataclass
class DeviceSection:
    """
    Mirrors your [Device] root:
      - [Device.Models.*]     -> models
      - [Device.<devname>]    -> devices
    """
    models: Dict[str, DeviceModelDefaults] = field(default_factory=dict)
    devices: Dict[str, DeviceConfig] = field(default_factory=dict)

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
    device: DeviceSection = field(default_factory=DeviceSection)

"""
TODO: Remove before the release
Mental Model or Device Section
machine.device.models = {
  "passthrough": DeviceModelDefaults(params={"backend":"stlink"}),
  "elder": DeviceModelDefaults(params={}),
  ...
}

machine.device.devices = {
  "usart1": DeviceConfig(
      irq="0-15:20-25:41",
      ranges=["0x40011000-0x40011FFF"],
      description="Main USART ...",
      scroll_config='[usart1]\ninternal_model = "uart1_model"\n',
      handlers=[
         DeviceHandler(model="qemu", enabled=False, type="stm32f2xx-usart", args="device chardev"),
         DeviceHandler(model="elder", enabled=True, scroll="/scratch/.../gen.so"),
         DeviceHandler(model="passthrough", enabled=False),
      ],
  ),
  "unhandled_space": DeviceConfig(
      ranges=[...],
      handlers=[DeviceHandler(model="passthrough", enabled=True)]
  )
}
"""