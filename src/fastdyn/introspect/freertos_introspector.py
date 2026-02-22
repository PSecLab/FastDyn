from fastdyn.introspect.introspector_base import RTOSIntrospector
from fastdyn.binary.schema_gen import *
from fastdyn.fastdyn import *
import struct

class FreeRTOSIntrospector(RTOSIntrospector, rtos_name="FreeRTOS"):
    
    def setup_hooks(self):
        """Wires up the FreeRTOS-specific execution hooks."""
        # Register hooks
        self.register_prologue_hook('vTaskSwitchContext')
        self.register_prologue_hook('prvAddNewTaskToReadyList')
        self.register_epilogue_hook('xQueueGenericCreate')

        # TODO: remove hardcoding requirements, i think the registeration function can return which symbols/structs need to be exported.
        # Pass the structs you know you'll need for this RTOS
        generator = SchemaGenerator(self.binary)
        target_structs = [
            "tskTaskControlBlock",
            "xLIST",
            "xLIST_ITEM",
            "xMINI_LIST_ITEM",
            "QueueDefinition"
        ]
        symbols_to_export = {
            "pxCurrentTCB": self.symbols.get('pxCurrentTCB').address,
            "pxReadyTasksLists": self.symbols.get('pxReadyTasksLists').address,
        }

        # Write the scheme for fastdyn
        generator.generate_schema(target_structs, symbols_to_export, output_path=self.out +"/schema.txt")
        print("Creating scheme at:" + self.out +"/schema.txt")
