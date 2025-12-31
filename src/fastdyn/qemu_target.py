import os, shutil
from .utils import helper
import subprocess

from . import fastdyn_log as fastdyn_log_conf
import logging
log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


#build qemu command
def build_qemu_cmd(machine_config, dev_config_path, out_path):
    """Builds the full qemu-system-arm command from the configuration."""
    #----------------------------------------QEMU & CPU configurations---------------------------------------
    cpu = machine_config.cpus[0]  #short-hand
    cmd = [cpu.qemu_path]

    cpu_configs = [
        "-machine", f"{cpu.machine},memory-backend=ram0",
        "-cpu", cpu.cpu,
        "-kernel", cpu.binary,
        "-qmp", f"unix:{cpu.qmp_socket},server,nowait",
        "-d", cpu.log_options,
        "-D", cpu.log_file,
        "-monitor", f"tcp:127.0.0.1:{cpu.monitor_port},server,nowait"
    ]

    if cpu.enable_gdb:
        log.info("GDB debugging enabled on Port 1234")
        cpu_configs.append('-s')
    if cpu.stop_on_start: cpu_configs.append('-S')
    if cpu.semihosting:
        cpu_configs.extend([
            "--semihosting",
            "--semihosting-config",cpu.semihosting_config
        ])

    #TODO: init_nsvtor will always be present, update the logic here
    if cpu.init_nsvtor:
        cpu_configs.extend(['-global', f'armv7m.init-nsvtor={cpu.init_nsvtor}'])
    else:
        if not helper.is_elf(cpu.binary):
            raise ValueError("Not an ELF (raw dump/bin). Need init_nsvtor in the toml configuration.")
        nsvtor_elf = helper.elf_file_parser(cpu.binary)
        cpu_configs.extend(['-global', f'armv7m.init-nsvtor={nsvtor_elf}'])

    cmd.extend(cpu_configs)

    #----------------------------------------Virtual & Modifier Instructions------------------------------------------
    virtuals_dir = os.path.join(out_path, 'virtuals')
    os.makedirs(virtuals_dir)
    virtuals_path = os.path.join(virtuals_dir, 'virtuals.txt')
    modifiers_path = os.path.join(virtuals_dir, 'modifiers.txt')

    log.info(f"Virtual Instructions available at {virtuals_path}")
    with open(virtuals_path, 'w') as file:
        file.writelines(cpu.virtuals)

    log.info(f"Modifier Instructions available at {modifiers_path}")
    with open(modifiers_path, 'w') as file:
        file.writelines(cpu.modifiers)

    #----------------------------------------Memory Configurations------------------------------------------
    # memory = machine_config.memories   #short-hand
    # print(memory)
    # ram0_path = os.path.join(memory.shared_mem_path, memory.main_ram_file)
    # ram1_path = os.path.join(memory.shared_mem_path, memory.shared_ram_file)

    # memory_configs = [
    #     '-object', f"memory-backend-file,id=ram0,mem-path={ram0_path},size={memory.main_ram_size},share=on",
    #     '-object', f"memory-backend-file,id=ram1,mem-path={ram1_path},size={memory.shared_ram_size},share=on",
    #     '-global', 'cortexm-soc.shram_backend=ram1',
    #     '-global', f'cortexm-soc.ram_baseaddr={memory.ram_base_addr}',
    #     '-global', f'cortexm-soc.shram_baseaddr={memory.shared_ram_base_addr}',
    # ]
    memory_config = []
    memories = machine_config.memories   #short-hand
    for memory in memories:
        curr_mem = memories[memory]
        # print(machine_config.memories[memory].memory_type)
        curr_config = [
            '-object', f"memory-backend-file,id={curr_mem.memory_name},mem-path={curr_mem.memory_file},size={curr_mem.memory_size},share=on",
        ]
        memory_config.extend(curr_config)
        extra_config = []
        if curr_mem.memory_name == "ram0":
            extra_config = [
            '-global', f'{cpu.machine}-soc.ram_baseaddr={curr_mem.memory_start}',
            ]
        elif curr_mem.memory_name == "ram1":
            extra_config = [
            '-global', f'{cpu.machine}-soc.shram_backend={curr_mem.memory_name}',
            '-global', f'{cpu.machine}-soc.shram_baseaddr={curr_mem.memory_start}',
            ]

        memory_config.extend(extra_config)

    cmd.extend(memory_config)

    #----------------------------------------Plugins Configurations------------------------------------------
    plugin_lib_path = cpu.plugin_library

    plugin_configs = [
        '--plugin',
    ]
    plugin_files = [
        f"{plugin_lib_path},dev={dev_config_path}",
        f'virtual={virtuals_path}',
        f'modifier={modifiers_path}',
        f"coverage={cpu.coverage}",
        f"finline={cpu.finline}"
    ]

    plugin_configs.extend([",".join(plugin_files)])
    cmd.extend(plugin_configs)
    # print(memory_config)
    # print(cmd)
    # import sys;sys.exit(1)
    gdb_cmd, launch_gdb, binary = get_gdb_cmd(machine_config, out_path)

    return cmd, gdb_cmd, launch_gdb, binary


#Get the gdb command based on the user request
def get_gdb_cmd(machine_config, out_path):
    launch_gdb = False
    #TODO: Handle multiple cpus
    cpu = machine_config.cpus[0]  #short-hand
    gdb_script_path = os.path.join(out_path, 'gdb_init.txt')
    with open(gdb_script_path, 'w') as f:
        f.write("target remote localhost:1234\n")
    binary = cpu.binary
    gdb_cmd = None
    if cpu.enable_gdb:
        launch_gdb = True
        if cpu.launch_gdb:
            gdb_cmd = [
                'xterm',
                '-e',
                f"gdb-multiarch -x {gdb_script_path} {binary}"
            ]
        else:
            gdb_cmd = None

    return launch_gdb, gdb_cmd, binary

def setup_qemu(machine, dev_config_path, work_dir=None):
    if work_dir is not None:
        if not os.path.isdir(work_dir):
            log.warn(f"The output directory: {work_dir} passed by the user does not exist.")
    else:
        work_dir = "fastdyn_work"

    if os.path.exists(work_dir):
        log.info(f"The output directory already exists at Path {os.path.abspath(work_dir)}. Deleting it!")
        shutil.rmtree(work_dir)

    log.info(f"Creating output directory at path: {os.path.abspath(work_dir)}")
    os.makedirs(work_dir)

    dev_config_path = helper.write_dev_config_json(output_dir=work_dir, data=machine.parsed_device)

    qemu_cmd, gdb_cmd, launch_gdb, binary = build_qemu_cmd(machine, dev_config_path, work_dir)


    return qemu_cmd, gdb_cmd, launch_gdb, binary

def start_execution(qemu_cmd, launch_gdb, gdb_cmd, binary):
    """
    Starts the actual execution of qemu,
    peripheral server with handlers to enable clean
    exiting
    """
    log.info("Running the following QEMU command:")
    print(" ".join(qemu_cmd))
    kill_qemu_process()
    qemu_proc = subprocess.Popen(qemu_cmd)
    log.info("Letting QEMU Run")

    if launch_gdb:
        if gdb_cmd is not None:
            subprocess.Popen(gdb_cmd)
        else:
            log.info(f'Connect by running: gdb-multiarch {binary}')

    try:
        qemu_proc.wait()
    except KeyboardInterrupt:
        kill_qemu_process()

def kill_qemu_process():
    PORT = 5555

    def get_pids_using_port(port):
        try:
            result = subprocess.check_output(["lsof", "-ti", f"tcp:{port}"])
            pids = result.decode().strip().split('\n')
            return [int(pid) for pid in pids if pid.strip()]
        except subprocess.CalledProcessError:
            return []

    def kill_pids(pids):
        for pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
                subprocess.run(["stty", "sane"])
            except ProcessLookupError:
                print(f"PID {pid} not found (may already be terminated)")
            except Exception as e:
                print(f"Failed to kill PID {pid}: {e}")

    pids = get_pids_using_port(PORT)
    if pids:
        kill_pids(pids)