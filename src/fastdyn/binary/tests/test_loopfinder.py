import logging
from pathlib import Path

import pytest
from fastdyn.binary.loopfinder import LoopFinder


logger = logging.getLogger(__name__)

TEST_AXF = Path(
    "./tests/RTOSDemo.axf"
)



@pytest.fixture(scope="module")
def firmware_blob():
    assert TEST_AXF.exists(), f"Missing test file: {TEST_AXF}"

    blob = TEST_AXF.read_bytes()

    return {
        "blob": blob,
        "base_va": 0x08000000,
        "arch": "arm",
        "mode": "thumb",
    }


def test_loopfinder_on_axf(firmware_blob):
    finder = LoopFinder(
        arch=firmware_blob["arch"],
        mode=firmware_blob["mode"],
    )

    loops = finder.find_loops(
        blob=firmware_blob["blob"],
        base_va=firmware_blob["base_va"],
    )

    logger.info("Detected %d loops in RTOSDemo.axf", len(loops))

    # Sanity
    assert isinstance(loops, list)
    assert len(loops) > 0, "Expected at least one loop in RTOSDemo.axf"

    # Structural invariants
    for l in loops:
        logger.info(
            "Loop: %s -> %s (%s)",
            hex(l.back_edge_from),
            hex(l.loop_head),
            l.mnemonic,
        )

        assert l.loop_head < l.back_edge_from
        assert isinstance(l.mnemonic, str)

