from __future__ import annotations

from elftools.elf.elffile import ELFFile

from fastdyn.binary.binary_utils.artifact_io import write_json_artifact


def run(context) -> None:
    binary_path = context.config.binary_path

    with open(binary_path, "rb") as f:
        elf_file = ELFFile(f)

        binary_info = {
            "path": binary_path,
            "elf_class": elf_file.elfclass,
            "endianness": "little" if elf_file.little_endian else "big",
            "machine": elf_file["e_machine"],
            "entry_point": hex(elf_file["e_entry"]),
            "has_dwarf": elf_file.has_dwarf_info(),
            "section_count": elf_file.num_sections(),
            "segment_count": elf_file.num_segments(),
        }

        sections = []
        for section in elf_file.iter_sections():
            sections.append({
                "name": section.name,
                "type": section["sh_type"],
                "address": hex(section["sh_addr"]),
                "offset": section["sh_offset"],
                "size": section["sh_size"],
                "flags": section["sh_flags"],
            })

        segments = []
        for segment in elf_file.iter_segments():
            segments.append({
                "type": segment["p_type"],
                "virtual_address": hex(segment["p_vaddr"]),
                "physical_address": hex(segment["p_paddr"]),
                "offset": segment["p_offset"],
                "file_size": segment["p_filesz"],
                "memory_size": segment["p_memsz"],
                "flags": segment["p_flags"],
            })

    context.shared["binary_sections"] = sections
    context.shared["binary_segments"] = segments
    write_json_artifact(context.cache_dir, "binary.json", binary_info)
    write_json_artifact(context.cache_dir, "sections.json", sections)
    write_json_artifact(context.cache_dir, "segments.json", segments)
