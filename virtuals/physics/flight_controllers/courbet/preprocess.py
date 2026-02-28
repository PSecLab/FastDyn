import re
import sys


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
    if re.fullmatch(r"[+-]?\d+", value):
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


def convert_config_file(symbols_dict: dict[str, int], input: str, output: str) -> bool:
    """
    Convert the virtuals/modifiers file content.
    """
    with open(input, "r") as input_file:
        with open(output, "w") as output_file:
            for line in input_file:
                line = line.strip()
                if not line:
                    continue
                tokens = line.split()

                # first token could be a symbol
                first_token = tokens[0]
                if is_number(first_token) == "none":
                    if first_token in symbols_dict:
                        first_token = hex(symbols_dict[first_token])
                    else:
                        print(f"Warning: {first_token} not found in symbols dictionary")
                        return False

                # second token will be the name of the virtual instruction, register, or memory location
                second_token = tokens[1]

                # third token if it exists could be a symbol
                third_token = None
                if len(tokens) > 2:
                    third_token = tokens[2]
                    if "*" in third_token or "[" in third_token or "]" in third_token or is_cortexm_register(third_token):
                        pass
                    elif is_number(third_token) == "none":
                        if third_token in symbols_dict:
                            third_token = hex(symbols_dict[third_token])
                        else:
                            print(f"Warning: {third_token} not found in symbols dictionary")
                            # return False

                output_file.write(f"{first_token} {second_token} {third_token if third_token else ''}\n")

    return True


# def convert_modifiers(symbols_dict: dict[str, int], modifier: str, modifier_out: str) -> int:
#     """
#     Convert the modifier file content.
#     """
#     with open(modifier, "r") as original:
#         with open(modifier_out, "w") as modified:
#             for line in original:
#                 line = line.strip()
#                 if not line:
#                     continue
#                 tokens = line.split()
#                 modified.write(line + "\n")
#             # print(tokens)
#             # Example: run number check on each token
#             # for tok in tokens:
#             #     kind = is_number(tok)
#                 # if kind != "none":
#                 #     print(f"  {tok} -> {kind}")
#     return 0


def preprocess(symbols_dict: dict[str, int], modifier_file: str, modifier_out: str, virtuals_file: str, virtuals_out: str):
    """
    Preprocess the modifier and virtuals files.
    """
    # print(f"Processing modifier file: {modifier_file}")
    convert_config_file(symbols_dict, modifier_file, modifier_out)
    # print(f"Output would be written to: {modifier_out}")

    # print(f"Processing virtuals file: {virtuals_file}")
    convert_config_file(symbols_dict, virtuals_file, virtuals_out)
    # print(f"Output would be written to: {virtuals_out}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python preprocess.py <map_file> <vehicle_dir>")
        sys.exit(1)

    map_file = sys.argv[1]
    vehicle_dir = sys.argv[2]
    labeled_virtuals_file = f"{vehicle_dir}/labeled_conf/virtuals.txt"
    labeled_modifier_file = f"{vehicle_dir}/labeled_conf/modifiers.txt"
    unlabeled_virtuals_out = f"{vehicle_dir}/unlabeled_conf/virtuals.txt"
    unlabeled_modifier_out = f"{vehicle_dir}/unlabeled_conf/modifiers.txt"

    symbols_dict = load_symbol_addresses(map_file)

    preprocess(symbols_dict, labeled_modifier_file, unlabeled_modifier_out, labeled_virtuals_file, unlabeled_virtuals_out)