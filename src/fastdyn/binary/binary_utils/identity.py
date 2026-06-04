from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

from fastdyn import fastdyn_log as fastdyn_log_conf


fastdyn_log = fastdyn_log_conf.getFastdynLogger()

_COMBINED_VERSION_RE = re.compile(
    rb"(?i)(ArduPilot|ArduCopter|ArduPlane|ArduRover|ArduSub|APM:Copter|APM:Plane|APM:Rover|PX4|Betaflight|iNAV|Cleanflight)[-\s]*[Vv]?(\d{1,3}\.\d{1,3}(?:\.\d{1,5})?(?:-[a-zA-Z0-9_.]+)?)"
)
_CONTEXT_VERSION_RE = re.compile(
    rb"(?i)\b(?:version|ver|firmware|fw)\s*[:= -]*[Vv]?(\d{1,3}\.\d{1,3}(?:\.\d{1,5})?(?:-[a-zA-Z0-9_.]+)?)\b"
)
_VERSION_RE_STR = re.compile(r"\b[Vv]?(\d{1,3}\.\d{1,3}(?:\.\d{1,5})?(?:-[a-zA-Z0-9_.]+)?)\b")

_STRING_RULES: list[tuple[re.Pattern[bytes], str, str, str]] = [
    (re.compile(rb"\bArduPilot\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bArduCopter\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bArduPlane\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bArduRover\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bArduSub\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bAPM:Copter\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bAPM:Plane\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bAPM:Rover\b", re.IGNORECASE), "project", "ArduPilot", "strong"),
    (re.compile(rb"\bPX4\b", re.IGNORECASE), "project", "PX4", "weak"),
    (re.compile(rb"\bBetaflight\b", re.IGNORECASE), "project", "Betaflight", "strong"),
    (re.compile(rb"\biNAV\b", re.IGNORECASE), "project", "iNAV", "strong"),
    (re.compile(rb"\bCleanflight\b", re.IGNORECASE), "project", "Cleanflight", "strong"),
    (re.compile(rb"\bArduCopter\b", re.IGNORECASE), "vehicle", "Copter", "strong"),
    (re.compile(rb"\bAPM:Copter\b", re.IGNORECASE), "vehicle", "Copter", "strong"),
    (re.compile(rb"\bCopter::", re.IGNORECASE), "vehicle", "Copter", "strong"),
    (re.compile(rb"\bArduPlane\b", re.IGNORECASE), "vehicle", "Plane", "strong"),
    (re.compile(rb"\bAPM:Plane\b", re.IGNORECASE), "vehicle", "Plane", "strong"),
    (re.compile(rb"\bPlane::", re.IGNORECASE), "vehicle", "Plane", "strong"),
    (re.compile(rb"\bArduRover\b", re.IGNORECASE), "vehicle", "Rover", "strong"),
    (re.compile(rb"\bAPM:Rover\b", re.IGNORECASE), "vehicle", "Rover", "strong"),
    (re.compile(rb"\bRover::", re.IGNORECASE), "vehicle", "Rover", "strong"),
    (re.compile(rb"\bArduSub\b", re.IGNORECASE), "vehicle", "Sub", "strong"),
    (re.compile(rb"\bSub::", re.IGNORECASE), "vehicle", "Sub", "strong"),
    (re.compile(rb"\bQuadPlane\b", re.IGNORECASE), "vehicle", "QuadPlane", "strong"),
    (re.compile(rb"\bBlimp\b", re.IGNORECASE), "vehicle", "Blimp", "strong"),
    (re.compile(rb"\bAntennaTracker\b", re.IGNORECASE), "vehicle", "AntennaTracker", "strong"),
    (re.compile(rb"\bChibiOS\b", re.IGNORECASE), "rtos", "ChibiOS", "strong"),
    (re.compile(rb"\bFreeRTOS\b", re.IGNORECASE), "rtos", "FreeRTOS", "strong"),
    (re.compile(rb"\bNuttX\b", re.IGNORECASE), "rtos", "NuttX", "strong"),
    (re.compile(rb"\bZephyr\b", re.IGNORECASE), "rtos", "Zephyr", "strong"),
    (re.compile(rb"\bRIOT\b", re.IGNORECASE), "rtos", "RIOT", "strong"),
    (re.compile(rb"\bSTM32F427\b", re.IGNORECASE), "board_or_mcu", "STM32F427", "strong"),
    (re.compile(rb"\bSTM32F407\b", re.IGNORECASE), "board_or_mcu", "STM32F407", "strong"),
    (re.compile(rb"\bSTM32F405\b", re.IGNORECASE), "board_or_mcu", "STM32F405", "strong"),
    (re.compile(rb"\bSTM32F767\b", re.IGNORECASE), "board_or_mcu", "STM32F767", "strong"),
    (re.compile(rb"\bSTM32F303\b", re.IGNORECASE), "board_or_mcu", "STM32F303", "strong"),
    (re.compile(rb"\bSTM32H743\b", re.IGNORECASE), "board_or_mcu", "STM32H743", "strong"),
    (re.compile(rb"\bSTM32H757\b", re.IGNORECASE), "board_or_mcu", "STM32H757", "strong"),
    (re.compile(rb"\bCubeOrange\b", re.IGNORECASE), "board_or_mcu", "CubeOrange", "strong"),
    (re.compile(rb"\bCubeBlack\b", re.IGNORECASE), "board_or_mcu", "CubeBlack", "strong"),
    (re.compile(rb"\bMatekF405\b", re.IGNORECASE), "board_or_mcu", "MatekF405", "strong"),
    (re.compile(rb"\bMatekH743\b", re.IGNORECASE), "board_or_mcu", "MatekH743", "strong"),
    (re.compile(rb"\bKakuteF7\b", re.IGNORECASE), "board_or_mcu", "KakuteF7", "strong"),
    (re.compile(rb"\bDurandal\b", re.IGNORECASE), "board_or_mcu", "Durandal", "strong"),
    (re.compile(rb"\bfmuv2\b", re.IGNORECASE), "board_or_mcu", "fmuv2", "strong"),
    (re.compile(rb"\bfmuv3\b", re.IGNORECASE), "board_or_mcu", "fmuv3", "strong"),
    (re.compile(rb"\bfmuv5\b", re.IGNORECASE), "board_or_mcu", "fmuv5", "strong"),
    (re.compile(rb"\bSTM32F4\b", re.IGNORECASE), "board_or_mcu", "STM32F4", "weak"),
    (re.compile(rb"\bSTM32F7\b", re.IGNORECASE), "board_or_mcu", "STM32F7", "weak"),
    (re.compile(rb"\bSTM32H7\b", re.IGNORECASE), "board_or_mcu", "STM32H7", "weak"),
    (re.compile(rb"\bSTM32\b", re.IGNORECASE), "board_or_mcu", "STM32", "weak"),
    (re.compile(rb"\bPixhawk(?:Mini)?\b", re.IGNORECASE), "board_or_mcu", "Pixhawk", "weak"),
]

_DWARF_RULES: list[tuple[re.Pattern[str], str, str]] = [
    (re.compile(r"(?:^|[/\\])ArduCopter(?:[/\\]|$)", re.IGNORECASE), "project", "ArduPilot"),
    (re.compile(r"(?:^|[/\\])ArduCopter(?:[/\\]|$)", re.IGNORECASE), "vehicle", "Copter"),
    (re.compile(r"(?:^|[/\\])ArduPlane(?:[/\\]|$)", re.IGNORECASE), "project", "ArduPilot"),
    (re.compile(r"(?:^|[/\\])ArduPlane(?:[/\\]|$)", re.IGNORECASE), "vehicle", "Plane"),
    (re.compile(r"(?:^|[/\\])ArduRover(?:[/\\]|$)", re.IGNORECASE), "project", "ArduPilot"),
    (re.compile(r"(?:^|[/\\])ArduRover(?:[/\\]|$)", re.IGNORECASE), "vehicle", "Rover"),
    (re.compile(r"(?:^|[/\\])ArduSub(?:[/\\]|$)", re.IGNORECASE), "project", "ArduPilot"),
    (re.compile(r"(?:^|[/\\])ArduSub(?:[/\\]|$)", re.IGNORECASE), "vehicle", "Sub"),
    (re.compile(r"(?:^|[/\\])ardupilot(?:[/\\]|$)", re.IGNORECASE), "project", "ArduPilot"),
    (re.compile(r"\bAP_HAL_ChibiOS\b|\bChibiOS\b", re.IGNORECASE), "rtos", "ChibiOS"),
    (re.compile(r"\bFreeRTOS\b", re.IGNORECASE), "rtos", "FreeRTOS"),
    (re.compile(r"\bNuttX\b", re.IGNORECASE), "rtos", "NuttX"),
    (re.compile(r"(?:^|[/\\])PX4(?:[/\\]|$)", re.IGNORECASE), "project", "PX4"),
]

_SYMBOL_HINTS: list[tuple[str, str, str]] = [
    ("AP_HAL", "project", "ArduPilot"),
    ("AP_Vehicle", "project", "ArduPilot"),
    ("AP_InertialSensor", "project", "ArduPilot"),
    ("GCS_MAVLink", "project", "ArduPilot"),
    ("Copter::", "project", "ArduPilot"),
    ("Copter::", "vehicle", "Copter"),
    ("Plane::", "project", "ArduPilot"),
    ("Plane::", "vehicle", "Plane"),
    ("Rover::", "project", "ArduPilot"),
    ("Rover::", "vehicle", "Rover"),
    ("Sub::", "project", "ArduPilot"),
    ("Sub::", "vehicle", "Sub"),
    ("chSysInit", "rtos", "ChibiOS"),
    ("chThdCreate", "rtos", "ChibiOS"),
    ("chMtxLock", "rtos", "ChibiOS"),
    ("xTaskCreate", "rtos", "FreeRTOS"),
    ("vTaskDelay", "rtos", "FreeRTOS"),
    ("px4_main", "project", "PX4"),
]

_STRING_SECTIONS = (".rodata", ".text", ".data")
_MIN_STRING_LEN = 4


@dataclass(frozen=True)
class IdentityClaim:
    claim: str
    value: str
    strength: str
    evidence_type: str
    evidence: str
    source: str


def _match_bytes_rules(value: bytes) -> list[tuple[str, str, str]]:
    matches = []
    for pattern, claim_type, claim_value, strength in _STRING_RULES:
        if pattern.search(value):
            matches.append((claim_type, claim_value, strength))
    return matches


def _match_text_rules(value: str) -> list[tuple[str, str]]:
    matches = []
    for pattern, claim_type, claim_value in _DWARF_RULES:
        if pattern.search(value):
            matches.append((claim_type, claim_value))
    return matches


def _extract_printable_strings(data: bytes, min_len: int = _MIN_STRING_LEN) -> list[bytes]:
    result: list[bytes] = []
    current: list[int] = []
    for byte in data:
        if 0x20 <= byte < 0x7F:
            current.append(byte)
            continue

        if len(current) >= min_len:
            result.append(bytes(current))
        current = []

    if len(current) >= min_len:
        result.append(bytes(current))
    return result


def _scan_section_strings(elf_file: ELFFile) -> list[IdentityClaim]:
    claims: list[IdentityClaim] = []

    for section_name in _STRING_SECTIONS:
        section = elf_file.get_section_by_name(section_name)
        if section is None:
            continue

        for value in _extract_printable_strings(section.data()):
            value_str = value.decode("ascii", errors="ignore")

            match = _COMBINED_VERSION_RE.search(value)
            if match:
                claims.append(IdentityClaim(
                    claim="version",
                    value=match.group(2).decode("ascii", errors="ignore"),
                    strength="strong",
                    evidence_type="elf_string",
                    evidence=value_str,
                    source=f"section_string_scan ({section_name})",
                ))

            for version_match in _CONTEXT_VERSION_RE.finditer(value):
                claims.append(IdentityClaim(
                    claim="version",
                    value=version_match.group(1).decode("ascii", errors="ignore"),
                    strength="weak",
                    evidence_type="elf_string",
                    evidence=value_str,
                    source=f"section_string_scan ({section_name})",
                ))

            for claim_type, claim_value, strength in _match_bytes_rules(value):
                claims.append(IdentityClaim(
                    claim=claim_type,
                    value=claim_value,
                    strength=strength,
                    evidence_type="elf_string",
                    evidence=value_str,
                    source=f"section_string_scan ({section_name})",
                ))

    return claims


def _die_attr_str(die, attr_name: str) -> str | None:
    attr = die.attributes.get(attr_name)
    if attr is None:
        return None
    value = attr.value
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _scan_dwarf_compile_units(elf_file: ELFFile) -> list[IdentityClaim]:
    claims: list[IdentityClaim] = []
    if not elf_file.has_dwarf_info():
        return claims

    try:
        dwarf = elf_file.get_dwarf_info()
    except Exception as exc:
        fastdyn_log.warning("Failed to parse DWARF info: %s", exc)
        return claims

    for cu in dwarf.iter_CUs():
        try:
            die = cu.get_top_DIE()
        except Exception:
            continue

        comp_dir = _die_attr_str(die, "DW_AT_comp_dir")
        name = _die_attr_str(die, "DW_AT_name")

        for path_str in (comp_dir, name):
            if not path_str:
                continue

            is_absolute = path_str.startswith("/") or re.match(r"^[a-zA-Z]:[\\/]", path_str) is not None
            strength = "weak" if is_absolute else "strong"
            has_project_hint = False

            for claim_type, claim_value in _match_text_rules(path_str):
                if claim_type == "project":
                    has_project_hint = True
                claims.append(IdentityClaim(
                    claim=claim_type,
                    value=claim_value,
                    strength=strength,
                    evidence_type="dwarf_path",
                    evidence=path_str,
                    source=f"DWARF CU: {name or comp_dir}",
                ))

            if has_project_hint:
                for version_match in _VERSION_RE_STR.finditer(path_str):
                    version_strength = "strong" if strength == "strong" else "weak"
                    claims.append(IdentityClaim(
                        claim="version",
                        value=version_match.group(1),
                        strength=version_strength,
                        evidence_type="dwarf_path",
                        evidence=path_str,
                        source=f"DWARF CU: {name or comp_dir}",
                    ))

    return claims


def _scan_elf_symbols(elf_file: ELFFile) -> list[IdentityClaim]:
    claims: list[IdentityClaim] = []
    for section in elf_file.iter_sections():
        if not isinstance(section, SymbolTableSection):
            continue

        for symbol in section.iter_symbols():
            if not symbol.name:
                continue

            if symbol.name.startswith("AP_") or "_ZN2AP" in symbol.name or "_ZN3AP" in symbol.name:
                claims.append(IdentityClaim(
                    claim="project",
                    value="ArduPilot",
                    strength="strong",
                    evidence_type="elf_symbol",
                    evidence=symbol.name,
                    source=f"symbol_table ({section.name})",
                ))

            for hint_str, claim_type, claim_value in _SYMBOL_HINTS:
                if hint_str in symbol.name:
                    claims.append(IdentityClaim(
                        claim=claim_type,
                        value=claim_value,
                        strength="strong",
                        evidence_type="elf_symbol",
                        evidence=symbol.name,
                        source=f"symbol_table ({section.name})",
                    ))

    return claims


def _claim_to_dict(claim: IdentityClaim, conflict_reason: str | None = None) -> dict:
    result = {
        "claim": claim.claim,
        "value": claim.value,
        "evidence_type": claim.evidence_type,
        "evidence": claim.evidence,
        "source": claim.source,
    }
    if conflict_reason:
        result["conflict_reason"] = conflict_reason
    return result


def _resolve_claims(claims: list[IdentityClaim]) -> tuple[dict, list[dict], list[dict], list[dict]]:
    summary_out = {}
    strong_out = []
    candidates_out = []
    conflict_out = []

    by_category: dict[str, list[IdentityClaim]] = {}
    for claim in claims:
        by_category.setdefault(claim.claim, []).append(claim)

    for category, category_claims in by_category.items():
        strong_values = {claim.value for claim in category_claims if claim.strength == "strong"}
        weak_values = {claim.value for claim in category_claims if claim.strength == "weak"}

        if category == "board_or_mcu" and len(strong_values) > 1:
            strong_values = {
                value
                for value in strong_values
                if not any(value != other and value in other for other in strong_values)
            }

        if category == "board_or_mcu" and len(weak_values) > 1:
            weak_values = {
                value
                for value in weak_values
                if not any(value != other and value in other for other in weak_values)
            }

        if len(strong_values) == 1:
            value = next(iter(strong_values))
            confidence = "high"
            matched_claims = [
                claim
                for claim in category_claims
                if (
                    claim.value == value
                    or (
                        category == "board_or_mcu"
                        and (claim.value in value or value in claim.value)
                    )
                )
                and claim.strength == "strong"
            ]
        elif not strong_values and len(weak_values) == 1:
            value = next(iter(weak_values))
            confidence = "medium"
            matched_claims = [claim for claim in category_claims if claim.value == value]
        else:
            value = None
            confidence = "low"
            matched_claims = category_claims

        if value is not None:
            examples = []
            for claim in matched_claims:
                if claim.evidence not in examples:
                    examples.append(claim.evidence)
                if len(examples) >= 5:
                    break

            summary_out[category] = {
                "value": value,
                "confidence": confidence,
                "evidence_count": len(matched_claims),
                "examples": examples,
            }

        if len(strong_values) > 1:
            reason = f"Multiple strong values found for {category}: {', '.join(sorted(strong_values))}"
            seen = set()
            for claim in category_claims:
                if claim.evidence in seen:
                    continue
                seen.add(claim.evidence)
                if len(seen) <= 5:
                    conflict_out.append(_claim_to_dict(claim, conflict_reason=reason))
            continue

        if len(strong_values) == 1:
            strong_value = next(iter(strong_values))
            strong_seen = set()
            conflict_seen = set()
            for claim in category_claims:
                matches = claim.value == strong_value
                if category == "board_or_mcu":
                    matches = claim.value in strong_value or strong_value in claim.value

                if claim.strength == "strong" and matches:
                    if claim.evidence not in strong_seen and len(strong_seen) < 5:
                        strong_seen.add(claim.evidence)
                        strong_out.append(_claim_to_dict(claim))
                    continue

                if not matches and claim.evidence not in conflict_seen and len(conflict_seen) < 5:
                    conflict_seen.add(claim.evidence)
                    conflict_out.append(_claim_to_dict(
                        claim,
                        conflict_reason=f"Conflicts with strong {category} claim: {strong_value}",
                    ))
            continue

        if len(weak_values) > 1:
            reason = f"Multiple conflicting weak values found for {category}: {', '.join(sorted(weak_values))}"
            seen = set()
            for claim in category_claims:
                if claim.evidence in seen:
                    continue
                seen.add(claim.evidence)
                if len(seen) <= 5:
                    conflict_out.append(_claim_to_dict(claim, conflict_reason=reason))
            continue

        if len(weak_values) == 1:
            seen = set()
            for claim in category_claims:
                if claim.evidence in seen:
                    continue
                seen.add(claim.evidence)
                if len(seen) <= 5:
                    candidates_out.append(_claim_to_dict(claim))

    return summary_out, strong_out, candidates_out, conflict_out


def _empty_result(limitations: list[str]) -> dict:
    return {
        "summary": {},
        "strong_identity_claims": [],
        "candidates": [],
        "weak_or_conflicting_evidence": [],
        "limitations": limitations,
    }


def extract_firmware_identity(binary_path: str | Path) -> dict:
    binary_path = Path(binary_path)
    fastdyn_log.info("Firmware identity: %s", binary_path)

    limitations: list[str] = []
    claims: list[IdentityClaim] = []

    try:
        with binary_path.open("rb") as f:
            elf_file = ELFFile(f)
            claims.extend(_scan_section_strings(elf_file))
            claims.extend(_scan_dwarf_compile_units(elf_file))
            claims.extend(_scan_elf_symbols(elf_file))
    except FileNotFoundError:
        return _empty_result([f"Binary not found: {binary_path}"])
    except Exception as exc:
        return _empty_result([f"Failed to open ELF: {exc}"])

    if not claims:
        limitations.append("No readable claims or identifiers extracted from binary.")

    summary_out, strong_out, candidates_out, conflict_out = _resolve_claims(claims)
    found_categories = {claim["claim"] for claim in (strong_out + candidates_out + conflict_out)}

    if "version" not in found_categories:
        limitations.append("No version strings detected in ELF sections.")
    if "project" not in found_categories:
        limitations.append("No project identifiers found in strings, DWARF, or symbols.")
    if "vehicle" not in found_categories:
        limitations.append("No vehicle-type identifiers found.")
    if "rtos" not in found_categories:
        limitations.append("No RTOS identifiers found.")
    if "board_or_mcu" not in found_categories:
        limitations.append("No board/MCU identifiers found.")

    return {
        "summary": summary_out,
        "strong_identity_claims": strong_out,
        "candidates": candidates_out,
        "weak_or_conflicting_evidence": conflict_out,
        "limitations": limitations,
    }
