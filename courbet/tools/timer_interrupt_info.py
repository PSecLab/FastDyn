"""
Tool to verify that the system timer interrupt vector is correctly identified.

Limitations:
- Assumes ChibiOS is used as the RTOS.
- Looks for specific function calls and names; may need adjustment for other RTOSes.
- Assumes standard Cortex-M vector table layout.
"""

import gdb
from tools.utils import *

VTOR_ADDR = 0xE000ED08
FIRST_IRQ_VECTOR = 16
MAX_VECTORS = 256


def _read_u32(addr):
    return int(gdb.parse_and_eval(f"*(uint32_t*){addr:#x}"))


def _get_vtor():
    try:
        vtor = _read_u32(VTOR_ADDR)
        if vtor != 0:
            return vtor
    except gdb.error:
        gdb.write(f"[{BLUE_TEXT}timer{END_COLOR}] [{YELLOW_TEXT}Warning{END_COLOR}] Unable to read VTOR register\n")
        pass
    # fallback: vector table at 0x8004000
    return 0x8004000


def _resolve_function(addr):
    try:
        block = gdb.block_for_pc(addr)
        if block and block.function:
            return block.function.print_name
    except gdb.error:
        pass
    return None


def _disasm_contains(addr, needle):
    try:
        asm = gdb.execute(f"disassemble {addr}", to_string=True)
        return needle in asm
    except gdb.error:
        return False


def find_timer_irq(verbose=True):
    """
    Locate the system timer interrupt vector.

    Returns a dict:
    {
        'vector_index': int,
        'irqn': int,
        'handler': str,
        'address': int,
        'evidence': str
    }
    """

    vtor = _get_vtor()
    if verbose:
        gdb.write(f"[{BLUE_TEXT}timer{END_COLOR}] VTOR = {hex(vtor)}\n")

    results = []

    for vec in range(FIRST_IRQ_VECTOR, MAX_VECTORS):
        addr = _read_u32(vtor + vec * 4)
        if addr == 0:
            continue

        fn = _resolve_function(addr)

        # Strong signal: calls ChibiOS system timer
        if _disasm_contains(addr, "st_lld_serve_interrupt"):
            # get address of st_lld_serve_interrupt function
            function_addr = None
            try:
                function_addr = int(gdb.parse_and_eval("&st_lld_serve_interrupt"))
            except gdb.error:
                pass
            if function_addr:
                if _disasm_contains(function_addr, "chSysTimerHandlerI"):
                    results.append({
                        "vector_index": vec,
                        "irqn": vec - FIRST_IRQ_VECTOR,
                        "handler": fn,
                        "address": addr,
                        "evidence": "calls chSysTimerHandlerI"
                    })
                    continue

            # Weak signal: known ChibiOS timer dispatcher
            results.append({
                "vector_index": vec,
                "irqn": vec - FIRST_IRQ_VECTOR,
                "handler": fn,
                "address": addr,
                "evidence": "handler name match"
            })


    if not results:
        gdb.write(f"[{BLUE_TEXT}timer{END_COLOR}] [{RED_TEXT}ERROR{END_COLOR}] Unable to locate system timer interrupt vector\n")
        return None

    # Prefer strong evidence
    results.sort(key=lambda r: r["evidence"] != "calls chSysTimerHandlerI")

    best = results[0]

    if verbose:
        gdb.write(
            f"[{BLUE_TEXT}timer{END_COLOR}] FOUND:\n"
            f"  vector index : {best['vector_index']}\n"
            f"  IRQn         : {best['irqn']}\n"
            f"  handler      : {best['handler']}\n"
            f"  address      : {hex(best['address'])}\n"
            f"  evidence     : {best['evidence']}\n\n"
            f"Check that you see:\n"
            f"{BLUE_TEXT}   <symbol/address> raise_periodic_irq {best['vector_index']}{END_COLOR}\n"
            f"in your {LIGHT_BLUE_TEXT}virtuals.txt{END_COLOR} file.\n\n"
        )

    return best

find_timer_irq()