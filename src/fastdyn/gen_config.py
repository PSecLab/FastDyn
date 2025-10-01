import json
import os

'''
Populating json for device config is straight forward, so no classes for this
'''
def _gen_dev_config(config, out_path):
    dev_config_data = config.dev_config.devices_custom
    dev_config_file = os.path.join(out_path, "dev_config.json")

    with open(dev_config_file, "w") as file:
        json.dump(dev_config_data, file, indent=4)

    return dev_config_file