from __future__ import annotations

import re
from pathlib import Path

from elftools.elf.elffile import ELFFile

from fastdyn import fastdyn_log as fastdyn_log_conf


fastdyn_log = fastdyn_log_conf.getFastdynLogger()

_MIN_STRING_LENGTH = 4
_TARGET_SECTIONS: tuple[str, ...] = (
    ".rodata",
    ".text",
    ".data",
)
_DEBUG_SECTION_PREFIXES: tuple[str, ...] = (
    ".debug_str",
    ".debug_line_str",
)

_RE_VERSION = re.compile(r"[Vv]\d+\.\d+(?:\.\d+)?(?:[-_.]\w+)?")
_RE_PROJECT = re.compile(
    r"(?:ArduPilot|ArduCopter|ArduPlane|ArduRover|ArduSub|AntennaTracker"
    r"|ChibiOS|PX4|Betaflight|iNav|Cleanflight|LibrePilot)",
    re.IGNORECASE,
)
_RE_BOARD_MCU = re.compile(
    r"(?:STM32\w*|F[0-9]{3}|H7[0-9]{2}|L4[0-9]{2}"
    r"|nRF\w+|ESP32\w*|ATSAM\w+"
    r"|CubeOrange|CubeBlack|Pixhawk\w*|Durandal|MatekF\w+"
    r"|KakuteF\w+|Nucleo\w*)",
    re.IGNORECASE,
)
_RE_RTOS = re.compile(
    r"(?:ChibiOS|FreeRTOS|NuttX|Zephyr|RTEMS|ThreadX)",
    re.IGNORECASE,
)
_RE_MAVLINK = re.compile(
    r"(?:MAV_CMD_|MAV_TYPE_|MAV_MODE_|MAV_STATE_|MAV_RESULT_"
    r"|PARAM_|MISSION_|FENCE_|RALLY_|MAV_COMP_"
    r"|MAVLINK|mavlink)",
)
_RE_PATH = re.compile(r"[/\\]\w+[/\\]")
_RE_URL = re.compile(r"https?://", re.IGNORECASE)
_RE_ERROR_LOG = re.compile(
    r"(?:Error:|Warning:|Assert|ASSERT|PANIC|FAULT|abort|Fail(?:ed|ure)?:)",
    re.IGNORECASE,
)

_CLASSIFIERS: list[tuple[str, re.Pattern[str]]] = [
    ("version-like", _RE_VERSION),
    ("project-like", _RE_PROJECT),
    ("rtos-like", _RE_RTOS),
    ("mavlink/arduPilot-like", _RE_MAVLINK),
    ("board/mcu-like", _RE_BOARD_MCU),
    ("url-like", _RE_URL),
    ("filesystem/path-like", _RE_PATH),
    ("error/log-like", _RE_ERROR_LOG),
]


def _classify(value: str) -> str:
    for label, pattern in _CLASSIFIERS:
        if pattern.search(value):
            return label
    return "other"


def _extract_printable_strings(data: bytes, min_length: int = _MIN_STRING_LENGTH) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    current: list[int] = []
    start = 0

    for index, byte in enumerate(data):
        if 0x20 <= byte <= 0x7E or byte in (0x09, 0x0A, 0x0D):
            if not current:
                start = index
            current.append(byte)
            continue

        if len(current) >= min_length:
            value = bytes(current).decode("ascii", errors="replace").strip()
            if len(value) >= min_length:
                result.append((start, value))
        current = []

    if len(current) >= min_length:
        value = bytes(current).decode("ascii", errors="replace").strip()
        if len(value) >= min_length:
            result.append((start, value))

    return result


def _resolve_debug_sections(elf_file: ELFFile) -> list[str]:
    found = []
    for section in elf_file.iter_sections():
        for prefix in _DEBUG_SECTION_PREFIXES:
            if section.name and section.name.startswith(prefix):
                found.append(section.name)
    return found


def extract_strings(binary_path: str | Path, max_strings: int = 2000) -> dict:
    binary_path = Path(binary_path)
    fastdyn_log.info("String extraction: %s", binary_path)

    all_entries: list[dict] = []
    sections_scanned: list[str] = []

    with binary_path.open("rb") as f:
        elf_file = ELFFile(f)
        section_names = list(_TARGET_SECTIONS)
        section_names.extend(_resolve_debug_sections(elf_file))

        for section_name in section_names:
            section = elf_file.get_section_by_name(section_name)
            if section is None:
                continue

            sections_scanned.append(section_name)
            for offset, value in _extract_printable_strings(section.data()):
                all_entries.append({
                    "value": value,
                    "section": section_name,
                    "offset": offset,
                    "classification": _classify(value),
                })

    total_found = len(all_entries)
    truncated = total_found > max_strings
    returned = all_entries[:max_strings]

    classification_summary: dict[str, int] = {}
    for entry in returned:
        label = entry["classification"]
        classification_summary[label] = classification_summary.get(label, 0) + 1

    return {
        "sections_scanned": sections_scanned,
        "total_found": total_found,
        "total_returned": len(returned),
        "truncated": truncated,
        "strings": returned,
        "classification_summary": classification_summary,
    }
