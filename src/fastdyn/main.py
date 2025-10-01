'''
Main file is responsible for kicking the qemu command.
'''
import logging
import argparse
import os, shutil
import subprocess

from dotenv import load_dotenv


from . import gen_config    #generate the files for the configs.
from . import parse_config  #Parse the config
from . import fastdyn_log


log = logging.getLogger(__name__)
fastdyn_log.setLogConfig()


#build qemu command
def build_qemu_cmd(config, dev_config_path):
    """Builds the full qemu-system-arm command from the configuration."""
    #----------------------------------------QEMU & CPU configurations---------------------------------------
    cpu = config.dev_config.cpu  #short-hand

    cmd = [cpu.get('qemu_path')]

    cpu_configs = [
        "-machine", f"{cpu['machine']},memory-backend=ram0",
        "-cpu", cpu['cpu'],
        "-kernel", cpu['binary'],
        "-qmp", f"unix:{cpu['qmp_socket']},server,nowait",
        "-d", cpu['log_options'],
        "-D", cpu['log_file'],
        "-monitor", f"tcp:127.0.0.1:{cpu['monitor_port']},server,nowait"
    ]

    if cpu.get('enable_gdb'):
        log.info("GDB debugging enabled on Port 1234")
        cpu_configs.append('-s')
    if cpu.get('stop_on_start'): cpu_configs.append('-S')
    if cpu.get('semihosting'):
        cpu_configs.extend([
            "--semihosting",
            "--semihosting-config",cpu.get('semihosting_config', "enable=on,target=native")
        ])
    cmd.extend(cpu_configs)

    #----------------------------------------Memory Configurations------------------------------------------
    memory = config.dev_config.memory   #short-hand
    ram0_path = os.path.join(memory.get('shared_mem_path', '/dev/shm'), memory.get('main_ram_file'))
    ram1_path = os.path.join(memory.get('shared_mem_path', '/dev/shm'), memory.get('shared_ram_file'))

    memory_configs = [
        '-object', f"memory-backend-file,id=ram0,mem-path={ram0_path},size={memory.get('main_ram_size')},share=on",
        '-object', f"memory-backend-file,id=ram1,mem-path={ram1_path},size={memory.get('shared_ram_size')},share=on",
        '-global', 'cortexm-soc.shram_backend=ram1',
        '-global', f'cortexm-soc.ram_baseaddr={memory.get("ram_base_addr")}',
        '-global', f'cortexm-soc.shram_baseaddr={memory.get("shared_ram_base_addr")}',
        '-global', f'armv7m.init-nsvtor={memory.get("init_nsvtor")}',
    ]

    cmd.extend(memory_configs)

    #----------------------------------------Plugins Configurations------------------------------------------
    plugin_lib_path = cpu.get('plugin_library', './build/libfastdyn.so')

    plugin_configs = [
        '-plugin', f"{plugin_lib_path},dev={dev_config_path}",
    ]

    cmd.extend(plugin_configs)

    return cmd







#This function is responsible for running the qemu command based on the inputs
def run_qemu(config, out_path):
    if out_path is not None:
        if not os.path.isdir(out_path):
            log.warn(f"The output directory: {out_path} passed by the user does not exist.")
    else:
        out_path = "tmp"

    if os.path.exists(out_path):
        log.info(f"The output directory already exists at Path {os.path.abspath(out_path)}. Deleting it!")
        shutil.rmtree(out_path)

    log.info(f"Creating output directory at path: {os.path.abspath(out_path)}")
    os.makedirs(out_path)

    #create json file for the device config
    dev_config_path = gen_config._gen_dev_config(config, out_path)
    log.info(f"Custom Devices Configuration written to : {dev_config_path}")

    cmd = build_qemu_cmd(config, dev_config_path)

    _start_execution(cmd)

def _start_execution(qemu_cmd):
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


def main():
    # Load variables from .env
    load_dotenv()
    parser = argparse.ArgumentParser(
        description="Parse, display, and generate configurations from a FastDyn TOML file.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "-c", "--config-path",
        required=True,
        help="Path to the TOML configuration file."
    )
    parser.add_argument(
        "-m","--map-file",
        default=None,
        help="Path to the symbol map file.")

    parser.add_argument(
        "-o", "--output",
        metavar="OUTPUT_DIR",
        help="Directory to place the generated files (default: './out')."
    )

    args = parser.parse_args()

    config = parse_config.Fastdyn_Config()  #generate the object for the config

    log.info(f"Parsing Config file: {args.config_path}")
    config.add_device_config(args.config_path)


    run_qemu(
        config=config,
        out_path=args.output
    )


if __name__ == "__main__":
    main()