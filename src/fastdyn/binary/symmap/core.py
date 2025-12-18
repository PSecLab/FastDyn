from __future__ import annotations

import abc
from dataclasses import dataclass
from typing import Dict, Iterable, Optional


# =============================================================================
# Public data model
# =============================================================================

@dataclass(frozen=True)
class SymbolInfo:
    """
    Canonical representation of a recovered symbol.
    """
    name: str
    address: int
    size: Optional[int]
    kind: str                 # function | variable | label | unknown
    provider: str             # provider name
    confidence: float         # 0.0 – 1.0


# =============================================================================
# Abstract provider interface
# =============================================================================

class SymbolProvider(abc.ABC):
    """
    Abstract interface for symbol providers.

    A SymbolProvider extracts or infers symbol → address mappings from a binary.
    Providers may rely on static metadata, static analysis, dynamic traces, or ML.
    """

    @property
    @abc.abstractmethod
    def name(self) -> str:
        """
        Return a stable, human-readable identifier for this provider.

        Used for provenance tracking, debugging, and conflict resolution.
        """
        raise NotImplementedError

    @abc.abstractmethod
    def is_applicable(self, binary_path: str) -> bool:
        """
        Return True if this provider can reasonably analyze the given binary.

        This method must be cheap and conservative; returning False is preferred
        over raising or failing later.
        """
        raise NotImplementedError

    @abc.abstractmethod
    def get_symbols(self, binary_path: str) -> Iterable[SymbolInfo]:
        """
        Extract or infer symbols from the binary.

        Implementations must return SymbolInfo objects with concrete addresses
        and confidence scores. Conflict resolution is handled by the resolver.
        """
        raise NotImplementedError


# =============================================================================
# Resolver
# =============================================================================

class SymbolResolver:
    """
    Combine symbols from multiple providers and resolve conflicts.
    """

    def __init__(self, providers: Iterable[SymbolProvider]) -> None:
        self._providers = list(providers)

    def resolve(
        self,
        binary_path: str,
        *,
        min_confidence: float = 0.0,
    ) -> Dict[str, SymbolInfo]:
        """
        Resolve a name → SymbolInfo map from all applicable providers.
        """

        best: Dict[str, SymbolInfo] = {}

        for provider in self._providers:
            if not provider.is_applicable(binary_path):
                continue

            for sym in provider.get_symbols(binary_path):
                if sym.confidence < min_confidence:
                    continue

                prev = best.get(sym.name)
                if (
                    prev is None
                    or sym.confidence > prev.confidence
                    or (
                        sym.confidence == prev.confidence
                        and sym.address < prev.address
                    )
                ):
                    best[sym.name] = sym

        return best

    def name_to_addr(self, binary_path: str) -> Dict[str, int]:
        """
        Convenience helper returning only name → address mappings.
        """
        return {k: v.address for k, v in self.resolve(binary_path).items()}

