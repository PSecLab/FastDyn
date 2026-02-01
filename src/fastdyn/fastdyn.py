'''
This package can be used independently as well to include as a tool.
See test/fastdyn_package/example.py
'''
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Dict, Optional, Tuple, Union, Sequence
import os, sys, pathlib
import subprocess
import logging

from .utils import helper

from . import fastdyn_log as fastdyn_log_conf
from .utils import parse_config as parse_helper

from .machine import VirtualInstruction

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

#supported targets
from .targets import qemu_target

class Fastdyn:
    def __init__(self):
        self.machines = {}

    def create_machine(self, machine_name, platform):
        machine = Machine(machine_name, platform)
        self.machines[machine_name] = machine

        return machine

    def run(self, target, machine_name, out_path=None):
        #before running resolve the device models for each machine
        machine = self.machines[machine_name]
        compile_device_routing(machine.devices, machine.parsed_device, models=machine.models) #return value will be written to machine.parsed_device

        if target.lower() == "qemu":
            #write device config to the json file
            qemu_cmd, gdb_cmd, launch_gdb, binary = qemu_target.setup_qemu(
                machine,
                out_path
            )

            qemu_target.start_execution(qemu_cmd, launch_gdb, gdb_cmd, binary)

    def shutdown(self):
        qemu_target.kill_qemu_process(port='5555')

class QemuTargetOpts:
    def __init__(self):
        #initialize with default values
        self.qemu_path: str = "qemu-system-arm"
        self.finline: Optional[str] = None
        self.coverage: bool = False
        self.enable_gdb: bool = False
        self.bbl_coverage: bool = False
        self.stop_on_start: bool = False
        self.launch_gdb: bool = False
        self.semihosting: bool = True
        self.semihosting_config: str = "enable=on,target=native"
        self.monitor_port: Optional[int] = 5555
        self.qmp_socket: Optional[str] = "/tmp/qmp.sock"

class Machine:
    def __init__(self, machine_name, platform_name):
        self.name: Optional[str] = machine_name
        self.cpus = []
        self.memories = {}
        self.devices = {}
        self.models = {}
        self.platform = platform_name
        self.qemu_target_opts = QemuTargetOpts()

        #optional params -- useful for svd
        self.irq_map = {}

        self.parsed_device = {}            #internal to the machine for qemu understanding

    def add_cpu(self, arch, machine, cpu, binary, init_nsvtor, twintrace, hardware_trace):
        cpu = CPU(arch, machine, cpu, binary, init_nsvtor, twintrace, hardware_trace, self) #pass parent for easy referencing to objs like irq_map
        self.cpus.append(cpu)
        return cpu

    def add_memory(self, memory_name, memory_id, memory_start, memory_size, memory_type, backend, memory_file, share=True):
        if memory_name in self.memories:
            raise KeyError(f"Unable to create a memory with name {memory_name}. Create a memory with a different name")

        #initial check to make sure main memory is added before optional rams
        if memory_name != "main" and len(self.memories) == 0:
            raise KeyError(f"Unable to create memory {memory_name}. First Create main memory")

        memory = Memory(memory_name, memory_id)

        if not memory.validate_and_add_memory(memory_start, memory_size, memory_type, backend, memory_file, share):
            raise ValueError(f"Unable to Create Memory: {memory_name}")

        self.memories[memory_name] = memory
        return memory

    def add_device(self, name):
        if name in self.devices:
            raise ValueError(f"Device with name {name} already attached")
        device = Device(name, self) #pass parent for easy referencing to objs like irq_map
        self.devices[name] = device

        return device

    def add_model(self, name, backend=None):
        if name in self.models:
            raise ValueError(f"Model with name: {name} already initialized")
        self.models[name] = {"backend": backend}

        return True

    #TODO: Update this
    def list_devices(self):
        pass

    def add_cmsis_svd(self, cmsis_svd):
        fastdyn_log.info("Parsing the passed directory path for the CMSIS SVD")
        svd, is_svd_file = parse_helper.discover_svd_files(cmsis_svd)
        if not is_svd_file:
                svd_device = parse_helper.get_svd_device(svd, self.platform)

        fastdyn_log.info("Creating IRQ Map using the CMSIS SVD")
        self.irq_map = parse_helper.create_svd_irq_map(svd_device)

        return True

@dataclass
class InstructionModifier:
    """Runtime instruction-level patch."""
    at: int
    patch: str

class CPU:
    def __init__(self, arch, machine, cpu, binary, init_nsvtor, twintrace, hardware_trace, machine_obj):
        """One CPU instance belonging to a machine."""
        self.arch = arch
        self.machine = machine
        self.cpu = cpu
        self.binary = binary
        self.init_nsvtor: int = init_nsvtor
        self.twintrace = twintrace       #supported options are record, replay or None
        self.hardware_trace = hardware_trace    # hardware log needed in case of replay

        self.machine_obj = machine_obj

        self.plugin_library: Optional[str] = "build/libfastdyn.so"
        self.monitor_elf: Optional[str] = None

        self.log_file: Optional[str] = "qemu.log"
        self.log_options: Optional[str] = "in_asm,op"

        self.virtuals = []
        self.modifiers = []
        self.logger_content: Optional[str] = """\
                                            # --- Plugin Logger Configuration ---
                                            # level = DEBUG
                                            # output = stderr
                                            """
        self.symbol_dict = {}

    def add_virtual_instruction(self, vi: Union["VirtualInstruction", str, Sequence[Union["VirtualInstruction", str]]]) -> bool:
        items = vi if isinstance(vi, (list, tuple)) else [vi]

        out: list[str] = []
        for item in items:
            ok, parsed = parse_helper.parse_vi(item)
            if not ok or parsed is None:
                fastdyn_log.error("Unable to parse Virtual Instruction")
                return False

            try:
                resolved = parse_helper.resolve_vi(parsed, symbol_map=self.symbol_dict, irq_map=self.machine_obj.irq_map)
            except Exception as e:
                fastdyn_log.error(f"Unable to resolve Virtual Instruction: {e}")
                return False

            out.append(parse_helper.vi_to_string(resolved))

        self.virtuals.extend(out)
        return True

    def add_modifier(
        self,
        mod: Union["InstructionModifier", str, Sequence[Union["InstructionModifier", str]]]
    ) -> bool:
        items = mod if isinstance(mod, (list, tuple)) else [mod]

        out: list[str] = []
        for item in items:
            ok, parsed = parse_helper.parse_mod(item)
            if not ok or parsed is None:
                fastdyn_log.error("Unable to parse Instruction Modifier")
                return False

            try:
                resolved = parse_helper.resolve_mod(parsed, symbol_map=self.symbol_dict)
            except Exception as e:
                fastdyn_log.error(f"Unable to resolve Instruction Modifier: {e}")
                return False

            out.append(parse_helper.mod_to_string(resolved))

        self.modifiers.extend(out)
        return True

    def update_cpu_param(self, param, val):
        if not hasattr(self, param):
            print(f"Unknown CPUConfig field: {param}")
            return False
        try:
            setattr(self, param, val)
            return True
        except (AttributeError, TypeError, ValueError) as e:
            print(f"Unable to set '{param}' to {val!r}: {e}")
            return False

    def add_map_file(self, map_file):
        #parse the map file to create a symbol lookup
        if os.path.exists(map_file):
            fastdyn_log.info(f"Parsing Config file: {map_file}")
            self.symbol_dict = parse_helper.load_symbol_addresses(map_file)
        else:
            fastdyn_log.error(f"Map file Path not found")
            return False
        return True

@dataclass
class DeviceHandler:
    model: str = ""                           # "qemu" | "elder" | "passthrough" | ...
    enabled: bool = True
    type: Optional[str] = None           # e.g., "stm32f2xx-usart"
    args: Optional[Any] = None           # could be str/list/dict depending on your parser
    scroll: Optional[str] = None         # path to .so (your example uses this for elder)

@dataclass
class Slave:
    device: str                         # The device ID/name
    model: list[str]                    # MUST be a list (e.g., ["elder"])
    params: Dict[str, Any] = field(default_factory=dict)
    device_scroll: Optional[str] = None # The path/identifier for the slave

class Device:
    def __init__(self, name, machine_obj):
        self.device_name = name
        self.machine_obj = machine_obj
        self.supported_ranges = []
        self.handlers = []
        self.irq_range = []
        #here just add ranges and validate them before running qemu (transformation step)

    def add_ranges(self, start, end=None, size=None):
        """
        Add a supported MMIO range for this device.

        Accepts:
        - start/end as int or str (hex like "0x40000000" or decimal like "1073741824")
        - size as int (bytes) or str with suffix: "B", "K"/"KB", "M"/"MB", "G"/"GB" (binary units, i.e., 1KB=1024B)

        Stores ranges internally as inclusive [start, end] integers.
        """
        import re

        def _parse_addr(x):
            if isinstance(x, int):
                return x
            if isinstance(x, str):
                return int(x.strip().lower(), 0)  # base=0 supports 0x.. and decimals
            raise TypeError(f"Address must be int or str, got {type(x)}")

        def _parse_size(x):
            if isinstance(x, int):
                if x < 0:
                    raise ValueError("size must be >= 0")
                return x
            if not isinstance(x, str):
                raise TypeError(f"size must be int or str, got {type(x)}")

            s = x.strip().lower()
            m = re.fullmatch(r"(\d+)\s*(b|kb|k|mb|m|gb|g)?", s)
            if not m:
                raise ValueError(f"Invalid size string: {x!r} (examples: '512K', '4MB', '1024')")

            val = int(m.group(1))
            unit = m.group(2) or "b"
            scale = {
                "b": 1,
                "k": 1024, "kb": 1024,
                "m": 1024**2, "mb": 1024**2,
                "g": 1024**3, "gb": 1024**3,
            }[unit]
            return val * scale

        if start is None:
            raise TypeError("start must not be None")
        if (end is None) == (size is None):
            raise TypeError("Provide exactly one of: end or size")

        start_i = _parse_addr(start)

        if end is not None:
            end_i = _parse_addr(end)
        else:
            size_i = _parse_size(size)
            if size_i <= 0:
                raise ValueError("size must be > 0")
            end_i = start_i + size_i - 1  # inclusive end

        if end_i < start_i:
            raise ValueError(f"Invalid range: end ({hex(end_i)}) < start ({hex(start_i)})")

        # store as inclusive integer range
        self.supported_ranges.append([start_i, end_i])
        return True

    def add_handler(self, name, enabled=True, args=None, scroll=None, type=None):
        handler_obj = DeviceHandler(
            model=str(name),       #required
            enabled=bool(enabled),  #required
            type=None if type is None else str(type),
            args=None if args is None else str(args),
            scroll=None if scroll is None else str(scroll),
        )
        self.handlers.append(handler_obj)
        return True

    def add_irq(self, irq):
        """
        Add IRQ(s) for this device.

        Supported inputs:
        - int: 53
        - str: "53", "0x35"  (numeric)
        - str: "USART1_IRQn" (symbol; resolved via machine_obj.irq_map)
        - [start, end]: inclusive range, where start/end can be int/str/symbol

        Behavior:
        - self.irq_range is a flat list of ints
        - add_irq(1) -> [1]
        - add_irq([2, 4]) -> extends with [2,3,4]
        - avoids duplicates (keeps list clean), keeps sorted ascending
        """
        def _resolve_one(x) -> int:
            # int directly
            if isinstance(x, int):
                return x

            # strings: numeric first, then symbol lookup
            if isinstance(x, str):
                s = x.strip()
                # numeric? (supports "53" and "0x35")
                try:
                    return int(s, 0)
                except ValueError:
                    pass

                # symbol: require irq_map only in this case
                irq_map = getattr(self.machine_obj, "irq_map", None)
                if not irq_map:
                    raise RuntimeError(
                        "IRQ symbol provided but machine_obj.irq_map is empty/unset. "
                        "Call machine.add_cmsis_svd(...) before using symbolic IRQ names."
                    )

                # support both {name->num} and {num->name}
                if s in irq_map and isinstance(irq_map[s], int):
                    return int(irq_map[s])

                for k, v in irq_map.items():
                    if v == s and isinstance(k, int):
                        return int(k)

                raise KeyError(f"IRQ symbol {s!r} not found in machine_obj.irq_map")

            raise TypeError(f"IRQ must be int, str, or a 2-item [start,end] list; got {type(x)}")

        # ensure storage exists
        if not hasattr(self, "irq_range") or self.irq_range is None:
            self.irq_range = []

        # internal dedup set (optional but keeps list clean and fast)
        irq_set = set(self.irq_range)

        def _add_value(v: int):
            if v < 0:
                raise ValueError("IRQ numbers must be >= 0")
            if v not in irq_set:
                self.irq_range.append(v)
                irq_set.add(v)

        # parse input
        if isinstance(irq, (int, str)):
            _add_value(_resolve_one(irq))

        elif isinstance(irq, (list, tuple)):
            if len(irq) != 2:
                raise TypeError("IRQ range must be a 2-item list/tuple like [start, end]")
            start = _resolve_one(irq[0])
            end   = _resolve_one(irq[1])

            if start < 0 or end < 0:
                raise ValueError("IRQ numbers must be >= 0")
            if end < start:
                raise ValueError(f"Invalid IRQ range: end ({end}) < start ({start})")

            for v in range(start, end + 1):
                _add_value(v)

        else:
            raise TypeError(f"IRQ must be int/str or a 2-item [start,end] list; got {type(irq)}")

        # keep deterministic order
        self.irq_range.sort()
        return True

    def add_slave(self, slave_data: dict):
            """
            Expects a dictionary containing at least 'device' and 'model'.
            All other keys (except 'device_scroll') are moved into 'params'.
            """
            if not hasattr(self, 'slaves') or self.slaves is None:
                self.slaves = []

            # 1. Extract metadata
            device_id = slave_data.get('device')
            models = slave_data.get('model', [])
            scroll = slave_data.get('device_scroll')

            # 2. Extract everything else into params
            # This takes every key in the TOML entry EXCEPT the metadata ones
            metadata_keys = {'device', 'model', 'device_scroll'}
            params = {k: v for k, v in slave_data.items() if k not in metadata_keys}

            # 3. Create and store the object
            new_slave = Slave(
                device=str(device_id),
                model=list(models) if isinstance(models, list) else [models],
                params=params,  # Now contains 'address', 'cs_id', etc.
                device_scroll=scroll
            )
            self.slaves.append(new_slave)
            return True

class MemoryType(Enum):
    SRAM = "SRAM"
    MMIO = "MMIO"
    FLASH = "FLASH"

class BackendType(Enum):
    FILE = "FILE"
    RAM = "RAM"
    MEMFD = "MEMFD"

class Memory:
    def __init__(self, memory_name, memory_id):
        self.memory_name: str = memory_name
        self.memory_id: str = memory_id
        self.memory_start: str = ""
        self.memory_size: str = ""
        self.memory_backend: BackendType
        self.memory_file: str = ""
        self.memory_type: MemoryType
        self.memory_share: bool = True

    def validate_and_add_memory(self, start, size, mem_type, mem_backend, memory_file, share):
        self.memory_start = start
        self.memory_size = size
        self.memory_share = share
        if isinstance(mem_type, str):
            try:
                self.memory_type = MemoryType[mem_type.upper()]
            except KeyError:
                valid = ", ".join([m.name for m in MemoryType])
                raise ValueError(f"Unknown memory type {mem_type!r}. Valid: {valid}")
        elif not isinstance(mem_type, MemoryType):
            raise TypeError(f"mem_type must be MemoryType or str, got {type(mem_type).__name__}")

        if isinstance(mem_backend, str):
            try:
                self.memory_backend = BackendType[mem_backend.upper()]
            except KeyError:
                valid = ", ".join([m.name for m in BackendType])
                raise ValueError(f"Unknown backend type {mem_backend!r}. Valid: {valid}")
        elif not isinstance(mem_backend, BackendType):
            raise TypeError(f"mem_type must be MemoryType or str, got {type(mem_backend).__name__}")

        # if not os.path.exists(memory_file):
        #     raise ValueError(f"memory file: {memory_file} does not exist")

        self.memory_file = memory_file

        return True

# =============================================================================
# Devices/Peripherals Information
# This api transforms the DeviceSection to Fastdyn understandable device format
# =============================================================================

import os
from typing import Any, Dict, Optional

def compile_device_routing(
    devices: Dict[str, Any],
    parsed_device: Dict[str, Any],
    *,
    models: Optional[Dict[str, Any]] = None,
    include_models: Optional[set[str]] = None,
    check_scroll_paths: bool = True,
) -> bool:
    """
    Transform {dev_name: Device} into model-centric routing JSON and write it into parsed_device.

    devices: machine.devices
    parsed_device: machine.parsed_device (dict that will be cleared+updated)
    models: machine.models (optional; used only to set routing[model]["backend"])
    """
    routing: Dict[str, Any] = {}
    builtin_qemu: Dict[str, Any] = {}

    def _dedup_preserve_order(seq):
        seen = set()
        out = []
        for x in seq:
            # handle unhashables
            key = x if isinstance(x, (str, int, float, tuple)) else repr(x)
            if key in seen:
                continue
            seen.add(key)
            out.append(x)
        return out

    def _model_backend(model: str):
        if not models:
            return None
        md = models.get(model)
        if isinstance(md, dict):
            return md.get("backend")
        return None

    # -----------------------------
    # Pass 1: group devices by enabled handler model
    # -----------------------------
    for dev_name, dev in devices.items():
        ranges = list(getattr(dev, "supported_ranges", []) or [])
        irq = getattr(dev, "irq_range", None)

        for h in getattr(dev, "handlers", []) or []:
            if getattr(h, "enabled", True) is False:
                continue

            model = getattr(h, "model", None)
            if not model:
                continue

            # QEMU builtins are separated
            if model == "qemu":
                h_type = getattr(h, "type", None)
                if not h_type:
                    raise ValueError(f"[Device.{dev_name}] qemu handler enabled but missing 'type'")
                builtin_qemu.setdefault(h_type, {})
                h_args = getattr(h, "args", None)
                if h_args is not None:
                    builtin_qemu[h_type]["args"] = h_args
                continue

            if include_models is not None and model not in include_models:
                continue

            bucket = routing.setdefault(model, {"overall": []})

            bucket["overall"].extend(ranges)

            entry = bucket.setdefault(dev_name, {})
            entry["range"] = list(ranges)

            if irq:
                entry["irq"] = irq

            # elder needs scroll
            if model == "elder":
                scroll = getattr(h, "scroll", None)
                if not scroll:
                    raise ValueError(f"[Device.{dev_name}] elder handler enabled but missing 'scroll'")
                entry["scroll"] = scroll

    # add backend + dedup overall ranges
    for model, bucket in routing.items():
        bucket["overall"] = _dedup_preserve_order(bucket["overall"])
        bucket["backend"] = _model_backend(model)

    # -----------------------------
    # Pass 2: slaves (kept compatible; your Device currently has none)
    # -----------------------------
    for dev_name, dev in devices.items():
        slaves = getattr(dev, "slaves", None)
        if not slaves:
            continue

        for slave in slaves:
            for model in getattr(slave, "model", []) or []:
                if include_models is not None and model not in include_models:
                    continue

                if model not in routing or dev_name not in routing[model]:
                    continue

                routing[model][dev_name].setdefault("slaves", {})
                slaves_out = routing[model][dev_name]["slaves"]

                params = getattr(slave, "params", {}) or {}
                s_cfg: Dict[str, Any] = dict(params)

                device_scroll = getattr(slave, "device_scroll", None)
                if device_scroll:
                    if check_scroll_paths and os.path.exists(device_scroll):
                        s_cfg["scroll_path"] = device_scroll
                        s_cfg["is_scroll_path"] = True
                    else:
                        s_cfg["is_scroll_path"] = False
                else:
                    s_cfg["is_scroll_path"] = False

                slaves_out[getattr(slave, "device")] = s_cfg

    # write to output dict (your current pattern)
    parsed_device.clear()
    parsed_device.update(routing)

    return True
