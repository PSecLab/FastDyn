"""
mmiofinder.py

Lightweight Capstone-based detector for hardcoded MMIO accesses.

Public API:
    find_mmio_accesses(...)
"""

from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional

from capstone import *
from capstone.arm import *
from capstone.arm64 import *


# ======================
# Public data structure
# ======================

@dataclass(frozen=True)
class MMIOAccess:
    pc: int
    size: int
    arch: str
    instruction: str
    base_register: str
    address: int
    note: str


# ======================
# Public API
# ======================

def find_mmio_accesses(
    *,
    blob: bytes,
    base_va: int,
    arch: str,
    mmio_regions: List[Tuple[int, int]],
    mode: str = "thumb",
) -> List[MMIOAccess]:
    """
    Find hardcoded MMIO accesses in a binary blob.

    Parameters
    ----------
    blob : bytes
        Raw binary (.text or firmware image).
    base_va : int
        Virtual address corresponding to blob[0].
    arch : str
        "arm32" or "arm64"
    mmio_regions : list[(start, end)]
        Inclusive MMIO address ranges.
    mode : str
        For arm32 only: "arm" or "thumb"

    Returns
    -------
    List[MMIOAccess]
    """

    if arch == "arm32":
        return _scan_arm32(blob, base_va, mmio_regions, mode)
    elif arch == "arm64":
        return _scan_arm64(blob, base_va, mmio_regions)
    else:
        raise ValueError(f"Unsupported arch: {arch}")


# ======================
# Helpers
# ======================

def _in_regions(addr: int, regions) -> bool:
    return any(start <= addr <= end for start, end in regions)


def _u32(x: int) -> int:
    return x & 0xFFFFFFFF


# ======================
# ARM32 implementation
# ======================

def _scan_arm32(
    blob: bytes,
    base_va: int,
    regions: List[Tuple[int, int]],
    mode: str,
) -> List[MMIOAccess]:

    cs = Cs(
        CS_ARCH_ARM,
        CS_MODE_ARM if mode == "arm" else CS_MODE_THUMB
    )
    cs.detail = True

    reg_const: Dict[int, int] = {}
    results: List[MMIOAccess] = []

    for insn in cs.disasm(blob, base_va):
        # Detect memory access
        hit = _arm32_mem_access(insn, reg_const)
        if hit:
            addr, base_reg, note = hit
            if _in_regions(addr, regions):
                results.append(
                    MMIOAccess(
                        pc=insn.address,
                        size=insn.size,
                        arch=f"ARM32-{mode.upper()}",
                        instruction=f"{insn.mnemonic} {insn.op_str}",
                        base_register=cs.reg_name(base_reg),
                        address=addr,
                        note=note,
                    )
                )

        # Track constants
        _arm32_track_constants(insn, reg_const)

    return results


def _arm32_track_constants(insn: CsInsn, reg_const: Dict[int, int]) -> None:
    m = insn.mnemonic.lower()
    ops = insn.operands

    def kill(r):
        reg_const.pop(r, None)

    # mov rX, #imm
    if m == "mov" and len(ops) == 2:
        if ops[1].type == ARM_OP_IMM:
            reg_const[ops[0].reg] = _u32(ops[1].imm)
        else:
            kill(ops[0].reg)
        return

    # movw / movt
    if m in ("movw", "movt") and len(ops) == 2:
        dst = ops[0].reg
        imm16 = ops[1].imm & 0xFFFF
        old = reg_const.get(dst, 0)
        if m == "movw":
            reg_const[dst] = (old & 0xFFFF0000) | imm16
        else:
            reg_const[dst] = (old & 0x0000FFFF) | (imm16 << 16)
        return

    # add/sub rX, rY, #imm
    if m in ("add", "sub") and len(ops) >= 3:
        if ops[1].type == ARM_OP_REG and ops[2].type == ARM_OP_IMM:
            src = ops[1].reg
            if src in reg_const:
                base = reg_const[src]
                reg_const[ops[0].reg] = (
                    _u32(base + ops[2].imm)
                    if m == "add"
                    else _u32(base - ops[2].imm)
                )
                return
        kill(ops[0].reg)

    # Conservative kill
    for r in insn.regs_write:
        kill(r)


def _arm32_mem_access(
    insn: CsInsn,
    reg_const: Dict[int, int],
) -> Optional[Tuple[int, int, str]]:

    if not insn.mnemonic.lower().startswith(("ldr", "str")):
        return None

    for op in insn.operands:
        if op.type == ARM_OP_MEM:
            base = op.mem.base
            disp = op.mem.disp
            if base in reg_const and abs(disp) <= 0x1000:
                addr = _u32(reg_const[base] + disp)
                return addr, base, f"disp={disp:+d}"

    return None


# ======================
# AArch64 implementation
# ======================

def _scan_arm64(
    blob: bytes,
    base_va: int,
    regions: List[Tuple[int, int]],
) -> List[MMIOAccess]:

    cs = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    cs.detail = True

    reg_const: Dict[int, int] = {}
    results: List[MMIOAccess] = []

    for insn in cs.disasm(blob, base_va):
        hit = _arm64_mem_access(insn, reg_const)
        if hit:
            addr, base_reg = hit
            if _in_regions(addr, regions):
                results.append(
                    MMIOAccess(
                        pc=insn.address,
                        size=insn.size,
                        arch="AArch64",
                        instruction=f"{insn.mnemonic} {insn.op_str}",
                        base_register=cs.reg_name(base_reg),
                        address=addr,
                        note="const base",
                    )
                )

        _arm64_track_constants(insn, reg_const)

    return results


def _arm64_track_constants(insn: CsInsn, reg_const: Dict[int, int]) -> None:
    m = insn.mnemonic.lower()
    ops = insn.operands

    def kill(r):
        reg_const.pop(r, None)

    # movz / movk
    if m in ("movz", "movk") and len(ops) == 3:
        dst = ops[0].reg
        imm = ops[1].imm
        shift = ops[2].imm
        old = reg_const.get(dst, 0)
        mask = 0xFFFF << shift
        if m == "movz":
            reg_const[dst] = imm << shift
        else:
            reg_const[dst] = (old & ~mask) | (imm << shift)
        return

    # add/sub Xd, Xn, #imm
    if m in ("add", "sub") and len(ops) >= 3:
        if ops[1].type == ARM64_OP_REG and ops[2].type == ARM64_OP_IMM:
            src = ops[1].reg
            if src in reg_const:
                base = reg_const[src]
                reg_const[ops[0].reg] = (
                    base + ops[2].imm if m == "add" else base - ops[2].imm
                )
                return
        kill(ops[0].reg)

    for r in insn.regs_write:
        kill(r)


def _arm64_mem_access(
    insn: CsInsn,
    reg_const: Dict[int, int],
) -> Optional[Tuple[int, int]]:

    if not insn.mnemonic.lower().startswith(("ldr", "str")):
        return None

    for op in insn.operands:
        if op.type == ARM64_OP_MEM:
            base = op.mem.base
            disp = op.mem.disp
            if base in reg_const and abs(disp) <= 0x1000:
                return reg_const[base] + disp, base

    return None

