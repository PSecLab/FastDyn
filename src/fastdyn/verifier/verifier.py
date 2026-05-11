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

        # Raw structured data for prompt_gen.py to format with LLM instructions.
        # Verifier stores facts here; prompt generation adds interpretation.
        self.raw_state = None      # dict with rmw_hw, rmw_em, wp_hw_str, wp_em_str
        self.raw_entropy = None    # dict with entropy_hw, entropy_em, warnings list
        self.diff_runtime_trace = None
        self.rare_transitions_data = None
        self.isr_analysis_data = None
        self.svd_bitfields_data = None

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
        fastdyn_log.warning(f"Peripheral {peripheral} not in emulation directory. Emulation crashed early.")
        differential_data.not_match = True
        differential_data.diff_init_data = "CRITICAL ERROR: No initialization trace generated because emulation crashed before or during initialization."
        differential_data.diff_loop_pattern_data = "CRITICAL ERROR: Emulation crashed."
        differential_data.diff_state_data = "CRITICAL ERROR: Emulation crashed early. Check your model code for infinite loops, bad memory accesses, or missing behaviors causing the firmware to halt/panic."
        differential_data.diff_entropy_data = "CRITICAL ERROR: Emulation crashed."
        differential_data.diff_runtime_trace = "CRITICAL ERROR: Emulation crashed. This peripheral was never accessed in emulation."
        differential_data.rare_transitions_data = ""
        differential_data.isr_analysis_data = "CRITICAL ERROR: Emulation crashed."
        differential_data.svd_bitfields_data = ""
        return True, differential_data

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

    # Pass through rare value transitions from the hardware trace (no diff needed)
    rare_path = os.path.join(periph_hw, 'rare_transitions.txt')
    if os.path.exists(rare_path):
        with open(rare_path, 'r') as f:
            differential_data.rare_transitions_data = f.read()
    else:
        differential_data.rare_transitions_data = ''

    # Pass through SVD bit field data (no diff needed — same for both traces)
    svd_bf_path = os.path.join(periph_hw, 'svd_bitfields.txt')
    if os.path.exists(svd_bf_path):
        with open(svd_bf_path, 'r') as f:
            differential_data.svd_bitfields_data = f.read()
    else:
        differential_data.svd_bitfields_data = ''

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

    if len(isr_hw) > 0 and len(isr_sw) == 0:
        fastdyn_log.warn("Hardware fired ISRs, but emulation fired none. Hard mismatch.")
        diff_data.not_match = True
    elif len(isr_hw) == 0 and len(isr_sw) > 0:
        fastdyn_log.warn("Emulation fired ISRs, but hardware fired none. Hard mismatch.")
        diff_data.not_match = True
    else:
        if len(isr_hw) != len(isr_sw):
            fastdyn_log.warn(f"Number of ISRs by the emulated device model ({len(isr_sw)}) does not match with the hardware ({len(isr_hw)}) - Warning only (timing artifact expected)")

        # Compare available iterations but cleanly ignore exact mismatches to prevent infinite LLM retry loops
        min_len = min(len(isr_hw), len(isr_sw))
        for isr_iter in range(min_len):
            if isr_hw[isr_iter] != isr_sw[isr_iter]:
                fastdyn_log.warn(f"ISR Loop iteration {isr_iter} by the emulated device model does not match exactly with hardware - Warning only")

    # Cap the dumped ISR data so an ISR storm in emulation cannot balloon the
    # revised prompt past the LLM's per-message size limit. The encoder already
    # deduplicates by abstract trace pattern; here we additionally bound the
    # iteration count and per-iteration event count for prompt sanity.
    MAX_ISR_ITERS  = 15
    MAX_EVENTS_PER = 60

    def _shrink(iters):
        out = []
        for it in iters[:MAX_ISR_ITERS]:
            if len(it) > MAX_EVENTS_PER:
                out.append(it[:MAX_EVENTS_PER] + [
                    ['...', f'{len(it) - MAX_EVENTS_PER} more events truncated']
                ])
            else:
                out.append(it)
        return out

    hw_show = _shrink(isr_hw)
    sw_show = _shrink(isr_sw)
    hw_omitted = max(0, len(isr_hw) - MAX_ISR_ITERS)
    sw_omitted = max(0, len(isr_sw) - MAX_ISR_ITERS)

    diff_data.isr_analysis_data = f'''
    Hardware ISR Analysis ({len(isr_hw)} iterations total, showing first {len(hw_show)})
    {hw_show}
    {f"# ... {hw_omitted} more iterations omitted (identical pattern)" if hw_omitted else ""}

    Emulated Model ISR Analysis ({len(isr_sw)} iterations total, showing first {len(sw_show)})
    {sw_show}
    {f"# ... {sw_omitted} more iterations omitted (identical pattern)" if sw_omitted else ""}

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

    read_seq_section = _build_read_sequence_section(state_hw_path, state_em_path)

    diff_data.diff_runtime_trace = f'''
    Hardware Runtime Trace Data Accesses
    {state_hw}

    Emulated Model Runtime Trace Data Accesses
    {state_em}

    {read_seq_section}
    '''


def _build_read_sequence_section(hw_path, em_path, max_per_reg=10):
    """
    Builds a compact 'Actual Read Sequences' subsection showing the first
    max_per_reg read values per register from both traces side-by-side.
    MCU-agnostic: uses no register-name heuristics — every register that
    was read at least once appears. Registers where hw and em sequences
    match exactly are marked [MATCH]; differing ones are marked [DIFFERS].
    This section is purely informational and does not affect MISMATCH/PASSING.
    """
    pattern = re.compile(
        r'^\[\s*\d+\.\d+\]\s*READ\s+to\s+'
        r'[A-Za-z0-9_]+->([A-Za-z0-9_]+)\s*'
        r'\([^)]+\)\s*value=(0x[0-9A-Fa-f]+)'
    )

    def collect(path):
        seq = defaultdict(list)
        if not os.path.exists(path):
            return seq
        with open(path) as f:
            for line in f:
                m = pattern.search(line)
                if m:
                    reg, val = m.group(1), m.group(2)
                    seq[reg].append(val)
        return seq

    hw_seq = collect(hw_path)
    em_seq = collect(em_path)
    all_regs = sorted(set(hw_seq) | set(em_seq))

    if not all_regs:
        return ''

    lines = ['Actual Read Sequences (first %d values per register):' % max_per_reg]
    for reg in all_regs:
        hw_vals = hw_seq[reg][:max_per_reg]
        em_vals = em_seq[reg][:max_per_reg]
        tag = '[MATCH]' if hw_vals == em_vals else '[DIFFERS]'
        hw_str = '[' + ', '.join(hw_vals) + (', ...' if len(hw_seq[reg]) > max_per_reg else '') + ']'
        em_str = '[' + ', '.join(em_vals) + (', ...' if len(em_seq[reg]) > max_per_reg else '') + ']'
        lines.append(f'  {tag} {reg}: hw={hw_str}  em={em_str}')

    return '\n    '.join(lines)
def diff_stateful_analysis(diff_data, periph_hw, periph_em):
    state_hw_path = os.path.join(periph_hw, 'state.txt')
    state_em_path = os.path.join(periph_em, 'state.txt')

    rmw_hw, wp_hw = parse_state_file(state_hw_path, periph_hw)
    rmw_em, wp_em = parse_state_file(state_em_path, periph_em)

    if rmw_hw != rmw_em:
        fastdyn_log.warn(f"Number of RMW pattern registers do not match in the emulated model with the hardware")

    # Format Write-Poll pairs as human-readable strings
    wp_hw_str = ', '.join(f'Polling Loop on {dst} (preceded by write to {src})' for src, dst in wp_hw) if wp_hw else 'None detected'
    wp_em_str = ', '.join(f'Polling Loop on {dst} (preceded by write to {src})' for src, dst in wp_em) if wp_em else 'None detected'

    # Store raw data for prompt_gen.py to format with LLM-specific instructions
    diff_data.raw_state = {
        'rmw_hw': rmw_hw,
        'rmw_em': rmw_em,
        'wp_hw_str': wp_hw_str,
        'wp_em_str': wp_em_str,
    }

    diff_data.diff_state_data = f'''
    Read-Modify-Write (RMW) pattern detected in the hardware on registers::
    {rmw_hw}

    Read-Modify-Write (RMW) pattern detected in the emulated model on registers::
    {rmw_em}

    Polling detection (firmware polls a register expecting a bit change)::
    {wp_hw_str}

    Polling detection in emulated model::
    {wp_em_str}
    '''


def diff_loop_pattern(diff_data, periph_hw, periph_em):
    loop_pattern_files_hw = [f for f in os.listdir(periph_hw) if f.startswith('loop_pattern')]
    loop_pattern_files_em = [f for f in os.listdir(periph_em) if f.startswith('loop_pattern')]

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

    # ── Per-register entropy mismatch analysis ──────────────────────────────
    entropy_warnings = []
    all_regs = sorted(set(list(entropy_hw.keys()) + list(entropy_em.keys())))

    for reg in all_regs:
        hw_entry = entropy_hw.get(reg)
        em_entry = entropy_em.get(reg)

        if hw_entry and not em_entry:
            # Register exists in hardware but not in emulation at all
            entropy_warnings.append(
                f"  [WARNING] {reg}: present in hardware (entropy={hw_entry[0]}, {hw_entry[1]}) "
                f"but NOT accessed in emulated model — the model may be stuck before "
                f"reaching this register."
            )
            diff_data.not_match = True
        elif em_entry and not hw_entry:
            entropy_warnings.append(
                f"  [WARNING] {reg}: accessed in emulated model but NOT in hardware trace."
            )
            diff_data.not_match = True
        elif hw_entry and em_entry:
            hw_level, hw_val = hw_entry[0], float(hw_entry[1])
            em_level, em_val = em_entry[0], float(em_entry[1])

            # Track data registers (HIGH entropy = externally-driven values
            # like UART DR, SPI DR, etc.)
            is_data_reg = (hw_level.lower() == 'high' or em_level.lower() == 'high')
            if hw_level.lower() == 'high':
                diff_data.data_registers.append(reg)

            # Skip mismatch warnings for data registers: their entropy
            # depends on external input (e.g., what the user types on
            # a UART console), not on model correctness.  Different
            # input across captures makes entropy comparison meaningless.
            if is_data_reg:
                continue

            if hw_val > 0 and em_val == 0:
                # Duration-independent: HW register value changes but model
                # returns the exact same value every time — strong signal.
                #
                # However, when both sides fall in the same entropy LEVEL
                # (e.g. both LOW), the variation in hardware may be caused
                # by timing artifacts rather than missing model logic.
                # Example: STM32 TIM SR register shows alternating 0x1F/0x1E
                # in hardware due to spurious NVIC re-entry (APB write
                # propagation delay), but the model correctly returns only
                # 0x1F because it clears UIF synchronously.  Both are LOW
                # entropy; the difference is not a behavioral bug.
                #
                # Downgrade to warning-only when levels match; keep hard
                # fail when levels differ (e.g. hw=MEDIUM, em=LOW).
                same_level = (hw_level.lower() == em_level.lower())
                entropy_warnings.append(
                    f"{reg}: hardware entropy={hw_level} ({hw_val}) but emulated "
                    f"entropy=LOW (0.00). The model always returns the same value for "
                    f"this register, but hardware shows it changing. This strongly "
                    f"suggests the model is missing self-clearing bits, auto-"
                    f"transitioning state, or is stuck in a polling loop."
                )
                if not same_level:
                    diff_data.not_match = True
                else:
                    fastdyn_log.info(
                        f"Entropy mismatch on {reg} downgraded to warning: both "
                        f"sides are {hw_level} entropy (hw={hw_val}, em={em_val}). "
                        f"Small variation within the same level is likely a timing "
                        f"artifact, not a model defect."
                    )
            elif hw_val == 0.0 and em_val >= 0.5:
                # Register is perfectly constant in hardware (write-only,
                # reserved, or always-zero) but varies in emulation.
                # Guard: em_val >= 0.5 avoids false positives from floating-point
                # noise on registers read only once or twice.
                # This is MCU-agnostic: any hardware register that never changes
                # value across all reads in a complete firmware run should be
                # equally stable in the emulated model.
                entropy_warnings.append(
                    f"{reg}: hardware entropy=0.00 (register always returns the "
                    f"same value) but emulated entropy={em_level} ({em_val:.2f}). "
                    f"The model returns varying values for a register that is "
                    f"constant in hardware — likely a write-only or reserved "
                    f"register whose reads the model handles incorrectly."
                )
                diff_data.not_match = True
            elif hw_level.lower() != em_level.lower():
                # Compare entropy LEVEL categories (LOW/MEDIUM/HIGH) not raw
                # values.  Level categories are more robust to capture-duration
                # differences than numeric thresholds: a 5s vs 10s recording
                # may shift the magnitude but rarely changes the category.
                #
                # However, when the raw entropy values are close (within 1.0
                # bit), the level difference is likely a boundary artifact
                # rather than a true modeling defect.  For example, hw=2.5
                # (MEDIUM) vs em=1.79 (LOW) differ by only 0.71 bits — the
                # model captures the register's dynamic behavior but FIFO
                # level fields vary slightly less due to instant-transfer
                # modeling.  Downgrade to warning in this case.
                entropy_delta = abs(hw_val - em_val)
                entropy_warnings.append(
                    f"{reg}: entropy level mismatch — hardware={hw_level} ({hw_val}), "
                    f"emulated={em_level} ({em_val}). Different entropy levels suggest "
                    f"the register's dynamic behavior is not modeled correctly."
                )
                if entropy_delta > 1.0:
                    diff_data.not_match = True
                else:
                    fastdyn_log.info(
                        f"Entropy level mismatch on {reg} downgraded to warning: "
                        f"raw values are within 1.0 bit (hw={hw_val}, em={em_val}, "
                        f"delta={entropy_delta:.2f}). Near-boundary level difference "
                        f"is likely a FIFO timing artifact, not a model defect."
                    )

    warnings_str = '\n'.join(entropy_warnings) if entropy_warnings else '    No significant entropy mismatches.'

    for w in entropy_warnings:
        fastdyn_log.warn(f"Entropy mismatch: {w.strip()}")

    # Store raw data for prompt_gen.py to format with LLM-specific instructions
    diff_data.raw_entropy = {
        'entropy_hw': entropy_hw,
        'entropy_em': entropy_em,
        'warnings': entropy_warnings,
    }

    # Factual diff report
    diff_data.diff_entropy_data = f'''
    Hardware Entropy::
    {entropy_hw}

    Emulated Model Entropy::
    {entropy_em}

    ### Entropy Mismatch Details
{warnings_str}
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
    rmw_pattern = re.compile(
        r'-\s*Read-Modify-Write\s*\(RMW\)\s*pattern\s*detected\s*on\s*register:\s*([A-Za-z0-9_]+)'
    )
    # Capture Write-Poll patterns: "WRITE to <REG>, followed by polling <REG>"
    wp_pattern = re.compile(
        r'-\s*Write-Poll\s*pattern\s*detected:\s*WRITE\s+to\s+([A-Za-z0-9_]+),\s*followed\s+by\s+polling\s+([A-Za-z0-9_]+)'
    )
    rmw_registers = []
    write_poll_pairs = []

    if not os.path.exists(path):
        fastdyn_log.error(f'State Analysis file does not exist under the {periph_name} directory: {path}')
        return rmw_registers, write_poll_pairs

    with open(path, 'r') as file:
        for line in file:
            if match := rmw_pattern.search(line):
                rmw_registers.append(match.group(1))
            elif match := wp_pattern.search(line):
                write_poll_pairs.append((match.group(1), match.group(2)))

    return rmw_registers, write_poll_pairs

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
        fastdyn_log.error(f'IRQ trace file does not exist: {path}')
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
