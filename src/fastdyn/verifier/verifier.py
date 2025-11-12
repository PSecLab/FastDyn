import os, sys
import logging
import re
from .. import fastdyn_log as fastdyn_log_conf
from collections import defaultdict
from typing import Dict

fastdyn_log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

class DiffLog:
    def __init__(self):
        self.not_match = False
        self.diff_entropy_data = None
        self.diff_entropy = None
        self.data_registers = []
        self.diff_loop_pattern_data = None
        self.diff_init_data = None
        self.diff_init = None
        self.diff_state_data = None
        self.platform_name = None

#This function will compare the automata for the given automatas...
#Will only compare the automata for the peripheral of interest.
def verify_automata(automata1, automata2, peripheral):
    periph_hw = os.path.join(automata1, peripheral)
    periph_em = os.path.join(automata2, peripheral)

    differential_data = DiffLog()

    if not os.path.exists(periph_hw):
        fastdyn_log.error(f"Peripheral {peripheral} does not exist under the {automata1} directory!")
        sys.exit(0)

    if not os.path.exists(periph_em):
        fastdyn_log.error(f"Peripheral {peripheral} does not exist under the {automata2} directory!")
        sys.exit(0)

    #parse platform name
    differential_data.platform_name = parse_summary_file(os.path.join(automata1, 'summary.txt')).get('Platform')

    #check the entropy file and see if there is any data field, if yes, ignore the data field for verification
    fastdyn_log.info('Performing Verification for the Entropy')
    diff_entropy_calculate(differential_data, periph_hw, periph_em)

    #Check the init pattern
    fastdyn_log.info('Performing Verification for the Init Pattern')
    diff_init_pattern(differential_data, periph_hw, periph_em)

    #Check the loop patterns
    fastdyn_log.info('Performing Verification for the Loops Pattern')
    diff_loop_pattern(differential_data, periph_hw, periph_em)

    #Check the stateful analysis behaviour
    fastdyn_log.info('Performing Verification for the Stateful Analysis data')
    diff_stateful_analysis(differential_data, periph_hw, periph_em)

    #Check the data registers from the log file
    fastdyn_log.info('Performing Verification for the Data Registers')
    diff_runtime_trace_analysis(differential_data, periph_hw, periph_em)

    #Check the ISR Analysis
    fastdyn_log.info('Performing Verification for the ISRs')
    diff_isr_analysis(differential_data, periph_hw, periph_em)

    return differential_data.not_match, differential_data

def diff_isr_analysis(diff_data, periph_hw, periph_em):
    '''
    Lazy analysis -> Just patch the isr analysis instead of any comparison
    '''
    isr_hw_path = os.path.join(periph_hw, 'isr_analysis.txt')
    isr_em_path = os.path.join(periph_em, 'isr_analysis.txt')

    if not os.path.exists(isr_hw_path):
        diff_data.isr_analysis_data = ''
        #search over, as isr file does not exists
        return

    isr_hw = parse_irq_file(isr_hw_path, periph_hw)
    isr_sw = parse_irq_file(isr_em_path, periph_hw)

    if len(isr_hw) != len(isr_sw):
        fastdyn_log.warn("Number of ISRs by the emulated device model does not match with the hardware")
    else:
        for isr_iter in range(len(isr_hw)):
            if isr_hw[isr_iter] != isr_sw[isr_iter]:
                fastdyn_log.warn(f"Hardware ISR Loop by the emulated device model does not match with the hardware access ISR Loop")

    diff_data.isr_analysis_data = f'''
    Hardware ISR Analysis
    {isr_hw}

    Emulated Model ISR Analysis
    {isr_sw}

    '''


def diff_runtime_trace_analysis(diff_data, periph_hw, periph_em):
    state_hw_path = os.path.join(periph_hw, 'runtime_full_trace.txt')
    state_em_path = os.path.join(periph_em, 'runtime_full_trace.txt')

    state_hw = parse_runtime_trace(state_hw_path, periph_hw, diff_data.data_registers)
    state_em = parse_runtime_trace(state_em_path, periph_em, diff_data.data_registers)

    if len(state_hw) != len(state_em):
        fastdyn_log.warn("Number of data accesses by the emulated device model does not match with the hardware")
    else:
        for i in range(len(state_hw)):
            if state_hw[i] != state_em[i]:
                fastdyn_log.warn(f"Hardware access {state_hw[i]} does not match with the emulated device model access {state_em[i]}")

    diff_data.diff_runtime_trace = f'''
    Hardware Runtime Trace Data Accesses
    {state_hw}

    Emulated Model Runtime Trace Data Accesses
    {state_em}

    '''
def diff_stateful_analysis(diff_data, periph_hw, periph_em):
    state_hw_path = os.path.join(periph_hw, 'state.txt')
    state_em_path = os.path.join(periph_em, 'state.txt')

    state_hw = parse_state_file(state_hw_path, periph_hw)
    state_em = parse_state_file(state_em_path, periph_em)

    if state_hw != state_em:
        fastdyn_log.warn(f"Number of RMW pattern registers do not match in the emulated model with the hardware")

    diff_data.diff_state_data = f'''
    Read-Modify-Write (RMW) pattern detected in the hardware on registers::
    {state_hw}

    Read-Modify-Write (RMW) pattern detected in the emulated model on registers::
    {state_em}
    '''


def diff_loop_pattern(diff_data, periph_hw, periph_em):
    loop_pattern_files_hw = [f for f in os.listdir(periph_hw) if f.startswith('loop_pattern')]
    loop_pattern_files_em = [f for f in os.listdir(periph_hw) if f.startswith('loop_pattern')]

    if len(loop_pattern_files_hw) != len(loop_pattern_files_em):
        fastdyn_log.warn("Number of loop pattern files by the hardware does not match with the emulator")
    else:
        diff_data.diff_loop_pattern_data = ''
        for index, file in enumerate(loop_pattern_files_hw):  #the file name will be same for both the em and hw
            curr_loop_hw = os.path.join(periph_hw, file)
            curr_loop_em = os.path.join(periph_em, file)

            loop_pattern_hw = parse_loop_pattern_file(curr_loop_hw, periph_hw, diff_data.data_registers)
            loop_pattern_em = parse_loop_pattern_file(curr_loop_em, periph_em, diff_data.data_registers)

            if len(loop_pattern_hw) != len(loop_pattern_em):
                fastdyn_log.warn(f"Size of the loop pattern for hardware does not match with emulator for file {loop_pattern_hw}")
            else:
                for i in range(len(loop_pattern_hw)):
                    if loop_pattern_hw[i] != loop_pattern_em[i]:
                        fastdyn_log.warn(f'Access {loop_pattern_em[i]} by the emulated model does not match with the hardware access {loop_pattern_hw[i]}')
                        diff_data.not_match = True

            diff_data.diff_loop_pattern_data += f'''
                Hardware Loop pattern {index+1}
                {loop_pattern_hw}

                Emulated Model Loop pattern {index+1}
                {loop_pattern_em}

            '''

def diff_entropy_calculate(diff_data, periph_hw, periph_em):
    '''
    Retreive the entropy the value from the hardware and compare with the emulated value.
    If both match and also if any register has an high entropy, add that to data register
    '''
    entropy_hw_path = os.path.join(periph_hw, 'entropy.txt')
    entropy_em_path = os.path.join(periph_em, 'entropy.txt')

    entropy_hw = parse_entropy_file(entropy_hw_path, periph_hw)
    entropy_em = parse_entropy_file(entropy_em_path, periph_em)

    if len(entropy_hw.keys())!=len(entropy_em.keys()):
        fastdyn_log.warn("Number of registers used by the emulator does not match with the hardware!")
        diff_data.not_match = True
    else:
        for reg in entropy_hw:
            try:
                if entropy_hw[reg][1] == entropy_em[reg][1]:
                    if entropy_hw[reg][0].lower() == 'high':
                        diff_data.data_registers.append(entropy_hw[reg])
                    continue
                else:
                    fastdyn_log.warn(f'Entropy does not match!')
                    diff_data.not_match = True
            except Error as e:
                fastdyn_log.warn(f'Register {reg} does not exist in the emulated data!')

    #Collecting the entropy in case we need in future
    diff_data.diff_entropy_data = f'''
    Hardware Entropy::
    {entropy_hw}

    Emulated Model Entropy::
    {entropy_em}
    '''


def diff_init_pattern(diff_data, periph_hw, periph_em):
    init_hw_path = os.path.join(periph_hw, 'init.txt')
    init_em_path = os.path.join(periph_em, 'init.txt')

    init_hw = parse_init_file(init_hw_path, periph_hw, diff_data.data_registers)
    init_em = parse_init_file(init_em_path, periph_em, diff_data.data_registers)

    if len(init_hw) != len(init_em):
        fastdyn_log.warn("Number of access for the initialization by the emulated model does not match with the hardware")
        diff_data.not_match = True
    else:
        for i in range(len(init_hw)):
            if init_hw[i] == init_em[i]:
                continue
            else:
                fastdyn_log.warn(f"Access {init_em[i]} by the emulated model does not match with the hardware access {init_hw[i]}")
                diff_data.not_match = True

    #collecting the init data in case we need in future
    diff_data.diff_init_data = f'''
    Hardware Initialization::
    {init_hw}

    Emulated Model Initialization::
    {init_em}
    '''

def parse_entropy_file(path, periph_name):
    """Parses an entropy file and returns a dict of register → [level, entropy]."""
    register_entropy = re.compile(
        r'(?i)^\s*[-•*]?\s*Register\s+([A-Z0-9_]+)\s*:\s*([A-Z]+)\s*(?:\([^)]*\))?\s*[-–—]*\s*Entropy\s*[:=≈]\s*((?:\d+(?:\.\d+)?|\.\d+))\s*bits?'
    )

    entropy_data = {}

    if not os.path.exists(path):
        fastdyn_log.error(f'Entropy file does not exist under the {periph_name} directory: {path}')
        return entropy_data

    with open(path, 'r') as file:
        for line in file:
            if match := register_entropy.search(line):
                reg, level, entropy = match.groups()
                entropy_data[reg] = [level, entropy]

    return entropy_data

def parse_loop_pattern_file(path, periph_name, data_registers):
    """Parses the loop file and returns a list of all the accesses."""
    pattern = re.compile(
        r'^\[\s*\d+\.\d+\]\s*'
        r'(READ|WRITE)\s+to\s+'
        r'([A-Za-z0-9_]+)->([A-Za-z0-9_]+)\s*'
        r'\((0x[0-9A-Fa-f]+)\)\s*'
        r'value=(0x[0-9A-Fa-f]+),\s*pc=(0x[0-9A-Fa-f]+)'
    )

    loop_data = []

    if not os.path.exists(path):
        fastdyn_log.error(f'Loop pattern file does not exist under the {periph_name} directory: {path}')
        return loop_data

    with open(path, 'r') as file:
        for line in file:
            if match := pattern.search(line):
                access_type, peripheral, register, address, value, pc = match.groups()
                if register in data_registers:
                    continue
                loop_data.append([access_type, peripheral, register, address, value, pc])

    return loop_data

def parse_state_file(path, periph_name):
    pattern = re.compile(
        r'-\s*Read-Modify-Write\s*\(RMW\)\s*pattern\s*detected\s*on\s*register:\s*([A-Za-z0-9_]+)'
    )
    state_data = []

    if not os.path.exists(path):
        fastdyn_log.error(f'State Analysis file does not exist under the {periph_name} directory: {path}')
        return state_data

    with open(path, 'r') as file:
        for line in file:
            if match := pattern.search(line):
                state_data.append(match.group(1))   #just track the register names

    return state_data

import os
import re
from collections import defaultdict

def parse_irq_file(path, periph_name, ignore_registers=None):
    """
    Parses an IRQ/ISR trace file into structured patterns per IRQ vector.

    Returns:
        list[list[list[str]]]:
            iterations[occurrence] = [
                [periph, vector],
                [access_type, peripheral, register, address, value, pc],
                ...
            ]
    """
    ignore_registers = set(ignore_registers or [])

    irq_header = re.compile(
        r'^\s*\[\s*\d+\.\d+\]\s*INTERRUPT on ([A-Za-z0-9_]+), Vector=(\d+)'
    )

    pattern = re.compile(
        r'^\s*\[\s*\d+\.\d+\]\s+'
        r'(READ|WRITE)\s+to\s+'
        r'([A-Za-z0-9_]+)->([A-Za-z0-9_]+)\s*'
        r'\((0x[0-9A-Fa-f]+)\)\s*'
        r'value=(0x[0-9A-Fa-f]+),\s*pc=(0x[0-9A-Fa-f]+)'
    )

    iterations = []
    current_iter = []

    if not os.path.exists(path):
        print(f'IRQ trace file does not exist: {path}')
        return iterations

    with open(path, 'r') as file:
        for line in file:
            # New interrupt header
            if header_match := irq_header.search(line):
                periph, vector = header_match.groups()
                # flush old iteration if exists
                if current_iter:
                    iterations.append(current_iter)
                # start new iteration and add header at index 0
                current_iter = []
                current_iter.insert(0, [periph, vector])

            # MMIO access lines
            elif match := pattern.search(line):
                access_type, peripheral, register, addr, value, pc = match.groups()
                if register in ignore_registers:
                    continue
                current_iter.append([access_type, peripheral, register, addr, value, pc])

    # flush last iteration
    if current_iter:
        iterations.append(current_iter)

    return iterations

def parse_init_file(path, periph_name, data_registers):
    """Parses an init file and returns a list of all the accesses."""
    pattern = re.compile(
        r'^\[\s*\d+\.\d+\]\s*'
        r'(READ|WRITE)\s+to\s+'
        r'([A-Za-z0-9_]+)->([A-Za-z0-9_]+)\s*'
        r'\((0x[0-9A-Fa-f]+)\)\s*'
        r'value=(0x[0-9A-Fa-f]+),\s*pc=(0x[0-9A-Fa-f]+)'
    )

    init_data = []

    if not os.path.exists(path):
        fastdyn_log.error(f'Entropy file does not exist under the {periph_name} directory: {path}')
        return init_data

    with open(path, 'r') as file:
        for line in file:
            if match := pattern.search(line):
                access_type, peripheral, register, address, value, pc = match.groups()
                if register in data_registers:
                    continue
                init_data.append([access_type, peripheral, register, address, value, pc])

    return init_data

def parse_runtime_trace(path, periph_name, data_registers):
    """Parses an runtime full trace file and returns a list of all the accesses."""
    pattern = re.compile(
        r'^\[\s*\d+\.\d+\]\s*'
        r'(READ|WRITE)\s+to\s+'
        r'([A-Za-z0-9_]+)->([A-Za-z0-9_]+)\s*'
        r'\((0x[0-9A-Fa-f]+)\)\s*'
        r'value=(0x[0-9A-Fa-f]+),\s*pc=(0x[0-9A-Fa-f]+)'
    )

    runtime_trace_data = []

    if not os.path.exists(path):
        fastdyn_log.error(f'Runtime Trace file does not exist under the {periph_name} directory: {path}')
        return runtime_trace_data

    with open(path, 'r') as file:
        for line in file:
            if match := pattern.search(line):
                access_type, peripheral, register, address, value, pc = match.groups()
                if register in data_registers:
                    runtime_trace_data.append([access_type, peripheral, register, address, value, pc])

    return runtime_trace_data

def parse_summary_file(filepath: str) -> Dict[str, str]:
    """Parses the key-value pairs from the summary.txt file, cleaning prefixes like '-'."""
    summary_data = {}
    print(summary_data)
    if not os.path.exists(filepath):
        return summary_data
    try:
        with open(filepath, 'r') as f:
            for line in f:
                if ":" in line:
                    key, value = line.split(":", 1)
                    key = key.strip().lstrip("-").strip()  # remove leading '-' and spaces
                    summary_data[key] = value.strip()
    except Exception:
        pass
    return summary_data
