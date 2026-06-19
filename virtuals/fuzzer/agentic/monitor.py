import os
import re
import sys
import time
import jpype
import pyghidra
import argparse
import networkx as nx
import tracemalloc

from pathlib import Path
from dataclasses import dataclass
from networkx.drawing.nx_pydot import write_dot

import agent
import context
import parsers
import selection
import target
import flow

DEFAULT_INPUT_NAME = "agentic-input.txt"
DEFAULT_WORK_DIR = "fastdyn_work"
DEFAULT_POLL_INTERVAL = 5.0

@dataclass(frozen=True)
class InterestingPair:
    index: int
    raw_path: Path
    trace_path: Path

def create_cfg_func(program, output_dir=None):
    graph = nx.DiGraph()

    function_manager = program.getFunctionManager()
    for function in function_manager.getFunctions(True):
        #TODO check if this is always what we expect
        addr = int(function.getEntryPoint().getOffset())
        graph.add_node(addr)
        #graph.add_node(function.getName())

    for function in function_manager.getFunctions(True):
        u = int(function.getEntryPoint().getOffset())
        for called in function.getCalledFunctions(pyghidra.task_monitor()):
            v = int(called.getEntryPoint().getOffset())
            graph.add_edge(u, v)
            #graph.add_edge(function.getName(), called.getName())

    for edge in graph.edges:
        label = len(nx.descendants(graph, edge[1]))
        graph[edge[0]][edge[1]]["label"] = label


    output_path = Path(output_dir or ".") / "cfg_func.dot"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_dot(graph, output_path)
    print(f"[monitor.py] created {output_path}")

    return graph

def create_cfg_bb(basic_block_model, output_dir=None):
    graph = nx.DiGraph()
    for block in basic_block_model.getCodeBlocks(pyghidra.task_monitor()):
        addr = int(block.getFirstStartAddress().getOffset())
        graph.add_node(addr)

    for block in basic_block_model.getCodeBlocks(pyghidra.task_monitor()):
        u = int(block.getFirstStartAddress().getOffset())
        it = block.getDestinations(pyghidra.task_monitor())
        while it.hasNext():
            dest = it.next().getDestinationBlock()
            if dest is None:
                continue

            v = int(dest.getFirstStartAddress().getOffset())
            graph.add_edge(u, v)

    output_path = Path(output_dir or ".") / "cfg_bb.dot"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_dot(graph, output_path)
    print(f"[monitor.py] created {output_path}")

    return graph

def load_or_import_program(project, binary_path: Path, program_path: str):
    """
    Return (program, existed).
    If the program already exists in the project, open it.
    Otherwise import/load it, save it to the project, then open it.
    """
    try:
        # Context manager version is nicer, but here we need to return the program.
        # consume_program() requires manual release by caller.
        program, consumer = pyghidra.consume_program(project, program_path)
        return program, consumer, True
    except FileNotFoundError:
        pass

    loader = (
        pyghidra.program_loader()
        .project(project)
        .source(str(binary_path))
        .name(binary_path.name)
    )

    with loader.load() as load_results:
        load_results.save(pyghidra.task_monitor())

    program, consumer = pyghidra.consume_program(project, program_path)
    return program, consumer, False

def check_in_dir(in_dir):
    PAIR_RE = re.compile(r"^interesting(\d+)\.raw$")
    pairs = []
    if not in_dir.is_dir():
        return pairs

    for raw_path in in_dir.iterdir():
        if not raw_path.is_file():
            continue
        match = PAIR_RE.match(raw_path.name)
        if match is None:
            continue
        index = int(match.group(1))
        trace_path = in_dir / f"interesting{index}.trace"
        if trace_path.is_file():
            pairs = pairs + [InterestingPair(
                index=index,
                raw_path=raw_path,
                trace_path=trace_path,
            )]
    return sorted(pairs, key=lambda pair: pair.index)


    # listing = program.getListing()

    # with open(coverage, "r") as file:
    #     for line in file.readlines():
    #         block = line.split("\t")
    #         blocks[int(block[0], 16)] = int(block[1])

    # with open(trace_path, "r") as file:
    #     for line in file.readlines():
    #         if not line or line[0] == "#":
    #             continue

    #         pc = int(line.strip("\n"), 0)
    #         addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(pc)
    #         instr = listing.getInstructionContaining(addr)

    #         end = (int(instr.getAddress().getOffset()) == branch)

    #         trace = trace + [(int(instr.getAddress().getOffset()), instr.getBytes())]

    #         remaining = blocks[pc] - 1
    #         while remaining > 0:
    #             instr = instr.getNext()
    #             trace = trace + [(int(instr.getAddress().getOffset()), instr.getBytes())]
    #             remaining = remaining - 1

    #         # The branch should only ever be at the end of a block
    #         if end:
    #             break

# maps the TBs to ghidra Basic Blocks, returning as set since coverage should be unique, unordered
# to avoid the possibility of missing a block, iterate through all instructions and get their block
def reduce_coverage(basic_block_model, coverage):
    program = basic_block_model.getProgram()
    listing = program.getListing()

    cvg = []
    with open(coverage, "r") as file:
        for line in file.readlines():
            pc = int(line.split("\t")[0], 16)
            count = int(line.split("\t")[1])

            addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(pc)
            instr = listing.getInstructionContaining(addr)

            count = count - 1
            while True:
                blocks = basic_block_model.getCodeBlocksContaining(addr, pyghidra.task_monitor())
                if len(blocks) == 0:
                    print(f"[monitor.py] trace block {hex(addr.getOffset())} not in basic block model")
                    break

                if len(blocks) > 1:
                    print(f"[monitor.py] trace block {hex(addr.getOffset())} exists multiple times in basic block model")

                block = blocks[0]
                node = int(block.getFirstStartAddress().getOffset())

                cvg = cvg + [node]

                instr = instr.getNext()
                addr = instr.getAddress()
                count = count - 1
                if count < 1:
                    break

            cvg = cvg + [node]
    
    return set(cvg)

# map TB trace to Basic Block trace, with deduplication (if different consecutive TBs are the same BB, deduplicate)
def reduce_trace(basic_block_model, trace_path):
    trace = []
    with open(trace_path, "r") as file:
        last_pc = 0
        last_bb = 0
        for line in file.readlines():
            if not line or line[0] == "#":
                continue

            pc = int(line.strip("\n"), 0)

            addr = basic_block_model.getProgram().getAddressFactory().getDefaultAddressSpace().getAddress(pc)
            blocks = basic_block_model.getCodeBlocksContaining(addr, pyghidra.task_monitor())
            if len(blocks) == 0:
                print(f"[monitor.py] trace block {hex(pc)} not in basic block model")
                continue

            if len(blocks) > 1:
                print(f"[monitor.py] trace block {hex(pc)} exists multiple times in basic block model")

            block = blocks[0]
            node = int(block.getFirstStartAddress().getOffset())

            # deduplicate basic blocks with multiple TBs, with care for looping blocks
            if not node == last_bb or pc == last_pc:
                trace = trace + [node]
                last_pc = pc
                last_bb = node

    return trace

def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("binary", help="Path to binary to analyze")

    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--work-dir", default=DEFAULT_WORK_DIR, help="FastDyn work directory used for default paths")
    run_parser.add_argument("--in-dir", help="The corpus to monitor for inputs and their traces")
    run_parser.add_argument("--out-dir", help="The directory to write the mutated outputs to")
    run_parser.add_argument("--coverage", help="Path to coverage information, such as bbl.txt")
    run_parser.add_argument("--regions")
    run_parser.add_argument("--snapshot")
    run_parser.add_argument("--input-path")
    run_parser.add_argument("--poll-interval", type=float, default=DEFAULT_POLL_INTERVAL)
    run_parser.add_argument("--model", help="Override the agent model; omitted uses agent.py's default")
    run_parser.add_argument("--taint", action="store_true")

    analyze_parser = subparsers.add_parser("analyze")
    analyze_parser.add_argument("--parser-search", action="store_true", help="Analyze the usage of switch statements")
    analyze_parser.add_argument("--dump-cfg", action="store_true")
    analyze_parser.add_argument("--generate-rag", action="store_true")
    analyze_parser.add_argument("--work-dir", default=DEFAULT_WORK_DIR, help="FastDyn work directory used for generated state")

    return parser.parse_args()

def resolve_common_defaults(args):
    work_dir = Path(args.work_dir).resolve()
    args.work_dir = str(work_dir)
    args.project_dir = str(work_dir / "ghidra_projects")

    if args.command != "run":
        return

    args.input_path = args.input_path or str(work_dir / DEFAULT_INPUT_NAME)
    args.in_dir = args.in_dir or str(work_dir / "corpus")
    args.out_dir = args.out_dir or str(work_dir / "corpus-agentic")
    args.coverage = args.coverage or str(work_dir / "bbl.txt")
    args.regions = args.regions or str(work_dir / "bin-writable-ranges")
    args.snapshot = args.snapshot or str(work_dir / "snapshot.bin")

def next_mutated_output_index(out_dir):
    out_dir = Path(out_dir)
    pattern = re.compile(r"^mutated(\d+)\.raw$")
    max_index = -1

    if not out_dir.is_dir():
        return 0

    for path in out_dir.iterdir():
        if not path.is_file():
            continue

        match = pattern.match(path.name)
        if match is None:
            continue

        max_index = max(max_index, int(match.group(1)))

    return max_index + 1

def monitor_loop(llm, basic_block_model, cfg, args):
    in_dir = Path(args.in_dir)
    out_dir = Path(args.out_dir)
    coverage_path = Path(args.coverage)
    regions_path = Path(args.regions)
    snapshot_path = Path(args.snapshot)
    input_path = Path(args.input_path)

    out_dir.mkdir(parents=True, exist_ok=True)
    input_path.parent.mkdir(parents=True, exist_ok=True)
    out_cnt = next_mutated_output_index(out_dir)

    while True:
        if not coverage_path.is_file():
            print(f"[monitor.py] waiting for coverage file {coverage_path}", flush=True)
            time.sleep(args.poll_interval)
            continue

        if args.taint and not regions_path.is_file():
            print(f"[monitor.py] waiting for writable regions file {regions_path}", flush=True)
            time.sleep(args.poll_interval)
            continue

        if args.taint and not snapshot_path.is_file():
            print(f"[monitor.py] waiting for snapshot file {snapshot_path}", flush=True)
            time.sleep(args.poll_interval)
            continue

        pairs = check_in_dir(in_dir)
        if not pairs:
            print(f"[monitor.py] waiting for interesting input pairs in {in_dir}", flush=True)
            time.sleep(args.poll_interval)
            continue

        pair = selection.select_input(pairs)
        if pair is None:
            time.sleep(args.poll_interval)
            continue

        print(f"[monitor.py] selected input {pair.index}")

        # Both of these are converted to the ghidra block definitions since they are not 1:1
        coverage = reduce_coverage(basic_block_model, args.coverage)
        trace = reduce_trace(basic_block_model, pair.trace_path)

        branches = selection.filter_interesting(cfg, trace, coverage)
        
        # No frontier nodes are available from this input yet; coverage may still be warming up.
        if not branches:
            time.sleep(args.poll_interval)
            continue

        branch, tainted_bytes = selection.select_branch(basic_block_model, args.binary, pair.raw_path, pair.trace_path, cfg, branches, coverage, args.coverage, args.regions, args.snapshot, args.taint)

        # No suitable branch, this can happen with taint analysis enabled if no taint is detected in any branch
        if branch is None:
            selection.ignore_input(pair)
            continue

        target.decode(pair.raw_path, input_path)

        report = context.initial_report(basic_block_model, input_path, branch.edge, tainted_bytes)
        if report is None:
            continue

        try:
            response = agent.prompt_agent(llm, report)
            input_path.write_text(str(response), encoding="utf-8")
            output_path = out_dir / f"mutated{out_cnt}.raw"
            tmp_output_path = out_dir / f".mutated{out_cnt}.raw.tmp"
            target.encode(input_path, tmp_output_path)
            os.replace(tmp_output_path, output_path)
            out_cnt = out_cnt + 1
        except Exception as exc:
            print(f"[monitor.py] agent failed with {exc}", file=sys.stderr, flush=True)


def main():
    args = parse_args()
    resolve_common_defaults(args)
        
    target = Path(args.binary).resolve()
    target_base = "/" + target.name

    project_path = Path(args.project_dir).resolve()
    project_path.mkdir(parents=True, exist_ok=True)

    print(f"[monitor.py] analyzing {target} in {project_path}")

    pyghidra.start()

    from ghidra.program.util import GhidraProgramUtilities
    from ghidra.program.model.block import BasicBlockModel

    with pyghidra.open_project(project_path, "test", create=True) as project:
        program, consumer, existed = load_or_import_program(project, target, target_base)

        try:
            if existed:
                print("[monitor.py] loaded existing program")
            else:
                print("[monitor.py] opened new file")
            
            if not GhidraProgramUtilities.isAnalyzed(program):
                print("[monitor.py] analyzing...")
                log = pyghidra.analyze(program, pyghidra.task_monitor(600))
                program.save("PyGhidra analysis", pyghidra.task_monitor())
                if log:
                    print(log)
                print("[monitor.py] analyzed.")
            else:
                print("[monitor.py] already analyzed.")

            basic_block_model = BasicBlockModel(program)

            if args.command == "analyze":
                if args.parser_search:
                    parsers.parser_search(program)
                if args.dump_cfg:
                    create_cfg_func(program, args.work_dir)
                    create_cfg_bb(basic_block_model, args.work_dir)
                if args.generate_rag:
                    context.generate_rag(program)
                    
            elif args.command == "run":
                cfg = create_cfg_bb(basic_block_model, args.work_dir)
                llm = agent.initialize_agentic_setup(
                    model=args.model,
                    storage_dir=Path(args.work_dir) / "agentic-crewai-data",
                )
                monitor_loop(llm, basic_block_model, cfg, args)
            
        finally:
            program.release(consumer)

if __name__ == "__main__":
    main()
