from __future__ import annotations

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact


def run(context) -> None:
    regions = []

    lowest_load_vaddr = None
    for segment in context.shared.get("binary_segments", []):
        if segment["type"] != "PT_LOAD":
            continue

        vaddr = int(segment["virtual_address"], 16)
        if lowest_load_vaddr is None or vaddr < lowest_load_vaddr:
            lowest_load_vaddr = vaddr

        regions.append({
            "name": f"elf_load_{segment['virtual_address']}",
            "base_address": segment["virtual_address"],
            "size": segment["memory_size"],
            "region_type": "elf_load_segment",
            "source": "elf",
            "flags": segment["flags"],
        })

    if context.config.flash_base is not None:
        regions.append({
            "name": "flash",
            "base_address": hex(context.config.flash_base),
            "size": context.config.flash_size if context.config.flash_size is not None else 0,
            "region_type": "flash",
            "source": "config",
        })
    elif lowest_load_vaddr is not None:
        regions.append({
            "name": "flash_inferred",
            "base_address": hex(lowest_load_vaddr),
            "size": 0,
            "region_type": "flash",
            "source": "elf",
            "note": "Inferred from lowest PT_LOAD segment because config lacked flash section",
        })

    regions.append({
        "name": "sram",
        "base_address": hex(context.config.sram_base),
        "size": 0,
        "region_type": "sram",
        "source": "config",
    })

    if context.config.init_nsvtor is not None:
        regions.append({
            "name": "vector_table",
            "base_address": hex(context.config.init_nsvtor),
            "size": 0,
            "region_type": "vector_table_base",
            "source": "config",
            "note": "init_nsvtor from config; not necessarily flash start",
        })

    write_json_artifact(context.cache_dir, "memory_map.json", regions)
