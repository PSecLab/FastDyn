"""
nextpc.py

Library to compute the set of possible next instruction PCs
for a given instruction, using Capstone.

Public API:
    next_pcs(...)
"""

from typing import Set
from capstone import *
from capstone.arm import *
from capstone.arm64 import *


# ======================
# Public API
# ======================

def next_pcs(
    *,
    blob: bytes,
    pc: int,
    base_va: int,
    arch: str,
    mode: str = "thumb",
) -> Set[int]:
    """
    Compute the set of possible next PCs for the instruction at `pc`.

    Parameters
    ----------
    blob : bytes
        Binary blob containing code
    pc : int
        Current program counter
    base_va : int
        Virtual address corresponding to blob[0]
    arch : str
        "arm32" or "arm64"
    mode : str
        For arm32: "arm" or "thumb"

    Returns
    -------
    Set[int]
        Set of possible next instruction PCs
    """

    offset = pc - base_va
    if offset < 0 or offset >= len(blob):
        raise ValueError("PC outside provided blob")

    if arch == "arm32":
        return _next_pcs_arm32(blob, pc, base_va, mode)
    elif arch == "arm64":
        return _next_pcs_arm64(blob, pc, base_va)
    else:
        raise ValueError(f"Unsupported arch: {arch}")


# ======================
# ARM32 implementation
# ======================

def _next_pcs_arm32(
    blob: bytes,
    pc: int,
    base_va: int,
    mode: str,
) -> Set[int]:

    cs = Cs(
        CS_ARCH_ARM,
        CS_MODE_ARM if mode == "arm" else CS_MODE_THUMB
    )
    cs.detail = True

    offset = pc - base_va
    insns = list(cs.disasm(blob[offset:], pc, count=1))
    if not insns:
        return set()

    insn = insns[0]
    next_set: Set[int] = set()

    fallthrough = pc + insn.size

    # Branch instructions
    if insn.id in (
        ARM_INS_B,
        ARM_INS_BL,
        ARM_INS_BLX,
        ARM_INS_BX,
    ):
        target = _arm32_branch_target(insn)
        if target is not None:
            next_set.add(target)

        # Conditional branches fall through
        if insn.cc != ARM_CC_AL:
            next_set.add(fallthrough)

        return next_set

    # Conditional execution (IT blocks)
    if insn.cc != ARM_CC_AL:
        next_set.add(fallthrough)
        return next_set

    # Default: linear execution
    next_set.add(fallthrough)
    return next_set


def _arm32_branch_target(insn) -> int | None:
    for op in insn.operands:
        if op.type == ARM_OP_IMM:
            return op.imm
        if op.type == ARM_OP_REG:
            # bx rX → indirect, unknown
            return None
    return None


# ======================
# ARM64 implementation
# ======================

def _next_pcs_arm64(
    blob: bytes,
    pc: int,
    base_va: int,
) -> Set[int]:

    cs = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    cs.detail = True

    offset = pc - base_va
    insns = list(cs.disasm(blob[offset:], pc, count=1))
    if not insns:
        return set()

    insn = insns[0]
    next_set: Set[int] = set()
    fallthrough = pc + insn.size

    # Unconditional / conditional branches
    if insn.id in (
        ARM64_INS_B,
        ARM64_INS_BL,
        ARM64_INS_CBZ,
        ARM64_INS_CBNZ,
        ARM64_INS_TBZ,
        ARM64_INS_TBNZ,
    ):
        target = _arm64_branch_target(insn)
        if target is not None:
            next_set.add(target)

        # Conditional branches fall through
        if insn.id != ARM64_INS_B:
            next_set.add(fallthrough)

        return next_set

    # Returns
    if insn.id == ARM64_INS_RET:
        # Indirect return
        return set()

    # Indirect branch
    if insn.id in (ARM64_INS_BR, ARM64_INS_BLR):
        return set()

    # Default linear
    next_set.add(fallthrough)
    return next_set


def _arm64_branch_target(insn) -> int | None:
    for op in insn.operands:
        if op.type == ARM64_OP_IMM:
            return op.imm
    return None

