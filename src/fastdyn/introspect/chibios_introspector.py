from fastdyn.introspect.introspector_base import RTOSIntrospector
from fastdyn.binary.schema_gen import *
from fastdyn.fastdyn import *
import struct
import logging
import pathlib

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

class ChibiOSIntrospector(RTOSIntrospector, rtos_name="ChibiOS"):

    def setup_hooks(self):
        """Wire up ChibiOS-specific execution hooks and emit schema."""

        # Register the kernel hooks that our QEMU plugin exposes:
        #
        #   - __trace_switch(thread_t *ntp, thread_t *otp)
        #       → handled by inspct_chibios_trace_switch()
        #   - __thd_object_init(os_instance_t *oip, thread_t *tp, const char *name, tprio_t prio)
        #       → handled by inspct_chibios_thd_object_init()
        #
        # The C side (inspct_chibios.c) expects these hook names with the
        # \"_Hook\" suffix, so we register the plain function names here.
        self.register_prologue_hook("__port_switch")
        self.register_prologue_hook("__thd_object_init")

        # Generate the schema FastDyn needs. These struct names must match the
        # DWARF type names produced by the ChibiOS build:
        #
        #   - ch_thread       : kernel thread descriptor (thread_t)
        #   - ch_system       : global system state (ch_system_t)
        #   - ch_os_instance  : per-core OS instance (os_instance_t)
        #   - ch_ready_list   : ready list wrapper (ready_list_t)
        #   - ch_priority_queue : priority queue node/header
        generator = SchemaGenerator(self.binary)
        target_structs = [
            "ch_thread",
            "ch_system",
            "ch_os_instance",
            "ch_ready_list",
            "ch_priority_queue",
        ]

        # Export the symbols the C introspector uses directly.
        symbols_to_export = {}
        for sym_name in ["ch_system", "ch_debug"]:
            sym = self.symbols.get(sym_name)
            if sym is None:
                log.warning("[ChibiOSIntrospector] Missing symbol '%s' in binary", sym_name)
                continue
            symbols_to_export[sym_name] = sym.address

        # Write the schema for fastdyn
        schema_content = generator.generate_schema(target_structs, symbols_to_export)

        return schema_content