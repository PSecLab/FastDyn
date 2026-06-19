import sys
import pyghidra
import argparse
import socket
import struct

from elftools.elf.elffile import ELFFile
from elftools.elf.constants import P_FLAGS
from multiprocessing import get_context
from collections import defaultdict, deque
from pathlib import Path
from triton import *

SNAPSHOT_REG_COUNT = 16
GDB_MEMORY_READ_CHUNK_SIZE = 0x1000
INPUT_TAINT_CHUNK_SIZE = 8
DEFAULT_WORK_DIR = "fastdyn_work"

# Input from harness generation
TRITON_TARGET_INPUT_SIZE = 291

def _triton_target_input_size():
    if TRITON_TARGET_INPUT_SIZE is None:
        return None

    return max(0, int(TRITON_TARGET_INPUT_SIZE))

def triton_target_input_bytes(input_bytes):
    """Return input bytes zero-padded or truncated to TRITON_TARGET_INPUT_SIZE."""
    data = bytes(input_bytes)
    size = _triton_target_input_size()
    if size is None:
        return data

    return data[:size].ljust(size, b"\x00")

def triton_target_input_range(start, end):
    """Clamp an input byte offset range to TRITON_TARGET_INPUT_SIZE when known."""
    start = max(0, int(start))
    end = max(start, int(end))

    size = _triton_target_input_size()
    if size is not None:
        end = min(end, size)

    return range(start, end)

# Input from harness generation
def triton_write_target_input(ctx, snap, input_bytes):
    base = snap["registers"][2]
    _set_triton_memory(ctx, base, triton_target_input_bytes(input_bytes))

# Input from harness generation
def triton_taint_target_input_range(ctx, snap, start, end):
    base = snap["registers"][2]
    for offset in triton_target_input_range(start, end):
        ctx.taintMemory(MemoryAccess(base + offset, CPUSIZE.BYTE))

def load_non_writable_segments(elf_path: str | Path):
    """
    Return loadable, non-writable ELF segments as:
        {
            "vaddr": virtual/load address,
            "memsz": memory size after loading,
            "filesz": initialized bytes from file,
            "flags": segment flags,
            "data": initialized file bytes padded to memsz with zeros
        }

    For normal code/rodata segments, filesz == memsz.
    If filesz < memsz, the extra bytes are loader-zeroed.
    """
    elf_path = Path(elf_path)

    segments = []

    with elf_path.open("rb") as f:
        elf = ELFFile(f)

        for seg in elf.iter_segments():
            if seg["p_type"] != "PT_LOAD":
                continue

            flags = seg["p_flags"]

            # # Skip writable segments.
            # if flags & P_FLAGS.PF_W:
            #     continue

            vaddr = seg["p_vaddr"]
            paddr = seg["p_paddr"]
            memsz = seg["p_memsz"]
            filesz = seg["p_filesz"]

            data = seg.data()

            # Loader initializes p_filesz bytes from the file, then zero-fills
            # p_memsz - p_filesz bytes.
            if len(data) < memsz:
                data = data + b"\x00" * (memsz - len(data))

            #print(f"Segment:\n\tvaddr - {hex(vaddr)}\n\tpaddr - {hex(paddr)}\n\tsize - {hex(memsz)}\n")

            segments.append({
                "vaddr": vaddr,
                "paddr": paddr,
                "memsz": memsz,
                "filesz": filesz,
                "flags": flags,
                "data": data,
            })

    return segments

def load_writable_ranges(regions_path):
    regions = []

    with open(regions_path, "r") as file:
        for line_number, line in enumerate(file, 1):
            line = line.partition("#")[0].strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) < 2:
                raise ValueError(f"{regions_path}:{line_number}: expected address and size")

            addr = int(parts[0], 16)
            size = int(parts[1], 16)
            if addr < 0 or size < 0:
                raise ValueError(f"{regions_path}:{line_number}: negative address or size")

            regions.append((addr, size))

    return regions

def _triton_arm_register(ctx, index):
    register_names = {
        13: ("sp", "r13"),
        14: ("lr", "r14"),
        15: ("pc", "r15"),
    }.get(index, (f"r{index}",))

    for name in register_names:
        if hasattr(ctx.registers, name):
            return getattr(ctx.registers, name)

    raise AttributeError(f"Triton ARM register {index} not found")

def _set_triton_memory(ctx, addr, data):
    if not data:
        return

    if hasattr(ctx, "setConcreteMemoryAreaValue"):
        ctx.setConcreteMemoryAreaValue(addr, bytes(data))
        return

    for offset, value in enumerate(data):
        ctx.setConcreteMemoryValue(addr + offset, value)

def _snapshot_known_memory_size(regions):
    return sum(size for _, size in regions)

def load_memory_register_snapshot(
    ctx,
    regions_path,
    snapshot_path,
    load_pc=True,
):
    """
    Load fuzz.c's snapshot.bin into Triton: 16 little-endian ARM registers,
    followed by each saved region listed in regions_path.
    """
    regions = load_writable_ranges(regions_path)
    snapshot = Path(snapshot_path).read_bytes()
    register_header_size = SNAPSHOT_REG_COUNT * 4

    if len(snapshot) < register_header_size:
        raise ValueError(f"{snapshot_path}: snapshot is too small for register header")

    registers = list(struct.unpack(f"<{SNAPSHOT_REG_COUNT}I", snapshot[:register_header_size]))
    memory_size = _snapshot_known_memory_size(regions)
    expected_size = register_header_size + memory_size
    if len(snapshot) != expected_size:
        raise ValueError(
            f"{snapshot_path}: expected {expected_size} bytes from regions, "
            f"got {len(snapshot)}"
        )

    offset = register_header_size
    for addr, size in regions:
        _set_triton_memory(ctx, addr, snapshot[offset:offset + size])
        offset += size

    for index, value in enumerate(registers):
        if index == 15 and not load_pc:
            continue
        ctx.setConcreteRegisterValue(_triton_arm_register(ctx, index), value)

    return {
        "registers": registers,
        "regions": regions,
        "memory_size": memory_size,
    }

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

def _coalesce_byte_ranges(offsets):
    ranges = []

    for offset in sorted(set(offsets)):
        if not ranges or offset != ranges[-1][1]:
            ranges.append([offset, offset + 1])
        else:
            ranges[-1][1] = offset + 1

    return [(start, end) for start, end in ranges]

def _format_byte_ranges(ranges):
    if not ranges:
        return "[]"

    return "[" + ", ".join(
        f"{start}" if end == start + 1 else f"{start}-{end - 1}"
        for start, end in ranges
    ) + "]"

def triton_get_taint(binary, raw, trace, branch, regions, snapshot):
    tainted_offsets = []

    try:
        with open(raw, "rb") as file:
            input_bytes = file.read()

        segments = load_non_writable_segments(binary)

        for chunk_start in range(0, len(input_bytes), INPUT_TAINT_CHUNK_SIZE):
            chunk_end = min(chunk_start + INPUT_TAINT_CHUNK_SIZE, len(input_bytes))

            try:
                ctx = TritonContext()
                ctx.setArchitecture(ARCH.ARM32)
                ctx.setMode(MODE.ALIGNED_MEMORY, True)
                ctx.setMode(MODE.ONLY_ON_TAINTED, True)

                for seg in segments:
                    ctx.setConcreteMemoryAreaValue(seg["vaddr"], seg["data"])

                snap = load_memory_register_snapshot(ctx, regions, snapshot)

                ctx.setConcreteRegisterValue(ctx.registers.pc, trace[0][0] | 1)

                triton_write_target_input(ctx, snap, input_bytes)
                triton_taint_target_input_range(ctx, snap, chunk_start, chunk_end)

            except Exception as exc:
                print(
                    f"[flow.py] Triton exception setting up taint chunk "
                    f"{chunk_start}-{chunk_end - 1}: {exc}",
                    flush=True,
                )
                continue

            for pc, opcode in trace:
                inst = Instruction()

                try:
                    inst.setAddress(pc)
                    inst.setOpcode(bytes((b & 0xFF) for b in opcode))
                except Exception as exc:
                    print(
                        f"[flow.py] Triton exception building instruction "
                        f"at pc={pc:#x}: {exc}",
                        flush=True,
                    )
                    continue

                try:
                    expected_pc = ctx.getConcreteRegisterValue(ctx.registers.pc)
                    if expected_pc != pc:
                        # print(f"PC drift: Triton has {expected_pc:#x}, trace has {pc:#x}")
                        ctx.setConcreteRegisterValue(ctx.registers.pc, pc | 1)
                except Exception as exc:
                    print(
                        f"[flow.py] Triton exception getting concrete register "
                        f"value at pc={pc:#x}: {exc}",
                        flush=True,
                    )

                try:
                    ctx.processing(inst)
                except Exception as exc:
                    print(f"[flow.py] Triton exception at pc={pc:#x}: {exc}", flush=True)
                    continue

                if pc == branch:
                    try:
                        expressions = inst.getSymbolicExpressions()
                        if expressions and expressions[0].isTainted():
                            tainted_offsets.extend(range(chunk_start, chunk_end))
                    except Exception as exc:
                        print(
                            f"[flow.py] Triton exception evaluating taint for "
                            f"branch {pc:#x}: {exc}",
                            flush=True,
                        )
                    break

    except Exception as exc:
        print(f"[flow.py] Larger exception catcher: {exc}", flush=True)

    return _format_byte_ranges(_coalesce_byte_ranges(tainted_offsets))

def triton_is_tainted(binary, raw, trace, branches, regions, snapshot):
    try:
        ctx = TritonContext()
        ctx.setArchitecture(ARCH.ARM32)
        ctx.setMode(MODE.ALIGNED_MEMORY, True)
        ctx.setMode(MODE.ONLY_ON_TAINTED, True)

        for seg in load_non_writable_segments(binary):
            ctx.setConcreteMemoryAreaValue(seg["vaddr"], seg["data"])

        snap = load_memory_register_snapshot(ctx, regions, snapshot)

        ctx.setConcreteRegisterValue(ctx.registers.pc, trace[0][0] | 1)

        with open(raw, "rb") as file:
            input_bytes = file.read()

        triton_write_target_input(ctx, snap, input_bytes)
        triton_taint_target_input_range(ctx, snap, 0, len(input_bytes))

        tainted = {}
        for pc, opcode in trace:
            inst = Instruction()
            inst.setAddress(pc)
            inst.setOpcode(opcode)

            try:
                expected_pc = ctx.getConcreteRegisterValue(ctx.registers.pc)
                if expected_pc not in (pc, pc | 1):
                    # print(f"PC drift: Triton has {expected_pc:#x}, trace has {pc:#x}")
                    ctx.setConcreteRegisterValue(ctx.registers.pc, pc | 1)
            except Exception as exc:
                print(f"[flow.py] Triton exception getting concrete register value at pc={pc:#x}: {exc}", flush=True)

            try:
                ctx.processing(inst)
            except Exception as exc:
                print(f"[flow.py] Triton exception at pc={pc:#x}: {exc}", flush=True)

            if pc in branches:
                try:
                    tainted[pc] = ctx.isRegisterTainted(ctx.registers.pc)
                except Exception as exc:
                    print(f"[flow.py] Triton exception at evaluating taint for branch {pc:#x}: {exc}", flush=True)
                    tainted[pc] = False
                branches.remove(pc)

            if len(branches) == 0:
                break

    except Exception as exc:
        print(f"[flow.py] Larger exception catcher: {exc}")

    for branch in branches:
        tainted[branch] = False

    return tainted

def make_trace_pickleable(trace):
    fixed = []

    for pc, opcode in trace:
        fixed.append((
            int(pc),
            bytes((b & 0xff) for b in opcode)
        ))

    return fixed

def triton_get_taint_worker(args):
    binary, raw, trace, branch, regions, snapshot = args
    return triton_get_taint(binary, raw, trace, branch, regions, snapshot)

def run_triton_get_taint_subprocess(binary, raw, trace, branch, regions, snapshot):
    mp = get_context("spawn")

    fixed = make_trace_pickleable(trace)

    with mp.Pool(processes=1, maxtasksperchild=1) as pool:
        return pool.apply(
            triton_get_taint_worker,
            ((binary, raw, fixed, branch, regions, snapshot),),
        )

def triton_is_tainted_worker(args):
    binary, raw, trace, branches, regions, snapshot = args
    return triton_is_tainted(binary, raw, trace, branches, regions, snapshot)

def run_triton_is_tainted_subprocess(binary, raw, trace, branches, regions, snapshot):
    mp = get_context("spawn")

    fixed = make_trace_pickleable(trace)

    with mp.Pool(processes=1, maxtasksperchild=1) as pool:
        return pool.apply(
            triton_is_tainted_worker,
            ((binary, raw, fixed, branches, regions, snapshot),),
        )
    
def norm(addr):
    return addr & ~1

def pop_return(call_stack, target):
    target = norm(target)

    if call_stack and norm(call_stack[-1]["return_addr"]) == target:
        call_stack.pop()
        return True

    # Optional repair if stack is slightly out of sync.
    for i in range(len(call_stack) - 1, -1, -1):
        if norm(call_stack[i]["return_addr"]) == target:
            del call_stack[i:]
            return True

    return False

def collapse_repeated_runs(xs):
    xs = list(xs)
    out = []
    i = 0

    while i < len(xs):
        collapsed = False

        # Try the longest possible repeated block first
        max_len = (len(xs) - i) // 2

        for n in range(max_len, 0, -1):
            block = xs[i:i+n]
            next_block = xs[i+n:i+2*n]

            if block == next_block:
                out.extend(block)

                # Skip all consecutive repeats of this block
                i += n
                while xs[i:i+n] == block:
                    i += n

                collapsed = True
                break

        if not collapsed:
            out.append(xs[i])
            i += 1

    return out

# Uses the concrete execution from triton to produce a rough callstack.
# Assumes that larget callstacks may have had some edge case affecting analysis and drops the stack
def triton_get_callstack(binary, raw, trace, branch, regions, snapshot):
    call_stack = []

    try:
        ctx = TritonContext()
        ctx.setArchitecture(ARCH.ARM32)
        ctx.setMode(MODE.ALIGNED_MEMORY, True)
        ctx.setMode(MODE.ONLY_ON_TAINTED, True)

        for seg in load_non_writable_segments(binary):
            ctx.setConcreteMemoryAreaValue(seg["vaddr"], seg["data"])

        snap = load_memory_register_snapshot(ctx, regions, snapshot)

        ctx.setConcreteRegisterValue(ctx.registers.pc, trace[0][0] | 1)

        with open(raw, "rb") as file:
            input_bytes = file.read()

        for i, b in enumerate(input_bytes):
            addr = snap["registers"][2] + i
            ctx.setConcreteMemoryValue(addr, b)
            ctx.taintMemory(MemoryAccess(addr, CPUSIZE.BYTE))

        for pc, opcode in trace:
            inst = Instruction()
            inst.setAddress(pc)
            inst.setOpcode(opcode)

            if (pc & ~1) == (branch & ~1):
                for frame in call_stack:
                    for item in frame:
                        frame[item] = hex(frame[item])
                if len(call_stack) > 30: # Could have had a problem in analysis
                    print(f"Before {len(call_stack)}")
                    print(call_stack)
                    call_stack = collapse_repeated_runs(call_stack)
                    print(f"After {len(call_stack)}")

                return call_stack

            try:
                expected_pc = ctx.getConcreteRegisterValue(ctx.registers.pc)
                lr_before = ctx.getConcreteRegisterValue(ctx.registers.r14)

                if norm(expected_pc) != norm(pc):
                    # QEMU/snapshot forced a function return by setting PC to LR.
                    if norm(pc) == norm(lr_before):
                        pop_return(call_stack, pc)
                    ctx.setConcreteRegisterValue(ctx.registers.pc, pc | 1)
            except Exception as exc:
                print(f"[flow.py] Triton exception getting concrete register value at pc={pc:#x}: {exc}", flush=True)
                continue

            try:
                ctx.processing(inst)
            except Exception as exc:
                print(f"[flow.py] Triton exception at pc={pc:#x}: {exc}", flush=True)
                continue

            pc_after = ctx.getConcreteRegisterValue(ctx.registers.pc)
            lr_after = ctx.getConcreteRegisterValue(ctx.registers.r14)
            sp_after = ctx.getConcreteRegisterValue(ctx.registers.sp)

            disasm = inst.getDisassembly()

            if disasm.startswith(("bl ", "blx ")):
                call_stack.append({
                    "call_site": pc,
                    "return_addr": lr_after & ~1,
                    "target": pc_after & ~1,
                    "sp": sp_after,
                })

            elif call_stack and norm(pc_after) == norm(call_stack[-1]["return_addr"]):
                call_stack.pop()

    except Exception as exc:
        print(f"[flow.py] Larger exception catcher: {exc}")

    print(":(")
    return []

def triton_get_callstack_worker(args):
    binary, raw, trace, branches, regions, snapshot = args
    return triton_get_callstack(binary, raw, trace, branches, regions, snapshot)

def run_triton_get_callstack_subprocess(binary, raw, trace, branches, regions, snapshot):
    mp = get_context("spawn")

    fixed = make_trace_pickleable(trace)

    with mp.Pool(processes=1, maxtasksperchild=1) as pool:
        return pool.apply(
            triton_get_callstack_worker,
            ((binary, raw, fixed, branches, regions, snapshot),),
        )      

def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("binary", help="Path to binary to analyze")

    parser.add_argument("--input", required=True)
    parser.add_argument("--trace", required=True)
    parser.add_argument("--branch", required=True)
    parser.add_argument("--coverage", required=True)
    parser.add_argument("--regions", required=True)
    parser.add_argument("--snapshot", required=True)
    parser.add_argument("--work-dir", default=DEFAULT_WORK_DIR)
    # parser.add_argument("--port", required=True)

    return parser.parse_args()

def main():
    args = parse_args()
        
    target = Path(args.binary).resolve()
    target_base = "/" + target.name

    project_path = (Path(args.work_dir).resolve() / "ghidra_projects")
    project_path.mkdir(parents=True, exist_ok=True)

    print(f"[triton.py] analyzing {target} in {project_path}")

    pyghidra.start()

    from ghidra.program.util import GhidraProgramUtilities
    from ghidra.program.model.block import BasicBlockModel

    with pyghidra.open_project(project_path, "test", create=True) as project:
        program, consumer, existed = load_or_import_program(project, target, target_base)

        try:
            if existed:
                print("[triton.py] loaded existing program")
            else:
                print("[triton.py] opened new file")
            
            if not GhidraProgramUtilities.isAnalyzed(program):
                print("[triton.py] analyzing...")
                log = pyghidra.analyze(program, pyghidra.task_monitor(600))
                program.save("PyGhidra analysis", pyghidra.task_monitor())
                if log:
                    print(log)
                print("[triton.py] analyzed.")
            else:
                print("[triton.py] already analyzed.")
                    
            triton_run_taint(
                program,
                args.binary,
                args.input,
                args.trace,
                args.coverage,
                args.branch,
                args.regions,
                args.snapshot,
            )
            
        finally:
            program.release(consumer)

if __name__ == "__main__":
    main()
