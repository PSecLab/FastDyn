from __future__ import annotations
import logging
import tomli
import sys

from . import helper
from typing import Optional, Dict, Any, Iterable, Tuple
import os
from .. import fastdyn_log as fastdyn_log_conf
from ..machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier, DeviceSection, DeviceModelDefaults, DeviceConfig, DeviceHandler, SlaveDevice
from .database import bus_rules
from .database.bus_rules import BUS_RULES


log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

def toml_parser(config_path):
    try:
        with open(config_path, "rb") as f:
            parsed_config = tomli.load(f)
    except FileNotFoundError:
        fastdyn_log.error(f"The file '{config_path}' was not found.")
    except tomli.TOMLDecodeError as e:
        fastdyn_log.error(f"Error: Failed to parse TOML file '{config_path}': {e}")

    return parsed_config

def parse_vi_and_modifiers(config, irq_map, symbols_dict):
    cpu_conf = config.get('CPU', {})

    virtual_instr_lines = []
    modifier_instr_lines = []

    virtuals = cpu_conf.get('virtuals', [])
    if virtuals:
        virtuals_ir = []
        for virt in virtuals:
            args_str = " ".join(virt.get('args', []))
            if (virt.get('instruction') == "raise_irq"):
                if args_str in irq_map:
                    args_str = str(irq_map[args_str])
                else:
                    # check if it's already a number
                    if not args_str.isdigit():
                        log.error("Invalid Interrupt for IRQ")
                        sys.exit()
            virtuals_ir.append(f"{virt.get('at')} {virt.get('instruction')} {args_str}\n")
        virtual_instr_lines = convert_config_file(virtuals_ir, symbols_dict)

    modifiers = cpu_conf.get('modifiers', [])
    if modifiers:
        modifiers_ir = []
        for mod in modifiers:
            lhs, rhs = helper.extract_regs(mod.get('patch'))
            modifiers_ir.append(f"{mod.get('at')} {lhs} {rhs}\n")
        modifier_instr_lines = convert_config_file(modifiers_ir, symbols_dict)

    virtual_instr: list[VirtualInstruction] = []
    for line in virtual_instr_lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        # expected: <at> <instruction> [args...]
        at_s = parts[0]
        instr = parts[1] if len(parts) > 1 else ""
        args = parts[2:] if len(parts) > 2 else []
        at = int(at_s, 0)  # handles "0x..." or decimal
        virtual_instr.append(VirtualInstruction(at=at, instruction=instr, args=args))

    modifier_instr: list[InstructionModifier] = []
    for line in modifier_instr_lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        # expected: <at> <lhs> <rhs>
        # you may have only 2 tokens in some cases; handle gracefully
        at_s = parts[0]
        at = int(at_s, 0)
        lhs = parts[1] if len(parts) > 1 else ""
        rhs = parts[2] if len(parts) > 2 else ""
        patch = f"{lhs} {rhs}".strip()
        modifier_instr.append(InstructionModifier(at=at, patch=patch))

    # return both lists (or whatever your caller expects)
    return virtual_instr, modifier_instr

def convert_config_file(input_lines: list[str], symbols_dict: Optional[dict]) -> list[str]:
    """
    Converts configuration file content by resolving symbols and returns a list of processed lines.

    Args:
        input_lines: A list of strings, where each string is a line from the input file.

    Returns:
        A list of processed and converted strings. Returns an empty list if a symbol
        cannot be found.
    """
    output_lines = []
    for line in input_lines:
        line = line.strip()
        if not line or line.startswith("#"):  # Ignore empty lines and comments
            continue

        tokens = line.split()
        if not tokens:
            continue

        # --- Process first token (could be a symbol) ---
        first_token = tokens[0]
        if helper.is_number(first_token) == "none":
            symbol, offset = helper.parse_symbol(first_token)
            if symbols_dict is not None and symbol in symbols_dict:
                resolved_address = symbols_dict[symbol]
                if resolved_address & 1:  # if thumb address, make it even
                    resolved_address -= 1
                first_token = hex(resolved_address + offset)
            else:
                fastdyn_log.warn(f"Symbol '{symbol}' from token '{tokens[0]}' not found in symbols dictionary.")
                return []  # Return empty list on failure

        # --- Process second token ---
        second_token = tokens[1]

        # --- Process third token (if it exists and could be a symbol) ---
        third_token = None
        if len(tokens) > 2:
            third_token = tokens[2]
            # Pass through if it looks like a memory access, pointer, or register
            if "*" in third_token or "[" in third_token or "]" in third_token or helper.is_cortexm_register(third_token):
                pass
            elif helper.is_number(third_token) == "none":
                symbol, offset = helper.parse_symbol(third_token) # parse symbol and offset
                if symbols_dict is not None and symbol in symbols_dict:
                    # Here we assume offset is 0 for the third token if it's just a symbol
                    if offset != 0:
                            fastdyn_log.warn(f"Offset for third token '{third_token}' is not supported. Treating as symbol only.")
                    third_token = hex(symbols_dict[symbol])
                else:
                    fastdyn_log.warn(f"Symbol '{symbol}' from token '{third_token}' not found in symbols dictionary.")
                    return []  # Return empty list on failure

        # --- Construct the output line ---
        final_line = f"{first_token} {second_token}"
        if third_token:
            final_line += f" {third_token}\n"
        else:
            final_line += f"\n"
        output_lines.append(final_line)

    return output_lines


def create_svd_irq_map(svd_device):
    name_map = {}
    for peripheral in svd_device.peripherals:
        if peripheral.interrupts:
            for interrupt in peripheral.interrupts:
                # The key is the interrupt name, the value is the number
                name_map[interrupt.name] = interrupt.value
    return name_map

def load_symbol_addresses(filename: str) -> dict[str, int]:
    """
    Reads a file with lines of format: symbol:address
    and returns a dictionary mapping symbol -> address.
    """
    symbols = {}
    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line or ":" not in line:
                continue
            symbol, address = line.split(":", 1)  # split only once
            address = int(address.strip(), 16)
            # check if address is thumb (odd)
            if address & 1:
                address -= 1  # make even
            symbols[symbol.strip()] = address
    return symbols

def validate_slave_params(
    *,
    parent_device: str,
    slave_device: str,
    params: Dict[str, Any],
) -> None:
    """
    Validates slave.params based on bus rules.

    Bus selection:
      1) params["bus"] if present
      2) inferred from parent device name (i2c1/spi2/etc)
      3) if no bus, skip validation (or you can choose to error)
    """
    bus = params.get("bus")
    if bus is not None:
        bus = str(bus).lower()
    else:
        bus = bus_rules.infer_bus_from_parent(parent_device)

    if not bus:
        # choose one:
        # - return (no validation)
        # - or raise to force explicit bus in config
        raise ValueError(
            f"[Device.{parent_device}] slave '{slave_device}': unknown bus '{bus}'"
        )

    rules = BUS_RULES.get(bus)
    if rules is None:
        raise ValueError(
            f"[Device.{parent_device}] slave '{slave_device}': unknown bus '{bus}'"
        )

    missing = [k for k in rules["required"] if params.get(k) in (None, "")]
    if missing:
        raise ValueError(
            f"[Device.{parent_device}] slave '{slave_device}' bus='{bus}' missing required keys: {missing}"
        )

def parse_devices_info(toml_config: Dict[str, Any]) -> "DeviceSection":
    """
    Parse the full [Device] TOML section into the canonical DeviceSection schema.

    Explicit-field handler strategy:
      - DeviceHandler has first-class fields: model, enabled, type, args, scroll
      - We do NOT store handler keys into a params dict.

    Unknown per-device keys go into DeviceConfig.params.
    Unknown per-slave keys go into SlaveDevice.params.
    """
    if not isinstance(toml_config, dict):
        raise TypeError(f"parse_devices_info expected dict, got {type(toml_config)}")

    device_root = toml_config.get("Device", {}) or {}
    if not isinstance(device_root, dict):
        raise TypeError(f"[Device] must be a table/dict, got {type(device_root)}")

    out = DeviceSection()

    # ----------------------------
    # Parse [Device.Models.*]
    # ----------------------------
    models_tbl = device_root.get("Models", {}) or {}
    if models_tbl:
        if not isinstance(models_tbl, dict):
            raise TypeError(f"[Device.Models] must be a table/dict, got {type(models_tbl)}")

        for model_name, model_data in models_tbl.items():
            # allow empty models like [Device.Models.elder] with no keys
            if model_data is None:
                model_data = {}
            if not isinstance(model_data, dict):
                # if someone wrote a scalar accidentally, store it in a single key
                model_data = {"value": model_data}

            out.models[str(model_name)] = DeviceModelDefaults(params=dict(model_data))

    # ----------------------------
    # Parse each [Device.<dev>]
    # ----------------------------
    reserved_top_keys = {"Models"}

    for dev_name, dev_data in device_root.items():
        if dev_name in reserved_top_keys:
            continue
        if not isinstance(dev_data, dict):
            # ignore non-tables defensively
            continue

        cfg = DeviceConfig()

        # Known keys (dedicated fields)
        if "description" in dev_data:
            cfg.description = dev_data.get("description") or ""

        if "irq" in dev_data:
            cfg.irq = dev_data.get("irq")

        if "ranges" in dev_data:
            ranges = dev_data.get("ranges") or []
            if not isinstance(ranges, list):
                raise TypeError(f"[Device.{dev_name}].ranges must be a list, got {type(ranges)}")
            cfg.ranges = [str(r) for r in ranges]

        if "scroll_config" in dev_data:
            cfg.scroll_config = dev_data.get("scroll_config")

        # ----------------------------
        # Handlers: [[Device.<dev>.handlers]]
        # ----------------------------
        handlers = dev_data.get("handlers", []) or []
        if handlers:
            if not isinstance(handlers, list):
                raise TypeError(f"[Device.{dev_name}].handlers must be an array, got {type(handlers)}")

            for h in handlers:
                if not isinstance(h, dict):
                    raise TypeError(
                        f"Each [[Device.{dev_name}.handlers]] entry must be a table/dict, got {type(h)}"
                    )

                model = h.get("model")
                if not model:
                    raise ValueError(f"Missing 'model' in [[Device.{dev_name}.handlers]]")

                enabled = h.get("enabled", True)
                if enabled is None:
                    enabled = True  # optional -> default enabled

                handler_obj = DeviceHandler(
                    model=str(model),
                    enabled=bool(enabled),
                    type=None if h.get("type") is None else str(h.get("type")),
                    args=h.get("args"),
                    scroll=None if h.get("scroll") is None else str(h.get("scroll")),
                )

                # Optional guardrails (helpful errors early)
                if handler_obj.enabled:
                    if handler_obj.model == "qemu" and handler_obj.type is None:
                        log.warning(f"[Device.{dev_name}] enabled qemu handler is missing 'type'")
                    if handler_obj.model == "elder" and handler_obj.scroll is None:
                        log.warning(f"[Device.{dev_name}] enabled elder handler is missing 'scroll'")

                cfg.handlers.append(handler_obj)

        # ----------------------------
        # Slaves: [[Device.<dev>.slaves]]
        # ----------------------------
        slaves = dev_data.get("slaves", []) or []
        if slaves:
            if not isinstance(slaves, list):
                raise TypeError(f"[Device.{dev_name}].slaves must be an array, got {type(slaves)}")

            for s in slaves:
                if not isinstance(s, dict):
                    raise TypeError(
                        f"Each [[Device.{dev_name}.slaves]] entry must be a table/dict, got {type(s)}"
                    )

                slave_dev = s.get("device")
                if not slave_dev:
                    raise ValueError(f"Missing 'device' in [[Device.{dev_name}.slaves]]")

                model_list = s.get("model", []) or []
                if isinstance(model_list, str):
                    model_list = [model_list]
                if not isinstance(model_list, list):
                    raise TypeError(
                        f"[[Device.{dev_name}.slaves]].model must be a list (or string), got {type(model_list)}"
                    )

                cs_id = s.get("cs_id")
                address = s.get("address")
                device_scroll = s.get("device_scroll")

                # Everything else goes into params
                slave_params = {
                    k: v
                    for k, v in s.items()
                    if k not in {"device", "model", "device_scroll"}
                }

                validate_slave_params(
                    parent_device=str(dev_name),
                    slave_device=str(slave_dev),
                    params=slave_params,
                )

                cfg.slaves.append(
                    SlaveDevice(
                        device=str(slave_dev),
                        model=[str(m) for m in model_list],
                        device_scroll=None if device_scroll is None else str(device_scroll),
                        params=slave_params,
                    )
                )

        # ----------------------------
        # Any other per-device keys -> cfg.params
        # ----------------------------
        known_device_keys = {"description", "irq", "ranges", "scroll_config", "handlers", "slaves"}
        extra_params = {k: v for k, v in dev_data.items() if k not in known_device_keys}
        cfg.params.update(extra_params)

        out.devices[str(dev_name)] = cfg

    return out