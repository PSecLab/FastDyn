'''
All code related to fuzzer helper functions
'''
import os, sys
import logging
import re

from .. import fastdyn_log as fastdyn_log_conf


log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

def generate_vi(cm_path, peripheral):
    anchor_vi = []

    #get the peripheral related info
    peripheral_dir  = os.path.join(cm_path, peripheral)
    entropy_file    = os.path.join(peripheral_dir, "entropy.txt")
    runtime_trace   = os.path.join(peripheral_dir, "runtime_full_trace.txt")

    if not os.path.exists(entropy_file) or not os.path.exists(runtime_trace):
        fastdyn_log.error(f"Unable to find entropy file under the {peripheral_dir}")
        sys.exit(1)

    data_registers = []
    with open(entropy_file, 'r') as entropy:
        for line in entropy:
            if "suggests data register" in line:
                # line looks like: "- Register DR: HIGH ..."
                parts = line.split()
                # parts[0] is '-', parts[1] is 'Register', parts[2] is 'DR:'

                reg_name = parts[2].replace(':', '') # Remove the colon
                fastdyn_log.info(f"Data Register found: {reg_name}")

                data_registers.append(reg_name)

        if len(data_registers) == 0:
            fastdyn_log.warning("No Data register found in the whole entropy file, no valuable fuzzing may be performed")

    pattern = re.compile(r'\((0x[\da-fA-F]+)\).*?pc=(0x[\da-fA-F]+)', re.IGNORECASE)

    found_pcs = []
    register_addrs = []

    with open(runtime_trace, 'r') as rt:
        for line in rt:
            if 'write' not in line.lower():
                continue
            found_dr = False
            for dr in data_registers:
                if dr and dr in line: # Ensure 'dr' is not empty and exists in the line
                    found_dr = True
                    break
            if found_dr:
                match = pattern.search(line)
                if match:
                    reg, pc = match.groups()
                    if pc in found_pcs:   #final check to make sure a every vi is unique i.e., no more than one vi for a single pc
                        continue
                    register_addrs.append(reg)
                    found_pcs.append(pc)

    if len(found_pcs) != len(register_addrs):
        fastdyn_log.error("Error occurred while parsing the runtime trace, trace format is not correct")

    #now, let's just generate the virtual instructions!
    for i in range(len(found_pcs)):
        single_vi = f'{found_pcs[i]} anchor /tmp/input_fastdyn:{int(register_addrs[i], 16)}'
        anchor_vi.append(single_vi)

    return anchor_vi