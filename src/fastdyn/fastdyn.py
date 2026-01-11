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

from . import qemu_target
from .utils import helper

from . import fastdyn_log as fastdyn_log_conf
from .utils import parse_config as parse_helper

from .machine import VirtualInstruction

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


class Fastdyn:
    def __init__(self):
        self.machines = {}

    def create_machine(self, machine_name, platform):
        machine = Machine(machine_name, platform)
        self.machines[machine_name] = machine

        return machine

    def run(self, machine_name, out_path=None):
        #before running resolve the device models for each machine
        machine = self.machines[machine_name]
        compile_device_routing(machine.devices, machine.parsed_device, models=machine.models)
        #write device config to the json file

        qemu_cmd, gdb_cmd, launch_gdb, binary = qemu_target.setup_qemu(
            machine,
            out_path
        )

        qemu_target.start_execution(qemu_cmd, launch_gdb, gdb_cmd, binary)

    def shutdown(self):
        qemu_target.kill_qemu_process()

class Machine:
    def __init__(self, machine_name, platform_name):
        self.id: str = field(default_factory=lambda: str(uuid.uuid4()))
        self.name: Optional[str] = machine_name
        self.cpus = []
        self.memories = {}
        self.devices = {}
        self.models = {}
        self.platform = platform_name

        #optional params -- useful for svd
        self.irq_map = {}

        self.parsed_device = {}            #internal to the machine for qemu understanding

    def add_cpu(self, arch, machine, cpu, binary, init_nsvtor):
        cpu = CPU(arch, machine, cpu, binary, init_nsvtor)
        self.cpus.append(cpu)
        return cpu

    def add_memory(self, memory_name, memory_start, memory_size, memory_type, memory_file):
        if memory_name in self.memories:
            raise KeyError(f"Unable to create a memory with name {memory_name}. Create a memory with a different name")

        memory = Memory(memory_name)

        if not memory.validate_and_add_memory(memory_start, memory_size, memory_type, memory_file):
            raise ValueError(f"Unable to Create Memory: {memory_name}")

        self.memories[memory_name] = memory
        return memory

    def add_device(self, name):
        if name in self.devices:
            raise ValueError(f"Device with name {name} already attached")
        device = Device(name)
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
        irq_map = parse_helper.create_svd_irq_map(svd_device)

        #add to each cpu for easy access in cpu
        self.irq_map = irq_map
        for cpu in self.cpus:
            cpu.irq_map = irq_map

        return True

@dataclass
class InstructionModifier:
    """Runtime instruction-level patch."""
    at: int
    patch: str

class CPU:
    def __init__(self, arch, machine, cpu, binary, init_nsvtor):
        """One CPU instance belonging to a machine."""
        self.arch = arch
        self.machine = machine
        self.cpu = cpu
        self.binary = binary
        self.init_nsvtor: int = init_nsvtor

        self.qemu_path: str = "qemu-system-arm"

        #initialize with default values
        self.finline: Optional[str] = None
        self.coverage: bool = False

        self.plugin_library: Optional[str] = "build/libfastdyn.so"
        self.monitor_elf: Optional[str] = None

        self.enable_gdb: bool = False
        self.stop_on_start: bool = False
        self.launch_gdb: bool = False
        self.semihosting: bool = True
        self.semihosting_config: str = "enable=on,target=native"
        self.monitor_port: Optional[int] = 5555
        self.qmp_socket: Optional[str] = "/tmp/qmp.sock"

        self.log_file: Optional[str] = "qemu.log"
        self.log_options: Optional[str] = "in_asm,op"

        self.virtuals = []
        self.modifiers = []
        self.logger_content: Optional[str] = """\
                                            # --- Plugin Logger Configuration ---
                                            # level = DEBUG
                                            # output = stderr
                                            """
        self.symbol_dict = None
        self.irq_map = None

    def add_virtual_instruction(self, vi: Union["VirtualInstruction", str, Sequence[Union["VirtualInstruction", str]]]) -> bool:
        items = vi if isinstance(vi, (list, tuple)) else [vi]

        out: list[str] = []
        for item in items:
            ok, parsed = parse_helper.parse_vi(item)
            if not ok or parsed is None:
                fastdyn_log.error("Unable to parse Virtual Instruction")
                return False

            try:
                resolved = parse_helper.resolve_vi(parsed, symbol_map=self.symbol_dict, irq_map=self.irq_map)
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

class Device:
    def __init__(self, name):
        self.device_name = name
        self.supported_ranges = []
        self.handlers = []
        self.irq_range = ""

    def add_ranges(self, ranges):
        #here just add ranges and validate them before running qemu (transformation step)
        self.supported_ranges = ranges
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

    def add_irq_ranges(self, irq_range=""):
        self.irq_range = irq_range
        return True

class MemoryType(Enum):
    SRAM = "SRAM"
    MMIO = "MMIO"
    FLASH = "FLASH"

class Memory:
    def __init__(self, memory_name):
        self.memory_name: str = memory_name
        self.memory_start: str = ""
        self.memory_size: str = ""
        self.memory_file: str = ""
        self.memory_type: MemoryType

    def validate_and_add_memory(self, start, size, mem_type, memory_file):
        self.memory_start = start
        self.memory_size = size
        if isinstance(mem_type, str):
            try:
                self.memory_type = MemoryType[mem_type.upper()]
            except KeyError:
                valid = ", ".join([m.name for m in MemoryType])
                raise ValueError(f"Unknown memory type {mem_type!r}. Valid: {valid}")
        elif not isinstance(mem_type, MemoryType):
            raise TypeError(f"mem_type must be MemoryType or str, got {type(mem_type).__name__}")

        if not os.path.exists(memory_file):
            raise ValueError(f"memory file: {memory_file} does not exist")

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
