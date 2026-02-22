from abc import ABC, abstractmethod
from fastdyn.fastdyn import *
from fastdyn.machine import *
import struct

class RTOSIntrospector(ABC):
    # This dictionary acts as our internal Plugin Registry
    _registry = {}

    def __init_subclass__(cls, rtos_name: str, **kwargs):
        super().__init_subclass__(**kwargs)
        # Automatically register the subclass using the provided RTOS name
        cls._registry[rtos_name] = cls

    def __init__(self, cpu_obj, symbols, binary):
        self.cpu = cpu_obj
        self.symbols = symbols
        self.binary = binary

    @abstractmethod
    def setup_hooks(self):
        """Must be implemented by subclasses to wire up the QEMU hooks."""
        pass

    def register_prologue_hook(self, sym_name):
        hook_sym = self.symbols.get(sym_name) 
        if hook_sym is None:
            print(f"[hook] Symbol '{sym_name}' not found")
            return False
        hook_sym_addr = hook_sym.address
        cb = VirtualInstruction(
                    at=hook_sym.address,
                    instruction=f"{sym_name}_Hook",
                    args=[]
                )
        self.cpu.add_virtual_instruction(cb)
        print(f"[hook] Successfully registered prologue hook for '{sym_name}' at 0x{hook_sym_addr:x}")
        return True

    def register_epilogue_hook(self, sym_name):
        hook_sym = self.symbols.get(sym_name + "_epi")
        if hook_sym is None:
            print(f"[hook] Symbol '{sym_name}' not found")
            return False

        # Many hooks for eiplogues
        for hook_sym_addr in hook_sym.address:
            hook_sym_addr = hook_sym.address
            cb = VirtualInstruction(
                        at=hook_sym.address,
                        instruction=f"{sym_name}_Hook",
                        args=[]
                    )
            self.cpu.add_virtual_instruction(cb)
        print(f"[hook] Successfully registered prologue hook for '{sym_name}' at 0x{hook_sym_addr:x}")
        return True

    @classmethod
    def create(cls, rtos_name: str, cpu_obj, symbols,binary) -> 'RTOSIntrospector':
        """Factory method to instantiate the correct introspector plugin."""
        introspector_class = cls._registry.get(rtos_name)
        if not introspector_class:
            raise NotImplementedError(f"No introspector plugin registered for: {rtos_name}")
        
        return introspector_class(cpu_obj, symbols, binary)
