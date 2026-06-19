import pyghidra
import math
import networkx as nx

from dataclasses import dataclass

def looped_switch_count(high_func):
    from ghidra.program.model.pcode import PcodeBlock, PcodeOp

    block_list = high_func.getBasicBlocks()
    blocks = [block_list.get(i) for i in range(block_list.size())]
    if not blocks:
        return 0

    def block_key(block):
        index = block.getIndex()
        if index >= 0:
            return ("idx", int(index))
        return ("range", str(block.getStart()), str(block.getStop()))

    known_blocks = {block_key(block) for block in blocks}

    cfg = nx.DiGraph()
    cfg.add_nodes_from(known_blocks)
    for block in blocks:
        src = block_key(block)
        for i in range(block.getOutSize()):
            dst = block.getOut(i)
            if dst is None:
                continue
            dst_key = block_key(dst)
            if dst_key in known_blocks:
                cfg.add_edge(src, dst_key)

    loop_block_keys = set()
    for component in nx.strongly_connected_components(cfg):
        if len(component) > 1:
            loop_block_keys.update(component)
        else:
            node = next(iter(component))
            if cfg.has_edge(node, node):
                loop_block_keys.add(node)

    loop_types = {PcodeBlock.WHILEDO, PcodeBlock.DOWHILE, PcodeBlock.INFLOOP}

    def has_loop_parent(block):
        current = block
        while current is not None:
            if current.getType() in loop_types:
                return True
            current = current.getParent()
        return False

    for block in blocks:
        if has_loop_parent(block):
            loop_block_keys.add(block_key(block))

    if not loop_block_keys:
        return 0

    def block_containing(addr):
        if addr is None:
            return None
        for block in blocks:
            if block.contains(addr):
                return block
        return None

    switch_sites = set()

    def add_switch_site(block, addr):
        if block is None or block_key(block) not in loop_block_keys:
            return
        if addr is None:
            switch_sites.add(("block", block_key(block)))
        else:
            switch_sites.add(("addr", str(addr)))

    for jump_table in high_func.getJumpTables():
        if jump_table is None or jump_table.isEmpty():
            continue
        switch_addr = jump_table.getSwitchAddress()
        add_switch_site(block_containing(switch_addr), switch_addr)

    for block in blocks:
        if block_key(block) not in loop_block_keys:
            continue

        saw_indirect_branch = False
        op_iter = block.getIterator()
        while op_iter.hasNext():
            op = op_iter.next()
            if op.getOpcode() != PcodeOp.BRANCHIND:
                continue

            saw_indirect_branch = True
            add_switch_site(block, op.getSeqnum().getTarget())

        if not saw_indirect_branch and block.getOutSize() > 2:
            last_op = block.getLastOp()
            addr = last_op.getSeqnum().getTarget() if last_op is not None else None
            add_switch_site(block, addr)

    return len(switch_sites)

def branch_factor(high_func):
    from ghidra.program.model.pcode import HighLocal, PcodeOp

    counts_by_var = {}

    def variable_key(high_var):
        symbol = high_var.getSymbol()
        if symbol is not None:
            return ("symbol", int(symbol.getId()))

        rep = high_var.getRepresentative()
        if rep is None:
            return ("name", high_var.getName())
        return (
            "rep",
            high_var.getName(),
            str(rep.getAddress()),
            int(rep.getSize()),
        )

    def is_counted_variable(high_var):
        if high_var is None:
            return False
        if not isinstance(high_var, HighLocal):
            return False

        symbol = high_var.getSymbol()
        return symbol is None or not symbol.isGlobal()

    def varnode_key(varnode):
        return (
            str(varnode.getAddress()),
            str(varnode.getPCAddress()),
            int(varnode.getSize()),
        )

    def condition_variables(varnode, visited=None):
        if varnode is None or varnode.isConstant():
            return set()
        if visited is None:
            visited = set()

        key = varnode_key(varnode)
        if key in visited:
            return set()
        visited.add(key)

        high_var = varnode.getHigh()
        if is_counted_variable(high_var):
            return {variable_key(high_var)}

        defining_op = varnode.getDef()
        if defining_op is None:
            return set()

        variables = set()
        for i in range(defining_op.getNumInputs()):
            variables.update(condition_variables(defining_op.getInput(i), visited))
        return variables

    block_list = high_func.getBasicBlocks()
    for i in range(block_list.size()):
        block = block_list.get(i)
        op_iter = block.getIterator()
        while op_iter.hasNext():
            op = op_iter.next()
            if op.getOpcode() != PcodeOp.CBRANCH or op.getNumInputs() < 2:
                continue

            for var_key in condition_variables(op.getInput(1)):
                counts_by_var[var_key] = counts_by_var.get(var_key, 0) + 1

    if not counts_by_var:
        return 0
    return max(counts_by_var.values())

def in_edges(high_func):
    block_list = high_func.getBasicBlocks()
    if block_list.size() == 0:
        return 0

    return max(block_list.get(i).getInSize() for i in range(block_list.size()))

def param_switch_count(high_func):
    from ghidra.program.model.pcode import HighParam, PcodeOp

    block_list = high_func.getBasicBlocks()
    blocks = [block_list.get(i) for i in range(block_list.size())]
    if not blocks:
        return 0

    def block_key(block):
        index = block.getIndex()
        if index >= 0:
            return ("idx", int(index))
        return ("range", str(block.getStart()), str(block.getStop()))

    def varnode_key(varnode):
        return (
            str(varnode.getAddress()),
            str(varnode.getPCAddress()),
            int(varnode.getSize()),
        )

    def flows_from_param(varnode, visited=None):
        if varnode is None or varnode.isConstant():
            return False
        if visited is None:
            visited = set()

        key = varnode_key(varnode)
        if key in visited:
            return False
        visited.add(key)

        high_var = varnode.getHigh()
        if isinstance(high_var, HighParam):
            return True
        if high_var is not None:
            symbol = high_var.getSymbol()
            if symbol is not None and symbol.isParameter():
                return True

        defining_op = varnode.getDef()
        if defining_op is None:
            return False

        for i in range(defining_op.getNumInputs()):
            if flows_from_param(defining_op.getInput(i), visited):
                return True
        return False

    def op_flows_from_param(op):
        if op is None:
            return False
        for i in range(op.getNumInputs()):
            if flows_from_param(op.getInput(i)):
                return True
        return False

    def block_containing(addr):
        if addr is None:
            return None
        for block in blocks:
            if block.contains(addr):
                return block
        return None

    switch_sites = set()

    def add_switch_site(block, addr):
        if addr is None:
            switch_sites.add(("block", block_key(block)))
        else:
            switch_sites.add(("addr", str(addr)))

    for jump_table in high_func.getJumpTables():
        if jump_table is None or jump_table.isEmpty():
            continue

        switch_addr = jump_table.getSwitchAddress()
        if switch_addr is None:
            continue

        op_iter = high_func.getPcodeOps(switch_addr)
        while op_iter.hasNext():
            op = op_iter.next()
            if op.getOpcode() == PcodeOp.BRANCHIND and op_flows_from_param(op):
                add_switch_site(block_containing(switch_addr), switch_addr)
                break

    for block in blocks:
        saw_indirect_branch = False
        op_iter = block.getIterator()
        while op_iter.hasNext():
            op = op_iter.next()
            if op.getOpcode() != PcodeOp.BRANCHIND:
                continue

            saw_indirect_branch = True
            if op_flows_from_param(op):
                add_switch_site(block, op.getSeqnum().getTarget())

        if not saw_indirect_branch and block.getOutSize() > 2:
            last_op = block.getLastOp()
            if op_flows_from_param(last_op):
                addr = last_op.getSeqnum().getTarget() if last_op is not None else None
                add_switch_site(block, addr)

    return len(switch_sites)


@dataclass(frozen=True)
class FunctionStats:
    name: str
    looped_switches: int
    max_branch_factor: int
    in_count: int
    bb_count: int
    call_count: int
    param_switches: int

def reciprocal_rank_fusion(ranked_lists, K=100, k=20, min_support=2):
    scores = {}
    support = {}

    for field, ranked in ranked_lists:
        prev_value = None
        rank = 0

        for i, f in enumerate(ranked[:K], start=1):
            value = getattr(f, field)

            if value <= 0:
                continue

            if value != prev_value:
                rank = i
                prev_value = value

            scores[f.name] = scores.get(f.name, 0.0) + (1.0 / (k + rank))
            support[f.name] = support.get(f.name, 0) + 1

    return sorted(
        [
            (name, scores[name], support[name])
            for name in scores
            if support[name] >= min_support
        ],
        key=lambda x: (-x[1], -x[2])
    )

def parser_search(program):
    from ghidra.app.decompiler import DecompInterface
    ifc = DecompInterface()
    if ifc.openProgram(program) == False:
        print("[parser.py] failed to open program in decompiler interface")

    function_stats = []
    function_manager = program.getFunctionManager()
    for function in function_manager.getFunctions(True):
        res = ifc.decompileFunction(function, 30, pyghidra.task_monitor())
        high_func = res.getHighFunction()

        if high_func is None:
            continue

        name = f'{hex(function.getEntryPoint().getOffset())}: {function.getName()}'

        stat = FunctionStats(
            name,
            looped_switch_count(high_func),
            branch_factor(high_func),
            in_edges(high_func),
            len(high_func.getBasicBlocks()),
            len(function.getCallingFunctions(pyghidra.task_monitor())),
            param_switch_count(high_func)
        )

        function_stats = function_stats + [stat]

    stats_looped_switches = sorted(function_stats, key=lambda f: f.looped_switches, reverse=True)
    stats_branch_factor = sorted(function_stats, key=lambda f: f.max_branch_factor, reverse=True)
    stats_in_count = sorted(function_stats, key=lambda f: f.in_count, reverse=True)
    stats_bb_count = sorted(function_stats, key=lambda f: f.bb_count, reverse=True)
    stats_call_count = sorted(function_stats, key=lambda f: f.call_count, reverse=True)
    stats_param_switches = sorted(function_stats, key=lambda f: f.param_switches, reverse=True)

    ranked_lists = [
        ("looped_switches", stats_looped_switches),
        ("max_branch_factor", stats_branch_factor),
        ("in_count", stats_in_count),
        ("bb_count", stats_bb_count),
        ("call_count", stats_call_count),
        ("param_switches", stats_param_switches),
    ]

    ranked_parsers = reciprocal_rank_fusion(
        ranked_lists,
        K=math.isqrt(len(function_stats)),
        k=20,
        min_support=3,
    )

    return ranked_parsers
