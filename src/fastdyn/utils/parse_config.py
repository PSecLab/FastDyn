from __future__ import annotations
import logging
import tomli
import sys

from . import helper
from typing import Optional, Dict, Any, Iterable, Tuple, Union
import os, sys
import re
from .. import fastdyn_log as fastdyn_log_conf
from ..machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier, DeviceSection, DeviceModelDefaults, DeviceConfig, DeviceHandler, SlaveDevice
from .database import bus_rules
from .database.bus_rules import BUS_RULES
from cmsis_svd.parser import SVDParser


log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

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

'''
Helper functions for symbol parsing
'''
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

'''
Helper functions to parse the CMSIS-SVD
'''

import os
import sys
import glob

class SvdResolutionError(Exception):
    pass


def _find_ci_key(keys, target):
    if not target:
        return None
    if target in keys:
        return target
    tl = target.lower()
    for k in keys:
        if k.lower() == tl:
            return k
    return None


def discover_svd_files(svd_path: str):
    """
    Discover SVD files from a user-provided path.

    Args:
        svd_path: Path to either:
            - a single .svd file, or
            - a directory to recursively search for .svd files

    Returns:
        (svd_map, is_file)
          - svd_map: dict[str, str] mapping device_name -> absolute path to .svd
          - is_file: True if single-file mode
    """
    if not svd_path:
        raise SvdResolutionError("No SVD path provided.")

    svd_path = os.path.abspath(os.path.expanduser(svd_path))

    if not os.path.exists(svd_path):
        raise SvdResolutionError(f"SVD path does not exist: {svd_path}")

    svd_map = {}
    is_file = False

    # Case 1: single file
    if os.path.isfile(svd_path):
        if not svd_path.lower().endswith(".svd"):
            raise SvdResolutionError(f"Provided file is not an .svd: {svd_path}")

        device_name = os.path.splitext(os.path.basename(svd_path))[0]
        svd_map[device_name] = svd_path
        is_file = True
        return svd_map, is_file

    # Case 2: directory
    if not os.path.isdir(svd_path):
        raise SvdResolutionError(f"SVD path is neither a file nor a directory: {svd_path}")

    for root, _, files in os.walk(svd_path):
        for filename in files:
            if filename.lower().endswith(".svd"):
                device_name = os.path.splitext(filename)[0]
                full_path = os.path.abspath(os.path.join(root, filename))

                # Keep first occurrence (current behavior)
                if device_name in svd_map:
                    continue

                svd_map[device_name] = full_path

    return svd_map, is_file


def resolve_svd(
    platform_or_board: str,
    svd: str | None = None,
    default_dir: str | None = "third_party/common/cmsis-svd-data",
    auto_discover: bool = True,
    search_root: str | None = None,
):
    """
    Resolve to a specific SVD file.

    Returns:
        (svd_file_path, svd_key)

    Policy:
      - If svd is a file: use it directly
      - If svd is a directory: resolve by platform_or_board (case-insensitive)
      - If svd is None: use default_dir (directory), resolve by platform_or_board
      - If not found AND svd is None AND auto_discover: search repo for <board>.svd
    """
    if svd is None:
        if not default_dir:
            raise SvdResolutionError("No SVD path provided and no default_dir configured.")
        svd_input = default_dir
    else:
        svd_input = svd

    svd_input = os.path.abspath(os.path.expanduser(svd_input))
    svd_map, is_file = discover_svd_files(svd_input)

    if not svd_map:
        raise SvdResolutionError(f"No SVD files discovered from: {svd_input}")

    # Single-file mode: pick the only entry
    if is_file:
        svd_key = next(iter(svd_map))
        return svd_map[svd_key], svd_key

    # Directory mode: resolve by name
    svd_key = _find_ci_key(svd_map.keys(), platform_or_board)
    if svd_key is not None:
        return svd_map[svd_key], svd_key

    # Optional auto-discovery ONLY when user didn't explicitly pass --svd
    if auto_discover and svd is None:
        search_root = os.path.abspath(search_root or os.getcwd())
        target_lower = f"{platform_or_board.lower()}.svd"

        matches = []
        # Fast-ish glob (case-sensitive). Still useful on most repos.
        matches.extend(
            os.path.abspath(p)
            for p in glob.glob(os.path.join(search_root, "**", f"{platform_or_board}.svd"), recursive=True)
            if os.path.isfile(p)
        )

        # Case-insensitive fallback (walk)
        if not matches:
            for root, _, files in os.walk(search_root):
                for fn in files:
                    if fn.lower() == target_lower:
                        matches.append(os.path.abspath(os.path.join(root, fn)))

        matches = sorted(set(matches))

        if len(matches) == 1:
            auto_file = matches[0]
            auto_map, auto_is_file = discover_svd_files(auto_file)
            auto_key = next(iter(auto_map))
            return auto_map[auto_key], auto_key

        if len(matches) > 1:
            raise SvdResolutionError(
                f"Multiple SVD files matched board '{platform_or_board}'. "
                "Pass --svd <file-or-directory> explicitly."
            )

    raise SvdResolutionError(
        f"Could not resolve SVD for '{platform_or_board}' from '{svd_input}'. "
        "Pass --svd <file-or-directory> explicitly."
    )

def get_svd_device(svd_file):
    parser = SVDParser.for_xml_file(svd_file)
    svd_device = parser.get_device()
    return svd_device

def create_svd_irq_map(svd_device):
    name_map = {}
    for peripheral in svd_device.peripherals:
        if peripheral.interrupts:
            for interrupt in peripheral.interrupts:
                # The key is the interrupt name, the value is the number
                name_map[interrupt.name] = interrupt.value
    return name_map

'''
Helper functions to parse the Virtual Instructions
'''

def parse_vi(vi) -> Tuple[bool, Optional["VirtualInstruction"]]:
    """
    Accepts either:
      - a string formatted as: "<at> <instruction> [args...]"
        where <at> can be a symbol, decimal, or hex (with/without 0x)
      - a VirtualInstruction object

    Returns:
      (ok, VirtualInstruction | None)
    """
    if isinstance(vi, VirtualInstruction):
        return True, vi

    if not isinstance(vi, str):
        fastdyn_log.error(f"Only string and VirtualInstruction format accepted (got {type(vi).__name__})")
        return False, None

    s = vi.strip()
    if not s:
        return False, None

    parts = s.split()
    if len(parts) < 2:
        fastdyn_log.error(f"Invalid VI line (need: '<at> <instruction> [args...]'): {vi!r}")
        return False, None

    at_tok, instr = parts[0], parts[1]
    args = parts[2:]

    tok = at_tok.strip()
    at_val: Union[int, str]
    if tok.lower().startswith("0x"):
        try:
            at_val = int(tok, 16)
        except ValueError:
            at_val = tok
    elif tok.isdigit():
        at_val = int(tok, 10)
    elif re.fullmatch(r"[0-9a-fA-F]+", tok):  # hex without 0x
        try:
            at_val = int(tok, 16)
        except ValueError:
            at_val = tok
    else:
        at_val = tok

    return True, VirtualInstruction(at=at_val, instruction=instr, args=args)

def resolve_vi(vi: "VirtualInstruction",
               irq_map: Dict[str, Any],
               symbol_map: Dict[str, Any]) -> "VirtualInstruction":
    """
    Resolve a VirtualInstruction using irq_map and symbol_map.

    Rules:
      - at:
          * if int: keep
          * if number-ish string (0x.. / decimal / bare-hex): convert to int
          * supports leading '*' (e.g., "*main" or "*0x8000") by stripping '*' then resolving
          * else: resolve via symbol_map (supports sym+/-offset), else raise KeyError
          * if resolved from symbol_map: clear Thumb bit (addr&1 -> addr-1) before applying offset
      - args:
          * leading '*' deref is supported: "*sym", "*sym+4", "**sym", "*0x1000" etc.
            - resolve the inner token via irq_map / numeric / symbol_map when possible, then reattach '*'
          * bracketed memory expressions and registers are passed through unchanged (e.g., "[r0]", "sp")
          * otherwise:
              - try irq_map exact match -> numeric string
              - else if numeric -> keep as-is
              - else try symbol_map (supports sym+/-offset) -> hex string
              - else keep as-is
      - raise_irq:
          * first arg must be number-ish after resolution, else raise ValueError
      - returns: new VirtualInstruction
    """
    sym_off_re = re.compile(r"^(.+?)([+-])(0x[0-9a-fA-F]+|\d+)$")
    hex_noprefix_re = re.compile(r"^[0-9a-fA-F]+$")
    reg_re = re.compile(r"^(r([0-9]|1[0-5])|sp|lr|pc|xpsr|apsr|ipsr|epsr)$", re.IGNORECASE)
    deref_re = re.compile(r"^(\*+)\s*(.+)$")

    def is_numberish(tok: str) -> bool:
        t = tok.strip()
        if not t:
            return False
        try:
            if t.lower().startswith("0x") or t.isdigit():
                int(t, 0)
                return True
        except ValueError:
            return False
        # bare hex
        try:
            return bool(hex_noprefix_re.fullmatch(t)) and int(t, 16) >= 0
        except ValueError:
            return False

    def to_int(tok: str) -> int:
        t = tok.strip()
        if t.lower().startswith("0x"):
            return int(t, 16)
        if t.isdigit():
            return int(t, 10)
        # bare hex
        return int(t, 16)

    def parse_symbol_offset(tok: str):
        """
        Returns (symbol, offset_int) if tok looks like sym(+/-)offset, else (tok, 0).
        """
        t = tok.strip()
        m = sym_off_re.fullmatch(t)
        if not m:
            return t, 0
        sym = m.group(1).strip()
        sign = m.group(2)
        off = int(m.group(3), 0)
        return sym, (off if sign == "+" else -off)

    def normalize_map_value(v: Any) -> int:
        # symbol_map/irq_map may store ints or numeric strings (including bare hex)
        if isinstance(v, int):
            return v
        if isinstance(v, str):
            s = v.strip()
            if not s:
                raise ValueError("empty numeric string in map")
            if s.lower().startswith("0x") or s.isdigit():
                return int(s, 0)
            # bare hex like "800395e"
            return int(s, 16)
        return int(v)

    def try_resolve_simple(tok: str, *, thumb_fix: bool = False) -> Optional[str]:
        """
        Resolve a single token into a numeric string if possible.
        Order: irq_map -> numeric -> symbol_map(sym+/-off)
        """
        t = tok.strip()
        if not t:
            return None

        if t in irq_map:
            return str(normalize_map_value(irq_map[t]))

        if is_numberish(t):
            return t  # keep formatting as provided

        sym, off = parse_symbol_offset(t)
        if sym in symbol_map:
            base = normalize_map_value(symbol_map[sym])
            if thumb_fix and (base & 1):
                base -= 1
            return hex(base + off)

        return None

    # --- resolve at ---
    if isinstance(vi.at, int):
        resolved_at = vi.at
    else:
        at_tok = str(vi.at).strip()
        if not at_tok:
            raise KeyError("Unresolved VI.at: empty token")

        # support gdb-ish "*main" / "*0x8000" by stripping leading '*'
        while at_tok.startswith("*"):
            at_tok = at_tok[1:].lstrip()
        if not at_tok:
            raise KeyError("Unresolved VI.at: only '*' provided")

        if is_numberish(at_tok):
            resolved_at = to_int(at_tok)
        else:
            sym, off = parse_symbol_offset(at_tok)
            if sym not in symbol_map:
                raise KeyError(f"Unresolved VI.at symbol: {sym!r} (from {str(vi.at)!r})")
            base = normalize_map_value(symbol_map[sym])
            if base & 1:  # thumb bit fix
                base -= 1
            resolved_at = base + off

    # --- resolve args ---
    resolved_args = []
    for arg in (vi.args or []):
        a = str(arg).strip()
        if not a:
            continue

        # 1) Handle leading deref like *main, **foo+4, *0x8000
        m = deref_re.fullmatch(a)
        if m:
            stars, inner = m.group(1), m.group(2).strip()
            inner_res = try_resolve_simple(inner, thumb_fix=True)
            if inner_res is not None:
                resolved_args.append(stars + inner_res)
                continue
            # if we can't resolve inner, fall through (and likely pass-through)

        # 2) Pass-through for bracketed memory expressions and registers
        if ("[" in a) or ("]" in a) or reg_re.fullmatch(a):
            resolved_args.append(a)
            continue

        # 3) If it still contains '*' somewhere (complex expression), don't touch it
        if "*" in a:
            resolved_args.append(a)
            continue

        # 4) irq_map first
        if a in irq_map:
            resolved_args.append(str(normalize_map_value(irq_map[a])))
            continue

        # 5) numeric -> keep
        if is_numberish(a):
            resolved_args.append(a)
            continue

        # 6) symbol_map (sym+/-offset)
        sym, off = parse_symbol_offset(a)
        if sym in symbol_map:
            base = normalize_map_value(symbol_map[sym])
            resolved_args.append(hex(base + off))
        else:
            resolved_args.append(a)

    # --- instruction-specific validation ---
    if vi.instruction == "raise_irq" and resolved_args:
        if not is_numberish(resolved_args[0]):
            raise ValueError(f"Invalid IRQ for raise_irq: {resolved_args[0]!r}")

    return VirtualInstruction(at=resolved_at, instruction=vi.instruction, args=resolved_args)

def vi_to_string(vi: "VirtualInstruction") -> str:
    """
    Convert a VirtualInstruction to: "<at> <instruction> [args...]"

    - at: int -> hex (0x...), str -> as-is
    - args: joined with spaces
    """
    at_str = f"0x{vi.at:x}" if isinstance(vi.at, int) else str(vi.at)
    args_str = " ".join(str(a) for a in (vi.args or []))
    return f"{at_str} {vi.instruction}" + (f" {args_str}" if args_str else "")

'''
Helper Functions for Modifiers
'''
def parse_mod(mod) -> Tuple[bool, Optional["InstructionModifier"]]:
    """
    Accepts:
      - string: "<at> <patch...>"
      - InstructionModifier-like object: has .at and .patch (even if from another module)

    Returns:
      (ok, InstructionModifier | None)
    """
    # Duck-typed InstructionModifier
    if hasattr(mod, "at") and hasattr(mod, "patch"):
        try:
            patch = str(mod.patch).strip()
            return True, InstructionModifier(at=mod.at, patch=patch)
        except Exception as e:
            fastdyn_log.error(f"Unable to convert InstructionModifier-like object: {e}")
            return False, None

    if not isinstance(mod, str):
        fastdyn_log.error(f"Only string and InstructionModifier format accepted (got {type(mod).__name__})")
        return False, None

    s = mod.strip()
    if not s:
        return False, None

    parts = s.split()
    if len(parts) < 2:
        fastdyn_log.error(f"Invalid modifier line (need: '<at> <patch...>'): {mod!r}")
        return False, None

    at_tok = parts[0]
    patch_str = " ".join(parts[1:]).strip()
    return True, InstructionModifier(at=at_tok, patch=patch_str)

def resolve_mod(mod: "InstructionModifier", *, symbol_map: Dict[str, Any]) -> "InstructionModifier":
    sym_off_re = re.compile(r"^(.+?)([+-])(0x[0-9a-fA-F]+|\d+)$")
    hex_noprefix_re = re.compile(r"^[0-9a-fA-F]+$")
    # Parse patterns like: "r15 <- main", "pc = foo+4", etc.
    patch_re = re.compile(r"^(?P<lhs>\S+)\s*(?P<op><-|=|:=|->)\s*(?P<rhs>\S+)(?P<rest>.*)$")

    def is_numberish(tok: str) -> bool:
        t = tok.strip()
        if not t:
            return False
        if t.lower().startswith("0x"):
            return True
        if re.fullmatch(r"[+-]?\d+", t):  # -1, +1, 123
            return True
        return bool(hex_noprefix_re.fullmatch(t))  # bare hex

    def to_int(tok: str) -> int:
        t = tok.strip()
        if t.lower().startswith("0x"):
            return int(t, 16)
        if re.fullmatch(r"[+-]?\d+", t):
            return int(t, 10)
        return int(t, 16)  # bare hex

    def parse_symbol_offset(tok: str):
        t = tok.strip()
        m = sym_off_re.fullmatch(t)
        if not m:
            return t, 0
        sym = m.group(1).strip()
        sign = m.group(2)
        off = int(m.group(3), 0)
        return sym, (off if sign == "+" else -off)

    def normalize_map_value(v: Any) -> int:
        if isinstance(v, int):
            return v
        if isinstance(v, str):
            s = v.strip()
            if not s:
                raise ValueError("empty numeric string in symbol_map")
            if s.lower().startswith("0x") or re.fullmatch(r"[+-]?\d+", s):
                return int(s, 0)
            if hex_noprefix_re.fullmatch(s):
                return int(s, 16)
            return int(s, 0)  # last try
        return int(v)

    # --- resolve at ---
    if isinstance(mod.at, int):
        resolved_at = mod.at
    else:
        at_tok = str(mod.at).strip()
        if not at_tok:
            raise KeyError("Unresolved modifier.at: empty token")

        if is_numberish(at_tok):
            resolved_at = to_int(at_tok)
        else:
            sym, off = parse_symbol_offset(at_tok)
            if sym not in symbol_map:
                raise KeyError(f"Unresolved modifier.at symbol: {sym!r} (from {at_tok!r})")
            base = normalize_map_value(symbol_map[sym])
            if base & 1:
                base -= 1
            resolved_at = base + off

    # --- resolve patch ---
    patch = (mod.patch or "").strip()
    if not patch:
        return InstructionModifier(at=resolved_at, patch=patch)

    m = patch_re.fullmatch(patch)
    if m:
        lhs = m.group("lhs")
        op = m.group("op")
        rhs = m.group("rhs")
        rest = m.group("rest").strip()
        rest_toks = rest.split() if rest else []
    else:
        # fallback: keep old behavior if there's no operator
        toks = patch.split()
        if len(toks) < 2:
            return InstructionModifier(at=resolved_at, patch=patch)
        lhs, rhs = toks[0], toks[1]
        op = ""  # no operator found
        rest_toks = toks[2:]

    rhs_s = rhs.strip()

    # passthrough: pointer/memory/register/number
    if ("*" in rhs_s) or ("[" in rhs_s) or ("]" in rhs_s) or helper.is_cortexm_register(rhs_s) or is_numberish(rhs_s):
        parts = [lhs] + ([op] if op else []) + [rhs] + rest_toks
        new_patch = " ".join([p for p in parts if p]).strip()
        return InstructionModifier(at=resolved_at, patch=new_patch)

    # symbol resolve (supports sym+/-offset)
    sym, off = parse_symbol_offset(rhs_s)
    if sym in symbol_map:
        base = normalize_map_value(symbol_map[sym])
        rhs = hex(base + off)

    parts = [lhs] + ([op] if op else []) + [rhs] + rest_toks
    new_patch = " ".join([p for p in parts if p]).strip()
    return InstructionModifier(at=resolved_at, patch=new_patch)

def mod_to_string(mod: "InstructionModifier") -> str:
    at_str = f"0x{mod.at:x}" if isinstance(mod.at, int) else str(mod.at)
    patch_str = (mod.patch or "").strip()
    return f"{at_str} {patch_str}" if patch_str else at_str
