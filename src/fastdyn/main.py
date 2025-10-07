'''
Main file is responsible for kicking the qemu command.
'''
import logging
import click
import os, shutil
import subprocess

from dotenv import load_dotenv


from . import gen_config    #generate the files for the configs.
from . import parse_config  #Parse the config
from . import fastdyn_log
from fastdyn.__init__ import __version__
from .verifier import verifier as verify             #contains the verification framework
from .verifier import prompt_gen as pg           #Generates the prompt
from .verifier import context_minimizer as cm   #Minimizes the context

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
        '--plugin', f"{plugin_lib_path},dev={dev_config_path}",
    ]

    cmd.extend(plugin_configs)

    return cmd

#This function is responsible for running the qemu command based on the inputs
def run_qemu(config, out_path):
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


@click.group()
@click.version_option(prog_name="Fastdyn Framework",version=__version__)
def cli():
    # Load variables from .env
    load_dotenv()
    log.info('****** Fastdyn Framework {0} *******'.format(__version__ ))


@cli.command('run',help= 'Runs the firmware on QEMU using the passed config file.')
@click.option('-c','--config',required = True, type= click.Path(resolve_path=True,exists=True),
                        help='The Path to the config file.',
                        metavar= 'PATH')
@click.option('-o','--work-dir',default="./fastdyn_work",metavar='PATH',
        show_default=True,
        type=click.Path(resolve_path=True,writable=True),
        help='Path to the work directory.')
def run(config, work_dir):
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

    config_obj = parse_config.Fastdyn_Config()  #generate the object for the config

    log.info(f"Parsing Config file: {config}")
    config_obj.add_device_config(config)

    run_qemu(
        config=config_obj,
        out_path=work_dir
    )

@cli.command(
    'generate',
    help='Generates the LLM Prompt using the hardware log passed by the user. '
         '[Disclaimer: Use this when generating a model for the first time]'
)
@click.option(
    '-hw', '--hardware-log',
    required=True,
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the log file generated when running the firmware on hardware.',
    metavar='PATH'
)
@click.option(
    '-b', '--board',
    type=str,
    required=True,
    help='Name of the platform on which the firmware is running.'
)
@click.option(
    '-p', '--peripheral',
    type=str,
    required=True,
    help='Name of the peripheral targeted for verification.'
)
@click.option(
    '--method',
    default='ngram',
    show_default=True,
    type=click.Choice(['ngram', 'window', 'other'], case_sensitive=False),
    help='Context minimization method.'
)
@click.option(
    '--n',
    type=int,
    default=2,
    show_default=True,
    help='Value of n for n-gram method.'
)
@click.option(
    '--isr-window',
    type=int,
    default=10000000,
    show_default=True,
    help='ISR window size.'
)
@click.option(
    '-o', '--work-dir',
    default="./fastdyn_work",
    show_default=True,
    metavar='PATH',
    type=click.Path(resolve_path=True, writable=True),
    help='Path to the work directory.'
)
def generate(hardware_log, board, peripheral, method, n, isr_window, work_dir):
    """Generates the LLM Prompt using the hardware log passed by the user."""
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

    #minimize the context
    cm_path = cm.minimize_context(
        out_dir=work_dir,
        log_file=hardware_log,
        platform=board,
        method=method,
        peripheral=peripheral,
        n=n,
        isr_window=isr_window,
        cm_dir_name="out_cm"
        )


    #generate the prompt
    pg_path = pg.initial_prompt_gen(
        analysis_dir=cm_path,
        peripheral=peripheral,
        out_dir=work_dir
    )

@cli.command(
    'verifier',
    help=(
        'Verifies the model passed by the user for a given peripheral. '
        'Generates a prompt in case of failure. '
        '[Disclaimer: Use this when iterating a generated model for correction]'
    )
)
@click.option(
    '-hw', '--hardware-log',
    required=True,
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the log file generated when running the firmware on hardware.',
    metavar='PATH'
)
@click.option(
    '-em', '--emulation-log',
    required=True,
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the log file generated when running the firmware on the elder model using emulation.',
    metavar='PATH'
)
@click.option(
    '-d', '--dev-model',
    required=True,
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the generated model file to be verified',
    metavar='PATH'
)
@click.option(
    '-b', '--board',
    type=str,
    required=True,
    help='Name of the platform on which the firmware is running.'
)
@click.option(
    '-p', '--peripheral',
    type=str,
    required=True,
    help='Name of the peripheral targeted for verification.'
)
@click.option(
    '--method',
    default='ngram',
    show_default=True,
    type=click.Choice(['ngram', 'window', 'other'], case_sensitive=False),
    help='Context minimization method.'
)
@click.option(
    '--n',
    type=int,
    default=2,
    show_default=True,
    help='Value of n for n-gram method.'
)
@click.option(
    '--isr-window',
    type=int,
    default=10000000,
    show_default=True,
    help='ISR window size.'
)
@click.option(
    '-o', '--work-dir',
    default="./fastdyn_work",
    show_default=True,
    metavar='PATH',
    type=click.Path(resolve_path=True, writable=True),
    help='Path to the work directory.'
)
def verifier(hardware_log, emulation_log, dev_model, board, peripheral, method, n, isr_window, work_dir):
    """Verifies the model against hardware and emulation logs."""
    log.info("Running Verifier")
    #minimize the context for hardware log
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

    #minimize the context--hardware
    cm_path_hardware = cm.minimize_context(
        out_dir=work_dir,
        log_file=hardware_log,
        platform=board,
        method=method,
        peripheral=peripheral,
        n=n,
        isr_window=isr_window,
        cm_dir_name="hardware"
        )

    #minimize the context--emulation
    cm_path_emulation = cm.minimize_context(
        out_dir=work_dir,
        log_file=emulation_log,
        platform=board,
        method=method,
        peripheral=peripheral,
        n=n,
        isr_window=isr_window,
        cm_dir_name="emulation"
        )

    #compare the automatas
    #diff_obj-> the difference object which contains the information about the differences in the automatas
    not_match, diff_obj = verify.verify_automata(automata1=cm_path_hardware, automata2=cm_path_emulation, peripheral=peripheral)

    #generate a prompt or tell the user, everything worked perfectly
    if not_match:
        log.warn("Log mismatch! Generating prompt")
        #generate the prompt
        #use the difference from the
        pg_path = pg.iteration_prompt_gen(
            diff_obj=diff_obj,
            device_model_path=dev_model,
            peripheral=peripheral,
            out_dir=work_dir,
        )
    else:
        log.info("Both hardware log and emulation log matched!")

if __name__ == "__main__":
    cli()