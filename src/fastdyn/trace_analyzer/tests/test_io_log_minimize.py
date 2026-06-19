import pytest
import tempfile
from pathlib import Path

from fastdyn.trace_analyzer.pipeline.io_log_minimize import build_io_trace_context
from fastdyn.trace_analyzer.models import RunArtifacts, StaticArtifacts
from fastdyn.verifier.context_minimizer import MMIOAnalyzer

class DummyRegister:
    def __init__(self, name, addr):
        self.name = name
        self.address_offset = addr
        self.description = "Dummy register"
        self.fields = []

class DummyPeripheral:
    def __init__(self, name, addr, regs):
        self.name = name
        self.base_address = addr
        self.registers = regs

class DummySVD:
    def __init__(self):
        self.peripherals = [
            DummyPeripheral("RCC", 0x40023800, [DummyRegister("CR", 0x0)])
        ]

def test_io_log_threshold_behavior():
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        log_path = tdp / "io.log"
        
        # Generate a fake io log with 300 target accesses and 50 other accesses
        lines = []
        # Target accesses (RCC CR = 0x40023800) -> 30 loops of length 10
        for i in range(300):
            lines.append(f"[{10.0 + i*0.1:.6f}] [machine0] Read: address=0x40023800, size=4 bytes, value=0x0, pc=0x8001000\n")
            
        # Target write access
        lines.append(f"[{50.0:.6f}] [machine0] Write: address=0x40023800, size=4 bytes, value=0x1, pc=0x8001000\n")
        
        # Non-target accesses
        for i in range(50):
            lines.append(f"[{60.0 + i*0.1:.6f}] [machine0] Read: address=0x40000000, size=4 bytes, value=0x0, pc=0x8001000\n")
            
        with open(log_path, "w") as f:
            f.writelines(lines)
            
        run_artifacts = RunArtifacts(
            run_id="test_run",
            run_dir=tdp,
            probe_result_path=tdp / "probe_result.json",
            probe_result={"extra_info": "0x40023800"},
            io_log_path=log_path
        )
        
        static_artifacts = StaticArtifacts(
            cache_dir=tdp,
            symbols_path=tdp / "symbols.json",
            functions_path=tdp / "functions.json",
            source_map_path=tdp / "source_map.json",
            compile_units_path=tdp / "compile_units.json",
            memory_map_path=tdp / "memory_map.json",
            svd_map_path=tdp / "svd_map.json",
            svd_map={
                "peripherals": [
                    {
                        "name": "RCC",
                        "registers": [{"name": "CR", "address": "0x40023800"}]
                    }
                ]
            },
            functions=[],
            symbols=[]
        )
        
        dummy_svd = DummySVD()
        
        ctx = build_io_trace_context(run_artifacts, static_artifacts, dummy_svd)
        
        assert ctx.target_peripheral == "RCC"
        assert ctx.target_register == "CR"
        
        # Should have found a loop pattern due to 300 identical reads
        assert len(ctx.loop_patterns) > 0
        # The write should appear in state behavior (RMW or write)
        assert any("Write" in s for s in ctx.state_behavior)
