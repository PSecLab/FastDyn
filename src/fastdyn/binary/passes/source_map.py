from __future__ import annotations

from elftools.dwarf.descriptions import describe_form_class
from elftools.elf.elffile import ELFFile

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact
from fastdyn.binary.binary_utils.source_resolution import resolve_source_path


def _decode_attr(attr):
    if attr is None:
        return None
    value = attr.value
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value) if value is not None else None


def _walk_die(die, line_program, comp_dir, source_roots, entries):
    if die.tag == "DW_TAG_subprogram":
        name_attr = die.attributes.get("DW_AT_name")
        low_pc = die.attributes.get("DW_AT_low_pc")
        high_pc = die.attributes.get("DW_AT_high_pc")
        decl_file = die.attributes.get("DW_AT_decl_file")
        decl_line = die.attributes.get("DW_AT_decl_line")

        if name_attr and low_pc and decl_file and decl_line and line_program:
            file_idx = decl_file.value
            if 0 < file_idx <= len(line_program["file_entry"]):
                file_entry = line_program["file_entry"][file_idx - 1]
                file_name = file_entry.name.decode("utf-8", errors="replace")

                dir_prefix = ""
                dir_index = file_entry.dir_index
                if dir_index > 0 and dir_index <= len(line_program["include_directory"]):
                    dir_prefix = line_program["include_directory"][dir_index - 1].decode("utf-8", errors="replace") + "/"

                name = name_attr.value.decode("utf-8", errors="replace")
                address = int(low_pc.value)
                size = None
                if high_pc is not None:
                    high_pc_val = int(high_pc.value)
                    high_pc_class = describe_form_class(high_pc.form)
                    size = high_pc_val - address if high_pc_class == "address" else high_pc_val

                source_file = dir_prefix + file_name
                resolution = resolve_source_path(source_file, comp_dir, source_roots)
                entries.append({
                    "function": name,
                    "address": hex(address),
                    "size": size,
                    "file": source_file,
                    "line": decl_line.value,
                    "provider": "dwarf",
                    **resolution,
                })

    for child in die.iter_children():
        _walk_die(child, line_program, comp_dir, source_roots, entries)


def run(context) -> None:
    entries = []

    with open(context.config.binary_path, "rb") as f:
        elf_file = ELFFile(f)
        if not elf_file.has_dwarf_info():
            context.shared["source_map"] = entries
            write_json_artifact(context.cache_dir, "source_map.json", entries)
            return

        dwarf_info = elf_file.get_dwarf_info()
        for cu in dwarf_info.iter_CUs():
            top_die = cu.get_top_DIE()
            if top_die is None:
                continue

            comp_dir = _decode_attr(top_die.attributes.get("DW_AT_comp_dir"))
            line_program = dwarf_info.line_program_for_CU(cu)
            _walk_die(top_die, line_program, comp_dir, context.config.source_roots, entries)

    entries.sort(key=lambda item: (int(item["address"], 16), item["function"]))
    context.shared["source_map"] = entries
    write_json_artifact(context.cache_dir, "source_map.json", entries)
