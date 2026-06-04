from __future__ import annotations

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


def run(context) -> None:
    compile_units = []

    with open(context.config.binary_path, "rb") as f:
        elf_file = ELFFile(f)
        if not elf_file.has_dwarf_info():
            write_json_artifact(context.cache_dir, "compile_units.json", compile_units)
            return

        dwarf_info = elf_file.get_dwarf_info()
        for cu in dwarf_info.iter_CUs():
            top_die = cu.get_top_DIE()
            if top_die is None:
                continue

            source_files = []
            try:
                line_program = dwarf_info.line_program_for_CU(cu)
                if line_program and hasattr(line_program, "header"):
                    for file_entry in line_program.header.get("file_entry", []):
                        name = getattr(file_entry, "name", None)
                        if isinstance(name, bytes):
                            name = name.decode("utf-8", errors="replace")
                        if name:
                            source_files.append(name)
            except Exception:
                source_files = []

            name = _decode_attr(top_die.attributes.get("DW_AT_name"))
            comp_dir = _decode_attr(top_die.attributes.get("DW_AT_comp_dir"))
            resolution = resolve_source_path(name or "", comp_dir, context.config.source_roots)

            compile_units.append({
                "name": name,
                "comp_dir": comp_dir,
                "language": getattr(top_die.attributes.get("DW_AT_language"), "value", None),
                "producer": _decode_attr(top_die.attributes.get("DW_AT_producer")),
                "source_files_count": len(source_files),
                "source_files": source_files[:50],
                **resolution,
            })

    context.shared["compile_units"] = compile_units
    write_json_artifact(context.cache_dir, "compile_units.json", compile_units)
