import pyghidra
import re

def _address_in_range(addr, start, end):
    if addr is None or start is None or end is None:
        return False

    try:
        return start.compareTo(addr) <= 0 and end.compareTo(addr) >= 0
    except Exception:
        return False

def _clang_line_contains_address(line, addr):
    try:
        tokens = line.getAllTokens()
    except Exception:
        return False

    for token in tokens:
        try:
            if _address_in_range(addr, token.getMinAddress(), token.getMaxAddress()):
                return True
        except Exception:
            continue

    return False

def _line_to_string(line):
    try:
        return line.toString()
    except Exception:
        return str(line)

# Give ability to get function from either a block within the function, or the function itself
def get_function_source(block, function, branch_addr=None):
    from ghidra.app.decompiler import DecompInterface

    program = None
    if not block == None:
        program = block.getModel().getProgram()
        function = program.getFunctionManager().getFunctionContaining(block.getFirstStartAddress())
    else:
        program = function.getProgram()

    if function is None:
        print("[context.py] function is none")
        return None

    ifc = DecompInterface()
    ifc.openProgram(program)

    results = ifc.decompileFunction(function, 10, pyghidra.task_monitor())

    if not results.decompileCompleted():
        print("[context.py] decompilation has failed")
        return None

    source = results.getDecompiledFunction().getC()
    if branch_addr is None:
        return source

    try:
        if not hasattr(branch_addr, "compareTo"):
            branch_addr = (
                program.getAddressFactory()
                .getDefaultAddressSpace()
                .getAddress(branch_addr)
            )

        from ghidra.app.decompiler.component import DecompilerUtils

        lines = DecompilerUtils.toLines(results.getCCodeMarkup())
        marked = []
        found = False

        for line in lines:
            text = _line_to_string(line)
            if _clang_line_contains_address(line, branch_addr):
                marked.append(f">>> {text}")
                found = True
            else:
                marked.append(text)

        if found:
            return "\n".join(marked)
    except Exception as exc:
        print(f"[context.py] failed to mark branch in decompilation: {exc}")

    return source

def _get_code_block_containing(basic_block_model, addr):
    blocks = basic_block_model.getCodeBlocksContaining(addr, pyghidra.task_monitor())
    if len(blocks) == 0:
        return None

    if len(blocks) > 1:
        print(f"[context.py] address {addr} exists multiple times in basic block model")

    return blocks[0]

def get_block_asm(block, addr=None):
    if addr is not None:
        block = _get_code_block_containing(block, addr)
        if block is None:
            return None

    listing = block.getModel().getProgram().getListing()

    start = block.getFirstStartAddress()
    end = block.getMaxAddress()

    instr = listing.getInstructionAt(start)

    lines = []
    while instr is not None and instr.getAddress().compareTo(end) <= 0:
        lines.append(f"{instr.getAddress()}: {instr}")
        instr = instr.getNext()

    return lines

def initial_report(basic_block_model, input, branch, tainted_bytes):
    with open(input, "r") as file:
        function_manager = basic_block_model.getProgram().getFunctionManager()

        saddr = basic_block_model.getProgram().getAddressFactory().getDefaultAddressSpace().getAddress(branch[0])
        daddr = basic_block_model.getProgram().getAddressFactory().getDefaultAddressSpace().getAddress(branch[1])

        src = _get_code_block_containing(basic_block_model, saddr)
        dst = _get_code_block_containing(basic_block_model, daddr)

        if src is None or dst is None:
            print(f"[context.py] src {hex(branch[0])} or dst {hex(branch[1])} failed")
            return None

        src_c = get_function_source(src, None, branch[0])

        src_asm = get_block_asm(basic_block_model, saddr)
        dst_asm = get_block_asm(basic_block_model, daddr)
        if src_asm is None or dst_asm is None:
            print(f"[context.py] asm for src {hex(branch[0])} or dst {hex(branch[1])} failed")
            return None

        src_asm_formatted = "\n".join(src_asm)

        if src_c is None:
            print(f"[context.py] failed on function {hex(branch[0])}")
            return None
        
        context_chunks = [""]
        context_chunks += [f"Input:\n{file.read()}\n"]
        if tainted_bytes:
            context_chunks += [f"Most interesting raw byte ranges:\n{tainted_bytes}\n"]
        context_chunks += [f"Block asm with target branch:\n{src_asm_formatted}\n"]
        context_chunks += [f"Intended result of conditional branch:\t{hex(branch[1])}\n"]
        context_chunks += [f"Function source:\n{src_c}\n"]

        context = "\n\n".join(context_chunks)

    return context
