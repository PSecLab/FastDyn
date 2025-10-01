'''
This file is responsible for parsing the config file to generate a python object.
'''
import logging
import tomli
import copy

from . import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

class Fastdyn_Config:
    def __init__(self):
        self.dev_config = None                 # If multiple dev config files, update this to a dict else this contains the pointer to info.

    def add_device_config(self, config_path):
        '''
        Opens the config file and parses it.
        '''
        try:
            with open(config_path, "rb") as f:
                config = tomli.load(f)
        except FileNotFoundError:
            init(autoreset=True)
            fastdyn_log.error(f"The file '{config_path}' was not found.")
        except tomli.TOMLDecodeError as e:
            init(autoreset=True)
            fastdyn_log.error(f"Error: Failed to parse TOML file '{config_path}': {e}")

        self.parse_dev_config(config)

    def parse_dev_config(self, config):
        parsed_config = DevConfig(config)       #object for the device configuration
        parsed_config.set_vals()

        self.dev_config = parsed_config

class DevConfig:
    def __init__(self,config):
        self.config = config
        self.memory = {}
        self.cpu = {}
        self.builtin_devs = {}
        self.devices_custom = {}

    def set_vals(self):
        self.cpu = self.config.get('CPU', {})
        self.memory = self.config.get('Memory', {})

        models_ranges, models_backend, self.builtin_devs = self.calculate_range(self.config.get('Device', {}))

        #add info for the backends
        for model, info in models_ranges.items():
            info['backend'] = models_backend[model]

        self.devices_custom = models_ranges


    def calculate_range(self, device_conf):
        model_ranges = {}
        models_info = {}
        builtin_model = {}
        for device_name, device_data in device_conf.items():
            if device_name == 'Models':
                models_info = device_data
                continue
            if not isinstance(device_data, dict):
                continue
            for handler in device_data.get('handlers', []):
                model = handler.get('model')
                # We only care about custom plugin models here
                if handler.get('enabled') is None or handler.get('enabled'):    #enabled parameter is optional
                    if model != 'qemu':
                        if model not in model_ranges:
                            model_ranges[model] = {}
                        if model == 'elder':
                            if handler.get('scroll') is not None:
                                model_ranges[model][device_name] = {"range": device_data.get('ranges', []), "scroll": handler.get('scroll')}
                            else:
                                fastdyn_log.error(f"No scroll/shared library passed for device {device_name} in model {model}")
                        else:
                            model_ranges[model][device_name] = {"range": device_data.get('ranges', [])}
                    else:
                        dev = handler.get('type')
                        if dev not in builtin_model:
                            builtin_model[dev] = {}
                            builtin_model[dev] = {"args": handler.get('args')}



        return model_ranges, models_info, builtin_model