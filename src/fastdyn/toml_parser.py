import sys
import tomli
import logging

from fastdyn.fastdyn import *
from . import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

#parse a single toml per machine, use as many instances as you want in future to parse multiple machines
def load_toml_config(config_path):
    try:
        with open(config_path, "rb") as f:
            return tomli.load(f)
    except FileNotFoundError:
        fastdyn_log.error(f"The file '{config_path}' was not found.")
        raise
    except tomli.TOMLDecodeError as e:
        fastdyn_log.error(f"Error: Failed to parse TOML file '{config_path}': {e}")
        raise

def parser(out_dir, machine_name, toml_config, svd_path):
    #parse the toml configuration
    fastdyn_log.info(f"Parsing Config file: {toml_config}")

    fastdyn_handle = Fastdyn()

    #parse the toml and the fastdyn_handle
    toml_parser = TomlParser(toml_config, fastdyn_handle)

    #right now, we support per machine config
    machine0 = fastdyn_handle.create_machine(machine_name=machine_name,
                                    platform=toml_parser.machine_info.get("platform")
                                    )

    machine0.add_rehosting_info(toml_parser.rehosting_info)

    # additional machine params if set by the user related to qemu target
    q = machine0.qemu_target_opts

    q.qemu_path          = toml_parser.machine_info.get("qemu_path", "qemu-system-arm")

    q.enable_gdb         = toml_parser.machine_info.get("enable_gdb", False)
    q.gdb_port           = toml_parser.machine_info.get("gdb_port", 1234)
    q.stop_on_start      = toml_parser.machine_info.get("stop_on_start", False)
    q.launch_gdb         = toml_parser.machine_info.get("launch_gdb", False)

    q.semihosting        = toml_parser.machine_info.get("semihosting", True)
    q.semihosting_config = toml_parser.machine_info.get("semihosting_config", "enable=on,target=native")

    q.monitor_port       = toml_parser.machine_info.get("monitor_port", 5555)
    q.qmp_socket         = toml_parser.machine_info.get("qmp_socket", "/tmp/qmp.sock")

    q.coverage           = toml_parser.machine_info.get("coverage", False)
    q.finline            = toml_parser.machine_info.get("finline", None)
    if isinstance(q.finline, str) and q.finline.strip().lower() == "none":
        q.finline = None
    q.print_command       = toml_parser.machine_info.get("print_command", False)
    q.reset_memory_files  = toml_parser.machine_info.get("reset_memory_files", False)

    #add cmsis svd if Platform name provided by the user
    if toml_parser.machine_info.get("platform") is not None:
        machine0.add_cmsis_svd(cmsis_svd=svd_path)



    #add cpus information per machine
    cpus = []
    for idx, cpu in enumerate(toml_parser.cpus_info):
        curr_cpu = toml_parser.cpus_info[cpu][0]

        cpu_obj = machine0.add_cpu(
                arch=curr_cpu.get("arch", "arm"),
                machine=curr_cpu.get("machine", "cortexm"),
                cpu=curr_cpu.get("cpu", "cortex-m4"),
                binary=curr_cpu['binary'],  #mandatory else throw error
                init_nsvtor= curr_cpu.get("init_nsvtor", None),  #handle by the target to retreive correct value from binary
                twintrace = curr_cpu.get("twintrace", None),
                hardware_trace = curr_cpu.get("hardware_trace", None),
                introspect = curr_cpu.get("introspect", False),
                exstng_config_path = curr_cpu.get("existing_config_path", False),
                )

        #additional params if set by the user
        cpu_obj.plugin_library  =   curr_cpu.get('plugin_library', 'build/libfastdyn.so')
        cpu_obj.monitor_elf     =   curr_cpu.get('monitor_elf', '../ws/monitor.elf')
        cpu_obj.symbol_file     =   curr_cpu.get('symbol_file', None)
        cpu_obj.optifuzz        =   curr_cpu.get('optifuzz', False)
        cpu_obj.loader_addr     =   curr_cpu.get('loader_addr', None)

        #symbol resolution per cpu
		#if curr_cpu.get("map_file") is not None:
		#	cpu_obj.add_map_file(curr_cpu.get("map_file"))



        #add virtual instructions per cpu
        if curr_cpu.get("virtuals"):
            for value in curr_cpu.get("virtuals"):
                curr_vi = VirtualInstruction(
                    at=value['at'],
                    instruction=value['instruction'],
                    args=value['args']
                )
                cpu_obj.add_virtual_instruction(curr_vi)

        #add modifiers per cpu
        if curr_cpu.get("modifiers"):
            for value in curr_cpu.get("modifiers"):
                curr_modifier = InstructionModifier(
                    at=value['at'],
                    patch=value['patch']
                )
                cpu_obj.add_modifier(curr_modifier)

        cpus.append(cpu_obj)

    #add machine memory
    #make sure to first add main memory to avoid errors
    try:
        curr_mem = toml_parser.memory_info.pop('main')
    except:
        fastdyn_log.error("Unable to retreive Main Memory")
        raise KeyError("Main Memory required with the name 'main' in the toml configuration")

    machine0.add_memory(memory_name='main',
                        memory_id = curr_mem['id'],
                        memory_start = curr_mem['base_address'],
                        memory_size=curr_mem['memory_size'],
                        memory_type=curr_mem['memory_type'],
                        backend      = curr_mem['backend'],          # file | ram | memfd
                        memory_file=curr_mem['memory_file'],
                        share = curr_mem['share'],
                        )

    #add additional memories added by the user in the toml config
    for memory in toml_parser.memory_info:
        curr_mem = toml_parser.memory_info.get(memory)[0]
        machine0.add_memory(memory_name=memory,
                            memory_id = curr_mem['id'],
                            memory_start = curr_mem['base_address'],
                            memory_size=curr_mem['memory_size'],
                            memory_type=curr_mem['memory_type'],
                            backend      = curr_mem['backend'],          # file | ram | memfd
                            memory_file=curr_mem['memory_file'],
                            share = curr_mem['share'],
                            )

    #add devices information

    #get the available models for the user
    for model in toml_parser.devices_info.get('Models'):
        #parse device specific info
        model_info = toml_parser.devices_info.get('Models')[model]
        backend = model_info.get('backend')
        #add model and its info to the devices
        machine0.add_model(
            name=model,
            backend=backend
        )
    toml_parser.devices_info.pop('Models')

    #add devices added by the user
    for device in toml_parser.devices_info:
        device_info = toml_parser.devices_info[device]

        #create a device
        device_handler = machine0.add_device(device)

        #add the handlers info
        for handler in device_info.get('handlers'):
            device_handler.add_handler(
                name=handler['model'],
                enabled=handler['enabled'],
                args=handler.get('args', None),
                scroll=handler.get('scroll',None),
                type=handler.get('type', None)
            )

        # Reject configs that enable more than one hardware-talking
        # backend on the same device range. passthrough and twintrace
        # both forward MMIO to the probe, so enabling both means every
        # firmware write becomes two probe writes to the same address
        # (corrupts clear-on-write registers, FIFO data registers,
        # etc.). Pick exactly one.
        HW_BACKENDS = {"passthrough", "twintrace"}
        hw_enabled = [h.model for h in device_handler.handlers
                      if h.enabled and h.model in HW_BACKENDS]
        if len(hw_enabled) > 1:
            fastdyn_log.error(
                f"[Device.{device}] enables multiple hardware-talking "
                f"backends on the same range: {hw_enabled}. "
                f"passthrough and twintrace both issue MMIO to the "
                f"physical probe; enabling both duplicates every write. "
                f"Set 'enabled = false' on all but one."
            )
            sys.exit(1)

        #add device ranges
        for range in device_info.get('ranges'):
            device_handler.add_ranges(
                start=range[0],
                end=range[1]
            )

        #add slaves in case of I2C and SPI
        slaves_list = device_info.get('slaves', [])
        for slave_entry in slaves_list:
            # Passes the whole dict: {'device': 'BM2E80', 'address': '0x76', ...}
            device_handler.add_slave(slave_entry)

        #add irqs if added by the user
        if device_info.get('irq') is not None:
            for irq in device_info.get('irq'):
                device_handler.add_irq(irq)

    return fastdyn_handle

class TomlParser:
    def __init__(self, config_path, fastdyn_handle):
        self.config_path    = config_path
        self.parsed_config  = self.toml_parser(config_path)
        self.fastdyn_handle = fastdyn_handle
        self.machine_info   = self.parsed_config.get("Machine")
        self.rehosting_info = self.parsed_config.get("Rehosting", {}) or {}
        self.cpus_info      = self.parsed_config.get("CPU")
        self.devices_info   = self.parsed_config.get("Device")
        self.memory_info    = self.parsed_config.get("Memory")

    def toml_parser(self, config_path):
        return load_toml_config(config_path)
