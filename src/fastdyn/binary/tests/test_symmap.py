from pathlib import Path
import pytest
from fastdyn.binary.symmap import SymbolResolver
from fastdyn.binary.symmap.providers.dwarf import DwarfProvider
import logging
log = logging.getLogger(__name__)

FIRMWARE = Path(
    "./tests/RTOSDemo.axf"
)

@pytest.mark.skipif(
    not FIRMWARE.exists(),
    reason="STM32 firmware image not available on this system",
)

def test_dwarf_symbols():
    resolver = SymbolResolver([DwarfProvider()])
    syms = resolver.resolve(FIRMWARE)

    log.info(syms)

    assert "main" in syms

