from abc import ABC, abstractmethod
import struct

class RTOSIntrospector(ABC):
    # This dictionary acts as our internal Plugin Registry
    _registry = {}

    def __init_subclass__(cls, rtos_name: str, **kwargs):
        super().__init_subclass__(**kwargs)
        # Automatically register the subclass using the provided RTOS name
        cls._registry[rtos_name] = cls

    def __init__(self, cpu_obj, symbols):
        self.cpu = cpu_obj
        self.symbols = symbols

    @abstractmethod
    def setup_hooks(self):
        """Must be implemented by subclasses to wire up the QEMU hooks."""
        pass

    @classmethod
    def create(cls, rtos_name: str, cpu_obj, symbols) -> 'RTOSIntrospector':
        """Factory method to instantiate the correct introspector plugin."""
        introspector_class = cls._registry.get(rtos_name)
        if not introspector_class:
            raise NotImplementedError(f"No introspector plugin registered for: {rtos_name}")
        
        return introspector_class(cpu_obj, symbols)
