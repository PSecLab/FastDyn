'''
Main file is responsible for kicking the qemu command.
'''
import logging
import click
import os, shutil
import subprocess
import signal
import sys

from . import fastdyn_log
from fastdyn.__init__ import __version__
# from .machine import Machine, CPUConfig, VirtualInstruction, InstructionModifier
from .verifier import verifier as verify             #contains the verification framework
from .verifier import prompt_gen as pg           #Generates the prompt
from .verifier import context_minimizer as cm   #Minimizes the context
from .utils import helper
from . import toml_parser

from dataclasses import asdict

log = logging.getLogger(__name__)
fastdyn_log.setLogConfig()

@click.group()
@click.version_option(prog_name="Fastdyn Framework",version=__version__)
def cli():
    log.info('****** Fastdyn Framework {0} *******'.format(__version__ ))


@cli.command('run',help= 'Runs the firmware on QEMU using the passed config file.')
@click.option('-c','--config',required = True, type= click.Path(resolve_path=True,exists=True),
                        help='The Path to the config file.',
                        metavar= 'PATH')
@click.option(
    '-m', '--map-file',
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the symbol map file.',
    default=None,
    metavar='PATH'
)
@click.option('-o','--work-dir',default="./fastdyn_work",metavar='PATH',
        show_default=True,
        type=click.Path(resolve_path=True,writable=True),
        help='Path to the work directory.')
def run(config, map_file, work_dir):
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

    #It will parse the config and create a handle using fastdyn.py apis that has all the info about the machines and cpus listed in the toml
    fastdyn_handle = toml_parser.parser(machine_name="machine0",toml_config=config, svd_path="third_party/cmsis-svd-data")

    #run all the machines requested by the user
    for idx, machine in enumerate(fastdyn_handle.machines):
        fastdyn_handle.run(machine_name=f"machine{idx}",
                           target="qemu",
                           out_path=work_dir
                           )

@cli.command(
    'generate',
    help='Generates the LLM Prompt using the hardware log passed by the user. '
         '[Disclaimer: Use this when generating a model for the first time]'
)
@click.option(
    '-hw', '--hardware-log',
    required=False,
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the log file generated when running the firmware on hardware.',
    metavar='PATH'
)
@click.option(
    '-sm', '--slave-model',
    is_flag=True,
    help='Generate a slave model instead of the main model.'
)
@click.option(
    '-rm', '--reference-model',
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the reference model file.',
    metavar='PATH'
)
@click.option(
    '-fc', '--firmware-code',
    type=click.Path(resolve_path=True, exists=True),
    help='Path to the firmware source code for slave model.',
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
def generate(hardware_log, slave_model, reference_model, firmware_code, board, peripheral, method, n, isr_window, work_dir):
    """Generates the LLM Prompt using the hardware log passed by the user."""
    if slave_model:
        if reference_model is None:
            raise click.UsageError("--slave-model requires --reference-model.")
        if firmware_code is None:
            raise click.UsageError("--slave-model requires --firmware-code.")

        if work_dir is not None:
            if not os.path.isdir(work_dir):
                log.warn(f"The output directory: {work_dir} passed by the user does not exist.")
        else:
            work_dir = "fastdyn_work"

        #generate the prompt
        pg_path = pg.slave_model_gen(
            peripheral_name=peripheral,
            platform_name = board,
            out_dir=work_dir,
            slave_firmware_path=firmware_code,
            reference_model_path=reference_model
        )

    else:
        if hardware_log is None:
            raise click.UsageError("Generating the model required --hardware-log is required.")

        if work_dir is not None:
            if not os.path.isdir(work_dir):
                log.warning(f"The output directory: {work_dir} passed by the user does not exist.")
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
