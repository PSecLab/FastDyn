from __future__ import annotations

from elftools.elf.elffile import ELFFile


def read_vaddr_bytes(binary_path: str, vaddr: int, size: int) -> bytes:
    with open(binary_path, "rb") as f:
        elf_file = ELFFile(f)
        for segment in elf_file.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue

            start_vaddr = segment["p_vaddr"]
            end_vaddr = start_vaddr + segment["p_memsz"]
            if start_vaddr <= vaddr < end_vaddr:
                file_offset = segment["p_offset"] + (vaddr - start_vaddr)
                f.seek(file_offset)
                return f.read(size)

    return b""


def read_text_section(binary_path: str) -> tuple[bytes, int] | None:
    with open(binary_path, "rb") as f:
        elf_file = ELFFile(f)
        text_section = elf_file.get_section_by_name(".text")
        if text_section is None:
            return None

        return text_section.data(), text_section["sh_addr"]


def hex_addr(value: int | None) -> str | None:
    if value is None:
        return None
    return hex(value)
