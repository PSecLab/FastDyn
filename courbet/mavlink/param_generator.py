import json

def get_params_with_range_or_values(json_data):
    """
    Returns a dict of parameters (top-level only) that have
    either a 'Range' or 'Values' field.
    """
    result = {}

    # If your JSON has a wrapper like {"": {...}}, flatten it first
    if isinstance(json_data, dict) and "" in json_data:
        json_data = json_data[""]

    for param_name, param_data in json_data.items():
        if isinstance(param_data, dict):
            # if "Range" in param_data or "Values" in param_data:
            result[param_name] = param_data

    return result


if __name__ == "__main__":
    # Load the JSON file
    with open("apm.pdef.json", "r") as f:
        data = json.load(f)

    params = get_params_with_range_or_values(data)

    print(f"Found {len(params)} parameters with Range or Values:\n")
    for name in params:
        print(name)
        print(json.dumps(params[name], indent=2))