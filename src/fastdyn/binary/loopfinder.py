# loopfinder.py

from dataclasses import dataclass
from typing import List, Optional, Iterable
from capstone import *
from capstone.arm import *


# =========================
# Data model
# =========================

@dataclass(frozen=True)
class Loop:
    loop_head: int
    back_edge_from: int
    mnemonic: str


# =========================
# Architecture abstraction
# =========================

class ArchBackend:
    """Architecture-specific helpers."""

    def is_direct_branch(self, insn) -> bool:
        raise NotImplementedError

    def get_branch_target(self, insn) -> Optional[int]:
        raise NotImplementedError


class ArmBackend(ArchBackend):
    def is_direct_branch(self, insn) -> bool:
        return CS_GRP_JUMP in insn.groups

    def get_branch_target(self, insn) -> Optional[int]:
        for op in insn.operands:
            if op.type == ARM_OP_IMM:
                return op.imm
        return None


# =========================
# Loop detection policy
# =========================

class LoopPolicy:
    """Defines what constitutes a loop."""

    def is_loop_edge(self, src: int, dst: int) -> bool:
        raise NotImplementedError


class BackwardEdgePolicy(LoopPolicy):
    """Classic compiler definition: backward CFG edge."""

    def is_loop_edge(self, src: int, dst: int) -> bool:
        return dst < src


# =========================
# Main analysis
# =========================

class LoopFinder:
    """
    Lightweight static loop detection using Capstone.
    """

    def __init__(
        self,
        arch: str,
        mode: str = "thumb",
        loop_policy: Optional[LoopPolicy] = None,
    ):
        self.arch = arch
        self.mode = mode
        self.loop_policy = loop_policy or BackwardEdgePolicy()

        self._init_disassembler()
        self.backend = self._init_backend()

    # ---------------------
    # Initialization
    # ---------------------

    def _init_disassembler(self):
        if self.arch == "arm":
            if self.mode == "thumb":
                self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
            else:
                self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        else:
            raise NotImplementedError(f"Unsupported arch: {self.arch}")

        self.md.detail = True

    def _init_backend(self) -> ArchBackend:
        if self.arch == "arm":
            return ArmBackend()
        raise NotImplementedError(f"No backend for arch {self.arch}")

    # ---------------------
    # Public API
    # ---------------------

    def find_loops(
        self,
        blob: bytes,
        base_va: int,
        max_insns: int = 100_000,
    ) -> List[Loop]:
        """
        Scan the binary and return all detected loops.
        """

        loops: List[Loop] = []
        seen_pcs = set()

        for insn in self.md.disasm(blob, base_va):
            if len(seen_pcs) >= max_insns:
                break
            seen_pcs.add(insn.address)

            if not self.backend.is_direct_branch(insn):
                continue

            target = self.backend.get_branch_target(insn)
            if target is None:
                continue  # indirect branch

            if self.loop_policy.is_loop_edge(insn.address, target):
                loops.append(
                    Loop(
                        loop_head=target,
                        back_edge_from=insn.address,
                        mnemonic=insn.mnemonic,
                    )
                )

        return loops

