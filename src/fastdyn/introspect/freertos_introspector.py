from fastdyn.introspect.introspector_base import RTOSIntrospector
from fastdyn.binary.schema_gen import *
from fastdyn.fastdyn import *
import struct

class FreeRTOSIntrospector(RTOSIntrospector, rtos_name="FreeRTOS"):
    
    def setup_hooks(self):
        """Wires up the FreeRTOS-specific execution hooks."""
        hook_sym = self.symbols.get('vTaskSwitchContext')
        self.tcb_sym = self.symbols.get('pxCurrentTCB')
        # Pass the structs you know you'll need for this RTOS
        generator = SchemaGenerator(self.binary)
        target_structs = ["tskTaskControlBlock"]
        target_structs = [
    "tskTaskControlBlock",
    "xLIST",
    "xLIST_ITEM",
    "xMINI_LIST_ITEM"
]
        generator.generate_schema(target_structs, output_path=self.out +"/schema.txt")
        print("Creating scheme at:" + self.out +"/schema.txt")

        
        if hook_sym and self.tcb_sym:
            # Mask the Thumb bit for QEMU alignment
            actual_addr = hook_sym.address & ~1 
            args = f"{self.symbols.get('pxCurrentTCB').address:#x}"
            cb = VirtualInstruction(
                    at=hook_sym.address,
                    instruction="vTaskSwitchContext_Hook",
                    args=[args]
                )
            self.cpu.add_virtual_instruction(cb)

            cb = VirtualInstruction(
                            at = self.symbols.get('prvAddNewTaskToReadyList').address,
                            instruction="prvAddNewTaskToReadyList_Hook",
                            args=[])
            self.cpu.add_virtual_instruction(cb)

            print(f"[FreeRTOS] Hooked vTaskSwitchContext at {hex(actual_addr)}")
            print(f"[FreeRTOS] Hooked prvAddNewTaskToReadyList at {hex(self.symbols.get('prvAddNewTaskToReadyList').address)}")

