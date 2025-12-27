import logging
import tomli
from .machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier

from .utils import helper
from typing import Optional, Tuple

from . import fastdyn_log as fastdyn_log_conf

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

def load_symbol_addresses(self, filename: str) -> dict[str, int]:
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
