from fastdyn.introspect.introspector_base import RTOSIntrospector
from fastdyn.fastdyn import *
import struct

class FreeRTOSIntrospector(RTOSIntrospector, rtos_name="FreeRTOS"):
    
    def setup_hooks(self):
        """Wires up the FreeRTOS-specific execution hooks."""
        hook_sym = self.symbols.get('vTaskSwitchContext')
        self.tcb_sym = self.symbols.get('pxCurrentTCB')
        
        if hook_sym and self.tcb_sym:
            # Mask the Thumb bit for QEMU alignment
            actual_addr = hook_sym.address & ~1 
            cb = VirtualInstruction(
                    at=hook_sym.address,
                    instruction="vTaskSwitchContext_Hook",
                    args=""
                )
            self.cpu.add_virtual_instruction(cb)
            print(f"[FreeRTOS] Hooked vTaskSwitchContext at {hex(actual_addr)}")

