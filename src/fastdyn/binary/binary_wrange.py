#!/usr/bin/env python3
import os
import sys
from dataclasses import dataclass

from elftools.elf.constants import P_FLAGS
from elftools.elf.elffile import ELFFile

try:
    from .. import fastdyn_log as fastdyn_log_conf
except ImportError:
    from fastdyn import fastdyn_log as fastdyn_log_conf

fastdyn_log = fastdyn_log_conf.getFastdynLogger()

# -------------------------------------------------------------------------------
# This script takes a binary, and outputs a list of its writable ranges of memory.
# -------------------------------------------------------------------------------


@dataclass(frozen=True)
class SectionMapping:
    name: str
    address: int
    size: int
    section_type: str


@dataclass(frozen=True)
class WritableSegment:
    index: int
    segment_type: str
    address: int
    file_size: int
    memory_size: int
    flags: int
    sections: list[SectionMapping]


def _format_flags(flags):
    flag_order = (
        (P_FLAGS.PF_R, "R"),
        (P_FLAGS.PF_W, "W"),
        (P_FLAGS.PF_X, "X"),
    )
    value = "".join(name for mask, name in flag_order if flags & mask)
    return value or "-"


def _format_range(start, size):
    return f"0x{start:08X}-0x{start + size:08X}"


def _sections_for_segment(elf, segment):
    sections = []

    for section in elf.iter_sections():
        if not segment.section_in_segment(section):
            continue

        name = section.name or "<unnamed>"
        sections.append(
            SectionMapping(
                name=name,
                address=section["sh_addr"],
                size=section["sh_size"],
                section_type=section["sh_type"],
            )
        )

    return sections


def collect_writable_segments(elf_path):
    writable = []

    with open(elf_path, "rb") as f:
        elf = ELFFile(f)

        for index, segment in enumerate(elf.iter_segments()):
            if not segment["p_flags"] & P_FLAGS.PF_W:
                continue
            if segment["p_memsz"] == 0:
                continue

            writable.append(
                WritableSegment(
                    index=index,
                    segment_type=segment["p_type"],
                    address=segment["p_vaddr"],
                    file_size=segment["p_filesz"],
                    memory_size=segment["p_memsz"],
                    flags=segment["p_flags"],
                    sections=_sections_for_segment(elf, segment),
                )
            )

    return writable


def print_writable_segments(segments):
    if not segments:
        fastdyn_log.info("[binary_wrange.py] No writable segments found.")
        return

    fastdyn_log.info("[binary_wrange.py] Writable segments:")
    for selection_id, segment in enumerate(segments):
        fastdyn_log.info(
            f"  [{selection_id}] segment {segment.index}: "
            f"{segment.segment_type} {_format_flags(segment.flags)} "
            f"{_format_range(segment.address, segment.memory_size)} "
            f"memsz=0x{segment.memory_size:x} filesz=0x{segment.file_size:x}"
        )

        if segment.sections:
            fastdyn_log.info("      sections:")
            for section in segment.sections:
                fastdyn_log.info(
                    f"        - {section.name} "
                    f"{_format_range(section.address, section.size)} "
                    f"size=0x{section.size:x} type={section.section_type}"
                )
        else:
            fastdyn_log.info("      sections: <none>")


def _parse_selection(selection, count):
    selection = selection.strip().lower()
    if selection in ("", "a", "all", "*"):
        return list(range(count))
    if selection in ("n", "none"):
        return []

    selected = set()
    tokens = selection.replace(",", " ").split()
    for token in tokens:
        if "-" in token:
            start_text, end_text = token.split("-", 1)
            start = int(start_text, 10)
            end = int(end_text, 10)
            if start > end:
                start, end = end, start
            indexes = range(start, end + 1)
        else:
            indexes = (int(token, 10),)

        for index in indexes:
            if index < 0 or index >= count:
                raise ValueError(
                    f"selection {index} is outside the valid range 0-{count - 1}"
                )
            selected.add(index)

    return sorted(selected)


def prompt_for_segments(segments):
    print_writable_segments(segments)

    if not segments:
        return []

    prompt = (
        "Select writable segments to include in the snapshot "
        "(e.g. 0,2-3; blank/all for all; none for none): "
    )

    while True:
        try:
            selection = input(prompt)
        except EOFError:
            fastdyn_log.info(
                "[binary_wrange.py] No input provided; including all segments."
            )
            return list(range(len(segments)))

        try:
            return _parse_selection(selection, len(segments))
        except ValueError as e:
            fastdyn_log.info(f"[binary_wrange.py] Invalid selection: {e}")


def write_selected_segments(out_file, segments, selected_indexes):
    with open(out_file, "w") as f:
        for index in selected_indexes:
            segment = segments[index]
            f.write(f"0x{segment.address:08X}\t0x{segment.memory_size:x}\n")


# Write a tab delimited list of address + size values for selected writable ranges.
def run(out_file, bin_path):
    if os.path.exists(out_file):
        fastdyn_log.info(
            f"[binary_wrange.py] Skipping existing output file: {out_file}"
        )
        return

    try:
        segments = collect_writable_segments(bin_path)
        selected_indexes = prompt_for_segments(segments)
        write_selected_segments(out_file, segments, selected_indexes)
    except Exception as e:
        fastdyn_log.info(f"[binary_wrange.py] Couldn't write {out_file}: {e}")


if __name__ == "__main__":
    fastdyn_log_conf.setLogConfig()

    if len(sys.argv) != 2:
        print("Usage: python binary_wrange.py <firmware.elf>")
        sys.exit(1)

    run("fastdyn_work/bin-writable-ranges", sys.argv[1])
