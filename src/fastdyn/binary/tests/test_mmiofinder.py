from pathlib import Path

import pytest

from fastdyn.binary.mmiofinder import find_mmio_accesses
from fastdyn.binary.nextpc import next_pcs


# ---------------------------------------------------------------------------
# Test data
# ---------------------------------------------------------------------------

FIRMWARE = Path(
    "./tests/RTOSDemo.axf"
)


@pytest.mark.skipif(
    not FIRMWARE.exists(),
    reason="STM32 firmware image not available on this system",
)
def test_find_mmio_accesses_and_nextpc():
    blob = FIRMWARE.read_bytes()

    hits = find_mmio_accesses(
        blob=blob,
        base_va=0x08000000,
        arch="arm32",
        mode="thumb",
        mmio_regions=[(0x40000000, 0x5FFFFFFF)],
    )

    assert hits, "Expected at least one MMIO access"

    for h in hits:
        assert h.pc is not None
        assert h.address is not None

        pcs = next_pcs(
            blob=blob,
            pc=h.pc,
            base_va=0x08000000,
            arch="arm32",
            mode="thumb",
        )

        # next_pcs returns a set
        assert isinstance(pcs, set)
        assert len(pcs) >= 1

