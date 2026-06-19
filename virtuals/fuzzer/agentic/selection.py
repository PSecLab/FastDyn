import time
import random
import struct
import pyghidra
import flow
import networkx as nx
from dataclasses import dataclass
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.elf.constants import P_FLAGS

ignored_pairs = set()
input_count = {}

# Favor inputs with fewer selections while keeping selection random
def select_input(pairs):
    global ignored_pairs
    global input_count

    valid = [pair for pair in pairs if pair not in ignored_pairs]
    if len(valid) == 0:
        print("[selection.py] our valid selections are empty")
        return None

    for pair in valid:
        if pair not in input_count:
            input_count[pair] = 0

    selection = random.choices(
        valid,
        weights=[1.0 / float(input_count[pair] + 1) for pair in valid],
        k=1,
    )[0]

    input_count[selection] += 1
    return selection

# Input wasn't considered usable, remove from selection
def ignore_input(pair):
    global ignored_pairs
    ignored_pairs.add(pair)

@dataclass(frozen=True)
class InterestingBranch:
    edge: int
    impact: int
    solvability: int

def get_instr_trace(program, trace_path, coverage):
    blocks = {} # pc: num_instr
    trace = [] # sequential pcs (instruction level)

    listing = program.getListing()

    with open(coverage, "r") as file:
        for line in file:
            block = line.split("\t")
            blocks[int(block[0], 16)] = int(block[1])

    addr_space = program.getAddressFactory().getDefaultAddressSpace()
    with open(trace_path, "r") as file:
        for line in file:
            if not line or line[0] == "#":
                continue

            pc = int(line.strip("\n"), 0)
            addr = addr_space.getAddress(pc)
            instr = listing.getInstructionContaining(addr)
            instr_addr = int(instr.getAddress().getOffset())

            trace.append((instr_addr, instr.getBytes()))

            remaining = blocks[pc] - 1
            while remaining > 0:
                instr = instr.getNext()
                instr_addr = int(instr.getAddress().getOffset())
                trace.append((instr_addr, instr.getBytes()))
                remaining = remaining - 1

    return trace

def select_branch(basic_block_model, binary, raw, trace_path, cfg, branches, coverage, cvg_path, regions, snapshot, taint):
    def filter_node(n):
        return not n in coverage

    program = basic_block_model.getProgram()
    listing = program.getListing()

    print("[selection.py] creating instruction trace")
    trace = get_instr_trace(program, trace_path, cvg_path)
    print("[selection.py] instruction trace created")
    
    interesting_branches = []
    trimmed = nx.subgraph_view(cfg, filter_node=filter_node)
    for branch in branches:
        # TODO: implement a metric for solvability
        impact = len(nx.descendants(trimmed, branch[1]))
        solvability = 1

        # Transform branch source to last instruction of block rather than first, improves constraint creation
        source_blocks = basic_block_model.getCodeBlocksContaining(program.getAddressFactory().getDefaultAddressSpace().getAddress(branch[0]), pyghidra.task_monitor())
        if len(source_blocks) == 0:
            print("[selection.py] could not get source block of branch")
        branch_source = int(listing.getInstructionContaining(source_blocks[0].getMaxAddress()).getAddress().getOffset())

        interesting_branch = InterestingBranch((branch_source, branch[1]), impact, solvability)
        interesting_branches = interesting_branches + [interesting_branch]

    sorted_branches = sorted(interesting_branches, key=lambda branch: branch.impact, reverse=True)

    if taint:
        branches = [branch.edge[0] for branch in interesting_branches]

        tainted = flow.run_triton_is_tainted_subprocess(binary, raw, trace, branches, regions, snapshot)
        for branch in sorted_branches:
            print(f"[selection.py] {hex(branch.edge[0])} -> {hex(branch.edge[1])}:")
            print(f"[selection.py] Tainted - {tainted[branch.edge[0]]}")
            if tainted[branch.edge[0]] == False:
                interesting_branches.remove(branch)
            else:
                taint_bytes = flow.run_triton_get_taint_subprocess(binary, raw, trace, branch.edge[0], regions, snapshot)
                return (branch, taint_bytes)

    if len(interesting_branches) == 0:
        return (None, "")

    selected = random.choices(
        interesting_branches,
        weights=[item.impact for item in interesting_branches],
        k=1,
    )[0]

    # call_stack = flow.run_triton_get_callstack_subprocess(binary, raw, trace, selected.edge[0], regions, snapshot)

    return (selected, "")

def filter_interesting(cfg, trace, coverage):
    interesting = []
    for reached in set(trace):
        for target in cfg.neighbors(reached):
            if not target in coverage:
                interesting = interesting + [(reached, target)]

    return interesting
