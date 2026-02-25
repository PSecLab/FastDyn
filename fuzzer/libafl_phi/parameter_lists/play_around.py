"""
parse a json array of rover-4.6 parameter descriptions
"""
import json

def get_parameter_names(file_path: str) -> (dict[str, list[str]], list[str]):
    with open(file_path, 'r') as f:
        data = json.load(f)

    param_names: dict[str, list[str]] = {}
    top_level_keys = data.keys()

    # Top-level keys are groups like "AFS_", "ATC_", etc.
    for group_name, group_params in data.items():
        if group_name.startswith("SIM_"):
            continue
        # Each group contains a dict of parameter_name -> metadata dict
        for param_name, param_dict in group_params.items():
            # Heuristic: every parameter should contain a "Description" field
            if isinstance(param_dict, dict) and "Description" in param_dict:
                # do not add if param_name begins with "SIM_"
                if not param_name.startswith("SIM_"):
                    param_names.setdefault(group_name, []).append(param_name)

    return param_names, top_level_keys

def get_parameter_block_text(file_path: str, target_param: str) -> str | None:
    """
    Scan the file as raw text, find `target_param`,
    and return the full matching {...} block that follows it.
    Returns None if the parameter cannot be found.
    """

    with open(file_path, "r") as f:
        text = f.read()

    # Find the location of the parameter name in quotes
    key = f'"{target_param}"'
    start = text.find(key)
    if start == -1:
        return None  # parameter not found

    # Find the first '{' after the parameter name
    brace_start = text.find("{", start)
    if brace_start == -1:
        return None  # malformed file

    # Walk forward and match nested braces
    depth = 0
    for i in range(brace_start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                # Return everything from the first '{' to the matching '}'
                return text[brace_start : i + 1]

    return None  # unmatched braces (malformed file)


if __name__ == "__main__":
    param_descriptions, top_level_keys = get_parameter_names('rover-4.6.json')
    # for desc in param_descriptions:
    #     print(desc)
    for key in top_level_keys:
        print(key)

    for group, params in param_descriptions.items():
        print(f"{group}: {len(params)} parameters")

    # Example: get the block text for a specific parameter
    block_text = get_parameter_block_text('rover-4.6.json', 'CRUISE_SPEED')
    if block_text:
        print(f"Block text for CRUISE_SPEED:\n{block_text}")
    else:
        print("CRUISE_SPEED parameter not found.")
