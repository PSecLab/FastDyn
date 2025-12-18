import re
from elftools.elf.elffile import ELFFile

def parse_symbol(s: str):
    """
    Parse a string into (symbol, offset).
    If string is just a symbol, offset is 0.
    If string is 'symbol+<num>', offset is the integer value.
    """
    match = re.fullmatch(r'([A-Za-z_][A-Za-z0-9_]*)(?:\+(\d+))?', s.strip())
    if not match:
        raise ValueError(f"Invalid input: {s}")

    symbol = match.group(1)
    offset = int(match.group(2)) if match.group(2) else 0
    return symbol, offset

def is_number(value: str) -> str:
    """
    Check if the given value is an integer, hex, or not a number.

    Returns:
        "hex" if value is hexadecimal,
        "int" if value is integer,
        "none" otherwise.
    """
    # Check for hex (0x prefix and valid hex digits)
    if re.fullmatch(r"0x[0-9A-Fa-f]+", value):
        return "hex"
    # Check for decimal integer
    if re.fullmatch(r"\d+", value):
        return "int"
    return "none"

def is_cortexm_register(value: str) -> bool:
    """
    Check if a string is a valid Cortex-M register.
    Valid ranges:
      - r0–r15
      - s0–s31
      - d0–d15
    """
    # Match r0–r15
    if re.fullmatch(r"r([0-9]|1[0-5])", value):
        return True
    # Match s0–s31
    if re.fullmatch(r"s([0-9]|[12][0-9]|3[01])", value):
        return True
    # Match d0–d15
    if re.fullmatch(r"d([0-9]|1[0-5])", value):
        return True
    return False

def extract_regs(expr: str):
    """
    Given a patch expression like 'r2 <- r3',
    return the left and right operands as strings.
    """
    parts = expr.split("<-")
    if len(parts) != 2:
        raise ValueError(f"Invalid expression: {expr}")
    left = parts[0].strip()
    right = parts[1].strip()
    return left, right

def elf_file_parser(elf_path: str) -> str:
    """
    Returns the VTOR base (as a hex string) to use as:
      -global armv7m.init-nsvtor=<value>
    """
    with open(elf_path, "rb") as f:
        ef = ELFFile(f)
        sec = ef.get_section_by_name(".isr_vector")
        if sec is None:
            raise ValueError("ELF has no .isr_vector section")
        return hex(int(sec["sh_addr"]))

def is_elf(path: str) -> bool:
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"
