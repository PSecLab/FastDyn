from fastdyn.binary.symmap import SymbolResolver
from fastdyn.binary.symmap.providers.dwarf import DwarfProvider
from fastdyn import fastdyn_log as fastdyn_log_conf

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
        "ChibiOS": {"ch", "chSchReadyI"} 
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

def introspect_rtos(cpu_obj, binary):
        resolver = SymbolResolver([DwarfProvider()])
        syms = resolver.resolve(binary)
        identify_rtos(syms)
