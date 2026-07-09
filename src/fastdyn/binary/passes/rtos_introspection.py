from __future__ import annotations

from typing import Any

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.schema_gen import SchemaGenerator
from fastdyn.binary.symmap.core import SymbolResolver
from fastdyn.binary.symmap.providers import DwarfProvider, ElfSymtabProvider


RTOS_SIGNATURES: dict[str, set[str]] = {
    "FreeRTOS": {"pxCurrentTCB", "vTaskSwitchContext"},
    "Zephyr": {"_kernel", "z_thread_mark_switched_in"},
    "ThreadX": {"_tx_thread_current_ptr", "tx_thread_create"},
    "RT-Thread": {"rt_current_thread", "rt_thread_create"},
    "MicroC/OS-III": {"OSTCBCurPtr", "OSTaskCreate"},
    "MicroC/OS-II": {"OSTCBCur", "OSTaskCreate"},
    "NuttX": {"g_readytorun", "nx_start"},
    "VxWorks": {"taskSpawn", "windLoadContext"},
    "ChibiOS": {"chSchReadyI"},
}

CHIBIOS_STRUCTS = [
    "ch_thread",
    "ch_system",
    "ch_os_instance",
    "ch_ready_list",
    "ch_priority_queue",
]

CHIBIOS_SYMBOLS = [
    "ch_system",
    "ch_debug",
    "__port_switch",
    "__thd_object_init",
    "chSchReadyI",
    "chSemWaitS",
    "chSchGoSleepS",
]

ZEPHYR_STRUCTS = [
    "z_kernel",
    "_cpu",
    "k_thread",
    "_thread_base",
    "_timeout",
    "_callee_saved",
    "_thread_stack_info",
    "_thread_arch",
]

ZEPHYR_SYMBOLS = [
    "_kernel",
    "z_thread_mark_switched_in",
    "z_thread_mark_switched_out",
    "z_impl_k_thread_name_set",
    "z_impl_k_sleep",
    "arch_cpu_idle",
]


def _detect_rtos(symbols: dict[str, Any]) -> tuple[str, list[str]]:
    names = set(symbols.keys())
    best_name = "Unknown/Custom Baremetal"
    best_matches: list[str] = []

    for rtos_name, signature in RTOS_SIGNATURES.items():
        matches = sorted(names & signature)
        if signature.issubset(names):
            return rtos_name, matches
        if len(matches) > len(best_matches):
            best_name = rtos_name
            best_matches = matches

    if best_matches:
        return f"Possible {best_name}", best_matches
    return "Unknown/Custom Baremetal", []


def _symbol_entries(symbols: dict[str, Any], names: list[str]) -> tuple[dict[str, str], list[str]]:
    entries: dict[str, str] = {}
    missing: list[str] = []
    for name in names:
        sym = symbols.get(name)
        if sym is None or isinstance(sym.address, list):
            missing.append(name)
            continue
        entries[name] = hex(int(sym.address))
    return entries, missing


def _write_empty_artifacts(context, *, rtos_name: str, matches: list[str], reason: str) -> None:
    identity = {
        "available": False,
        "rtos": rtos_name,
        "matched_symbols": matches,
        "reason": reason,
    }
    write_json_artifact(context.cache_dir, "rtos_identity.json", identity)
    write_json_artifact(context.cache_dir, "rtos_symbols.json", {})
    write_json_artifact(context.cache_dir, "rtos_schema.json", {
        "available": False,
        "schema_path": None,
        "structs": [],
        "symbols": [],
        "reason": reason,
    })
    (context.cache_dir / "rtos_schema.txt").write_text("", encoding="utf-8")


def run(context) -> None:
    resolver = SymbolResolver([
        DwarfProvider(include_variables=True),
        ElfSymtabProvider(),
    ])
    symbols = resolver.resolve(context.config.binary_path)
    rtos_name, matches = _detect_rtos(symbols)

    if rtos_name == "Zephyr":
        symbol_entries, missing_symbols = _symbol_entries(symbols, ZEPHYR_SYMBOLS)
        required_missing = [
            name for name in ("_kernel", "z_thread_mark_switched_in")
            if name in missing_symbols
        ]
        if required_missing:
            _write_empty_artifacts(
                context,
                rtos_name=rtos_name,
                matches=matches,
                reason="missing required Zephyr symbols: " + ", ".join(required_missing),
            )
            write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
            return

        schema_path = context.cache_dir / "rtos_schema.txt"
        try:
            schema_text = SchemaGenerator(context.config.binary_path).generate_schema(
                ZEPHYR_STRUCTS,
                {
                    name: int(addr, 16)
                    for name, addr in symbol_entries.items()
                    if name == "_kernel"
                },
            )
        except Exception as exc:
            _write_empty_artifacts(
                context,
                rtos_name=rtos_name,
                matches=matches,
                reason=f"schema generation failed: {exc}",
            )
            write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
            return

        schema_path.write_text(schema_text, encoding="utf-8")

        identity = {
            "available": True,
            "rtos": "Zephyr",
            "matched_symbols": matches,
            "schema_path": str(schema_path),
        }
        schema_meta = {
            "available": True,
            "schema_path": str(schema_path),
            "structs": ZEPHYR_STRUCTS,
            "symbols": sorted(symbol_entries),
            "missing_optional_symbols": sorted(
                name for name in missing_symbols
                if name not in {"_kernel", "z_thread_mark_switched_in"}
            ),
        }

        write_json_artifact(context.cache_dir, "rtos_identity.json", identity)
        write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
        write_json_artifact(context.cache_dir, "rtos_schema.json", schema_meta)
        return

    if rtos_name != "ChibiOS":
        _write_empty_artifacts(
            context,
            rtos_name=rtos_name,
            matches=matches,
            reason="no supported RTOS introspector matched",
        )
        return

    symbol_entries, missing_symbols = _symbol_entries(symbols, CHIBIOS_SYMBOLS)

    required_missing = [
        name for name in ("ch_system", "__port_switch", "__thd_object_init")
        if name in missing_symbols
    ]
    if required_missing:
        _write_empty_artifacts(
            context,
            rtos_name=rtos_name,
            matches=matches,
            reason="missing required ChibiOS symbols: " + ", ".join(required_missing),
        )
        write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
        return

    schema_path = context.cache_dir / "rtos_schema.txt"
    try:
        schema_text = SchemaGenerator(context.config.binary_path).generate_schema(
            CHIBIOS_STRUCTS,
            {
                name: int(addr, 16)
                for name, addr in symbol_entries.items()
                if name in {"ch_system", "ch_debug"}
            },
        )
    except Exception as exc:
        _write_empty_artifacts(
            context,
            rtos_name=rtos_name,
            matches=matches,
            reason=f"schema generation failed: {exc}",
        )
        write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
        return

    schema_path.write_text(schema_text, encoding="utf-8")

    identity = {
        "available": True,
        "rtos": "ChibiOS",
        "matched_symbols": matches,
        "schema_path": str(schema_path),
    }
    schema_meta = {
        "available": True,
        "schema_path": str(schema_path),
        "structs": CHIBIOS_STRUCTS,
        "symbols": sorted(symbol_entries),
        "missing_optional_symbols": sorted(
            name for name in missing_symbols
            if name not in {"ch_system", "__port_switch", "__thd_object_init"}
        ),
    }

    write_json_artifact(context.cache_dir, "rtos_identity.json", identity)
    write_json_artifact(context.cache_dir, "rtos_symbols.json", symbol_entries)
    write_json_artifact(context.cache_dir, "rtos_schema.json", schema_meta)
