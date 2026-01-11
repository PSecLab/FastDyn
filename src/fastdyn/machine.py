from dataclasses import dataclass, field
from typing import List, Optional, Any, Dict, Union
import uuid


# =============================================================================
# CPU sub-structures
# =============================================================================

@dataclass
class VirtualInstruction:
    """Synthetic instruction injected at a concrete instruction address."""
    at: Union[int, str]
    instruction: str
    args: List[str] = field(default_factory=list)


@dataclass
class InstructionModifier:
    """Runtime instruction-level patch."""
    at: Union[int, str]
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
    coverage: bool = False

    plugin_library: Optional[str] = None
    monitor_elf: Optional[str] = None

    enable_gdb: bool = False
    stop_on_start: bool = False
    launch_gdb: bool = False
    semihosting: bool = True
    semihosting_config: str = "enable=on,target=native"
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
ModelName = str

@dataclass
class DeviceModelDefaults:
    """
    Global defaults for a handler model type (from [Device.Models.<name>]).

    Example TOML:
      [Device.Models.passthrough]
      backend = "stlink"

    Stored as:
      device.models["passthrough"].params["backend"] == "stlink"
    """
    params: Dict[str, Any] = field(default_factory=dict)

@dataclass
class DeviceHandler:
    """
    One handler entry inside [[Device.<dev>.handlers]].

    Example TOML:
      [[Device.usart1.handlers]]
      model   = "elder"
      enabled = true
      scroll  = "/path/gen.so"

    Everything besides {model, enabled} goes into `params`.
    """
    model: ModelName                           # "qemu" | "elder" | "passthrough" | ...
    enabled: bool = True

    # QEMU-specific bits (optional)
    type: Optional[str] = None           # e.g., "stm32f2xx-usart"
    args: Optional[Any] = None           # could be str/list/dict depending on your parser

    # Plugin-specific bits (optional)
    scroll: Optional[str] = None         # path to .so (your example uses this for elder)

@dataclass
class SlaveDevice:
    """
    Optional: a slave device attached to a parent peripheral (SPI/I2C etc.).

    Example idea (TOML-ish):
      [[Device.i2c1.slaves]]
      device = "tmp102"
      model = ["elder", "passthrough"]
      param = "0x48"
      device_scroll = "/path/slave.so"
    """
    device: str
    model: List[ModelName] = field(default_factory=list)     # which models to attach this slave under

    # Optional scroll / implementation hook for the slave
    device_scroll: Optional[str] = None

    # Anything else you add later (timing, flags, quirks, etc.)
    params: Dict[str, Any] = field(default_factory=dict)


@dataclass
class DeviceConfig:
    """
    One peripheral/MMIO region description (from [Device.<name>]).

    Example TOML:
      [Device.usart1]
      irq = "0-15:20-25:41"
      ranges = ["0x40011000-0x40011FFF"]
      description = "..."
      scroll_config = " ... "
      [[Device.usart1.handlers]] ...
    """
    description: str = ""
    irq: Optional[str] = None            # keep as raw string (e.g. "0-15:20-25:41")
    ranges: List[str] = field(default_factory=list)  # raw "0x...-0x..." strings

    scroll_config: Optional[str] = None  # raw INI-like block string
    handlers: List[DeviceHandler] = field(default_factory=list)

    slaves: List[SlaveDevice] = field(default_factory=list)

    # Optional: room for future per-device knobs without schema changes
    params: Dict[str, Any] = field(default_factory=dict)

@dataclass
class DeviceSection:
    """
    Mirrors your [Device] root:
      - [Device.Models.*]     -> models
      - [Device.<devname>]    -> devices
    """
    models: Dict[ModelName, DeviceModelDefaults] = field(default_factory=dict)
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
    parsed_device: Dict = field(default_factory=dict)            #deliberatily kept because Fastdyn doesn't understand device type