'''
This file is responsible for parsing the config file to generate a python object.
'''
import logging
import tomli
import copy

from cmsis_svd.parser import SVDParser
from .utils.helper import *

from . import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

class Fastdyn_Config:
    def __init__(self):
        self.dev_config     = None                 # If multiple dev config files, update this to a dict else this contains the pointer to info.
        self.symbol_dict    = None
        self.svd_file_map   = None
        self.virtual_instr  = []
        self.modifier_instr = []

    def add_device_config(self, config_path, map_file, svd_file_map):
        '''
        Opens the config file and parses it.
        '''
        self.svd_file_map = svd_file_map

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

        '''
        Parse the map file
        '''
        self.symbols_dict = self.load_symbol_addresses(map_file)

        '''
        Create IRQ map using the SVD File Map
        '''
        platform = self.dev_config.cpu['platform']
        parser = SVDParser.for_xml_file(svd_file_map[platform])
        svd_device = parser.get_device()
        self.irq_map = self.create_svd_irq_map(svd_device)

        '''
        Parse the virtual and modifier instructions
        '''

        self.parse_vi_instr(config)

    def parse_vi_instr(self, config):
        cpu_conf = config.get('CPU', {})

        virtuals = cpu_conf.get('virtuals', [])
        if virtuals:
            virtuals_ir = []
            for virt in virtuals:
                args_str = " ".join(virt.get('args', []))
                if (virt.get('instruction') == "raise_irq"):
                    if args_str in self.irq_map:
                        args_str = str(self.irq_map[args_str])
                    else:
                        # check if it's already a number
                        if not args_str.isdigit():
                            log.error("Invalid Interrupt for IRQ")
                            sys.exit()
                virtuals_ir.append(f"{virt.get('at')} {virt.get('instruction')} {args_str}\n")
            self.virtual_instr = self.convert_config_file(virtuals_ir)

        modifiers = cpu_conf.get('modifiers', [])
        if modifiers:
            modifiers_ir = []
            for mod in modifiers:
                lhs, rhs = extract_regs(mod.get('patch'))
                modifiers_ir.append(f"{mod.get('at')} {lhs} {rhs}\n")
            self.modifier_instr = self.convert_config_file(modifiers_ir)


    def create_svd_irq_map(self, svd_device):
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

    def parse_dev_config(self, config):
        parsed_config = DevConfig(config)       #object for the device configuration
        parsed_config.set_vals()

        self.dev_config = parsed_config

    def convert_config_file(self, input_lines: list[str]) -> list[str]:
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
            if is_number(first_token) == "none":
                symbol, offset = parse_symbol(first_token)
                if symbol in self.symbols_dict:
                    resolved_address = self.symbols_dict[symbol]
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
                if "*" in third_token or "[" in third_token or "]" in third_token or is_cortexm_register(third_token):
                    pass
                elif is_number(third_token) == "none":
                    symbol, offset = parse_symbol(third_token) # parse symbol and offset
                    if symbol in self.symbols_dict:
                        # Here we assume offset is 0 for the third token if it's just a symbol
                        if offset != 0:
                             fastdyn_log.warn(f"Offset for third token '{third_token}' is not supported. Treating as symbol only.")
                        third_token = hex(self.symbols_dict[symbol])
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
            info['backend'] = models_backend[model].get('backend')

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
                            model_ranges[model]["overall"] = []
                        model_ranges[model]["overall"].extend(device_data.get('ranges', []))
                        if model == 'elder':
                            if handler.get('scroll') is not None:
                                model_ranges[model][device_name] = {"range": device_data.get('ranges', []), "scroll": handler.get('scroll'), "irq":device_data.get('irq',"")}
                            else:
                                fastdyn_log.error(f"No scroll/shared library passed for device {device_name} in model {model}")
                        else:
                            model_ranges[model][device_name] = {"range": device_data.get('ranges', []), "irq":device_data.get('irq',"")}
                    else:
                        dev = handler.get('type')
                        if dev not in builtin_model:
                            builtin_model[dev] = {}
                            builtin_model[dev] = {"args": handler.get('args')}



        return model_ranges, models_info, builtin_model