import logging
import sys
from cmsis_svd.parser import SVDParser
from typing import Dict

from .utils import svd_parser
from .machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier, DeviceSection, DeviceModelDefaults, DeviceConfig, DeviceHandler, SlaveDevice
from .utils import parse_config as parse_helper
from . import machine_apis

from . import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

def parse_config(name: str, toml_config: str, map_file_path: str) -> Machine:
    machine = machine_apis.create_machine(name)
    #parse the svd file
    log.info("Parsing CMSIS-SVD")
    svd_file_map = svd_parser.discover_svd_files()

    fastdyn_log.info(f"Parsing Config file: {toml_config}")

    symbols_dict = None
    if map_file_path is not None:
        fastdyn_log.info(f"Parsing Config file: {map_file_path}")
        symbols_dict = parse_helper.load_symbol_addresses(map_file_path)
    else:
        fastdyn_log.warning(f"Map Config file not found")

    #parse the toml configuration
    parsed_config = parse_helper.toml_parser(toml_config)

    #populate the machine using toml
    machine_config = MachineConfigParser(machine)
    machine_config.update_cpu_info(parsed_config)

    #Initial Verification before running
    Platform = machine_config.machine.cpus[0].platform
    if Platform not in svd_file_map:
        log.error(f'{Platform} not found in the SVD File Map')
        sys.exit(1)

    parser = SVDParser.for_xml_file(svd_file_map[Platform])
    svd_device = parser.get_device()
    irq_map = parse_helper.create_svd_irq_map(svd_device)

    #Resolve virtual instruction and modifiers
    #TODO: Update this to handle multiple CPUs as well in future
    virtuals, modifiers = parse_helper.parse_vi_and_modifiers(parsed_config, irq_map, symbols_dict)
    machine_config.add_vi_and_modifiers(virtuals, modifiers)

    #Add memory for the machine
    machine_config.add_machine_memory(parsed_config)

    #TODO: Update this to handle multiple cpus as well
    device_info = parse_helper.parse_devices_info(parsed_config)
    machine_config.add_device_model(device_info)

    return machine_config.machine   #Fully implemented machine

class MachineConfigParser:
    def __init__(self, machine: Machine):
        self.machine = machine
        self.cpu_config = None

    def update_cpu_info(self, toml_config):
        for cpu in range(1):                          #TODO: Update to handle cases for multiple cpus
            cpu_config = machine_apis.create_cpu()
            for param in toml_config.get('CPU'):
                if param not in ["virtuals", "modifiers"]:
                    success = machine_apis.populate_cpu_config(cpu_config, param, toml_config['CPU'].get(param))
                    if not success:
                        log.error(f"Unable to Find Param: {param} in CPUConfig")
                        sys.exit()

            machine_apis.machine_add_cpu(self.machine, cpu_config)

    def add_vi_and_modifiers(self, virtuals, modifiers):
        for virtual in virtuals:
            ok = machine_apis.add_virtual_instruction(virtual, self.machine.cpus[0]) #TODO: Update to handle multi-cpu
            if not ok:
                fastdyn_log.error("Unable to add a virtual Instruction")
                sys.exit(1)

        for modifier in modifiers:
            machine_apis.add_modifier_instruction(modifier, self.machine.cpus[0]) #TODO: Update to handle multi-cpu
            if not ok:
                fastdyn_log.error("Unable to add a virtual Instruction")
                sys.exit(1)

    def add_device_model(self, device_info: DeviceConfig) -> Dict:
        #request api to transform to Fastdyn understandable format
        ok = machine_apis.compile_device_routing(device_section=device_info, machine=self.machine)
        if not ok:
            fastdyn_log.error("Unable to add device information")
            sys.exit(1)

    def add_machine_memory(self, toml_config):
        for param in toml_config.get('Memory'):
            success = machine_apis.populate_memory_config(self.machine.memory, param, toml_config['Memory'].get(param))
            if not success:
                log.error(f"Unable to Find Param: {param} in MemoryConfig")
                sys.exit()
