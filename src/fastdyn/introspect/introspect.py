from fastdyn.binary.symmap import SymbolResolver
from fastdyn.binary.symmap.providers.dwarf import DwarfProvider
from fastdyn import fastdyn_log as fastdyn_log_conf
from fastdyn.introspect.introspector_base import RTOSIntrospector
import os, importlib 

fastdyn_log = fastdyn_log_conf.getFastdynLogger()

def identify_rtos(symbols):
    """
    Pass in a list or set of symbol names from your SymbolInfo dictionary.
    """
    signatures = {
        "FreeRTOS": {"pxCurrentTCB", "vTaskSwitchContext"},
        "Zephyr": {"_kernel", "z_swap"},
        "ThreadX": {"_tx_thread_current_ptr", "tx_thread_create"},
        "RT-Thread": {"rt_current_thread", "rt_thread_create"},
        "MicroC/OS-III": {"OSTCBCurPtr", "OSTaskCreate"},
        "MicroC/OS-II": {"OSTCBCur", "OSTaskCreate"},
        "NuttX": {"g_readytorun", "nx_start"},
        "VxWorks": {"taskSpawn", "windLoadContext"},
        "ChibiOS": {"chSchReadyI"} 
    }
    
    symbol_set = set(symbols.keys())
    
    for rtos, sig_symbols in signatures.items():
        # Using issubset ensures we match even if LTO stripped some other symbols,
        # as long as our core signatures survived.
        if sig_symbols.issubset(symbol_set):
            fastdyn_log.info("Detected RTOS:" + rtos)
            return rtos
    fastdyn_log.info("Likely Unknown/Custom Baremetal")    
    return "Unknown/Custom Baremetal"

def _load_local_introspectors():
    """Scans the current directory for anything ending in _introspector.py"""
    # Get the directory where this script is running
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    for filename in os.listdir(current_dir):
        # Only load files that match our strict naming convention
        if filename.endswith('_introspector.py') and filename != 'base_introspector.py':
            # Strip the '.py' extension to get the module name
            module_name = filename[:-3]
            # Dynamically import it, triggering the auto-registration
            importlib.import_module(f"fastdyn.introspect.{module_name}")


def introspect_rtos(cpu_obj, binary):
    resolver = SymbolResolver([DwarfProvider()])
    syms = resolver.resolve(binary)
    rtos_name = identify_rtos(syms)

    _load_local_introspectors()

    if rtos_name == "Unknown/Custom Baremetal":
        fastdyn_log.info("Cannot introspect custom baremetal firmware.")
        return ""
    # Dynamically instantiate the correct introspector!
    introspector = RTOSIntrospector.create(rtos_name, cpu_obj, syms, binary)
    # Fire up the OS-specific hooks
    schema_contents = introspector.setup_hooks()

    return schema_contents
