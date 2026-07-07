#!/usr/bin/env python3
"""Summarize ArduRover MMIO rehosting progress without perturbing QEMU."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
from typing import Any


DEFAULT_ELF = Path("virtuals/physics/flight_controllers/courbet/bin/ardurover_v462")

MILESTONES = {
    "post_periodic_callback": "0x0811a878",
    "post_WithSemaphore_dtor": "0x0811a87e",
    "Invensense_start_return": "0x0811a4e6",
    "backend_start_returned": "0x080436d2",
    "inertial_init_after_start_backends": "0x080437da",
    "init_gyro": "0x08042a5c",
    "wait_for_sample": "0x08041d08",
    "scheduler_loop": "0x0807c278",
}

OLD_MARKERS = {
    "post_periodic_callback": "old Invensense::start reached post-periodic-callback",
    "post_WithSemaphore_dtor": "old Invensense::start returned from WithSemaphore dtor",
    "Invensense_start_return": "old Invensense::start returning",
    "backend_start_returned": "_start_backends backend->start returned",
    "inertial_init_after_start_backends": "AP_InertialSensor::init returned from _start_backends",
    "init_gyro": "AP_InertialSensor::init_gyro reached",
    "wait_for_sample": "AP_InertialSensor::wait_for_sample reached",
    "scheduler_loop": "AP_Scheduler::loop reached",
}

SPARSE_RE = re.compile(r"\[SPARSE\]\s+([A-Za-z0-9_]+)")


def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    return value if isinstance(value, dict) else {}


def normalize_pc(pc: str) -> str:
    return f"0x{int(str(pc), 16):08x}"


def resolve_pc(elf: Path, pc: str) -> str:
    if not elf.exists():
        return "unknown"
    try:
        out = subprocess.check_output(
            ["arm-none-eabi-addr2line", "-e", str(elf), "-f", "-C", pc],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip().splitlines()
    except Exception:
        return "unknown"
    if not out:
        return "unknown"
    function = out[0]
    source = out[1] if len(out) > 1 else "??:0"
    return f"{function} ({source})"


def parse_gdb_markers(path: Path | None) -> dict[str, bool]:
    markers = {name: False for name in MILESTONES}
    if path is None or not path.exists():
        return markers

    text = path.read_text(encoding="utf-8", errors="replace")
    for match in SPARSE_RE.finditer(text):
        name = match.group(1)
        if name in markers:
            markers[name] = True

    for name, needle in OLD_MARKERS.items():
        if needle in text:
            markers[name] = True

    return markers


def retained_bbl_hits(probe: dict[str, Any]) -> dict[str, bool]:
    raw = probe.get("bbl_trace") or []
    pcs: set[str] = set()
    if isinstance(raw, list):
        for item in raw:
            if isinstance(item, dict) and "pc" in item:
                try:
                    pcs.add(normalize_pc(str(item["pc"])))
                except ValueError:
                    pass
            elif isinstance(item, str):
                try:
                    pcs.add(normalize_pc(item))
                except ValueError:
                    pass
    return {name: normalize_pc(pc) in pcs for name, pc in MILESTONES.items()}


def recent_switch_counts(path: Path) -> Counter[str]:
    counts: Counter[str] = Counter()
    if not path.exists():
        return counts
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            incoming = event.get("in") if isinstance(event, dict) else None
            if isinstance(incoming, dict):
                name = str(incoming.get("name") or "<unnamed>")
                counts[name] += 1
    return counts


def top_threads(summary: dict[str, Any]) -> list[tuple[str, int, int, int]]:
    threads = summary.get("threads") or []
    rows: list[tuple[str, int, int, int]] = []
    if not isinstance(threads, list):
        return rows
    for thread in threads:
        if not isinstance(thread, dict):
            continue
        rows.append(
            (
                str(thread.get("name") or "<unnamed>"),
                int(thread.get("priority") or 0),
                int(thread.get("state") or 0),
                int(thread.get("scheduled_count") or 0),
            )
        )
    rows.sort(key=lambda item: item[3], reverse=True)
    return rows


def missed_alarm_count(qemu_log: Path) -> int:
    if not qemu_log.exists():
        return 0
    text = qemu_log.read_text(encoding="utf-8", errors="replace")
    return text.count("Missed ")


def verdict(markers: dict[str, bool], bbl_hits: dict[str, bool], summary: dict[str, Any]) -> str:
    reached_post_cb = markers["post_periodic_callback"] or bbl_hits["post_periodic_callback"]
    reached_return = (
        markers["post_WithSemaphore_dtor"]
        or markers["Invensense_start_return"]
        or markers["backend_start_returned"]
        or bbl_hits["post_WithSemaphore_dtor"]
        or bbl_hits["Invensense_start_return"]
        or bbl_hits["backend_start_returned"]
    )

    if markers["scheduler_loop"] or bbl_hits["scheduler_loop"]:
        return "Scheduler loop reached. This specific startup blocker is cleared."
    if markers["init_gyro"] or bbl_hits["init_gyro"]:
        return "Reached gyro initialization. Next blocker is likely calibration/sample health, not backend start."
    if reached_post_cb and not reached_return:
        return (
            "Stall is after DeviceBus periodic callback registration and before backend start returns. "
            "Treat timer/yield/scheduler semantics as primary suspects."
        )
    if reached_return:
        return "Backend returned at least once in retained evidence; inspect the next missing milestone."

    current = summary.get("current_thread") if isinstance(summary, dict) else None
    if isinstance(current, dict):
        return f"No sparse milestone evidence; final RTOS thread is {current.get('name')}."
    return "Insufficient evidence. Run sparse GDB once or a no-GDB RTOS introspection run."


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-w",
        "--work-dir",
        default="fastdyn_recent_run",
        type=Path,
        help="FastDyn run directory containing probe_result.json",
    )
    parser.add_argument(
        "-g",
        "--gdb-log",
        default=Path("fastdyn_work/gdb_diag.log"),
        type=Path,
        help="Optional GDB log from the sparse progress script",
    )
    parser.add_argument(
        "--elf",
        default=DEFAULT_ELF,
        type=Path,
        help="Firmware ELF for addr2line resolution",
    )
    args = parser.parse_args()

    work_dir = args.work_dir
    probe_path = work_dir / "probe_result.json"
    summary_path = work_dir / "rtos_summary.json"
    probe = load_json(probe_path)
    summary = load_json(summary_path)
    switches_path = work_dir / "rtos_recent_switches.jsonl"
    qemu_log = work_dir / "qemu.log"

    markers = parse_gdb_markers(args.gdb_log)
    bbl_hits = retained_bbl_hits(probe)
    recent_counts = recent_switch_counts(switches_path)

    exit_reason = probe.get("exit_reason", "unknown")
    pc = str(probe.get("pc", "0x0"))
    try:
        pc_norm = normalize_pc(pc)
    except ValueError:
        pc_norm = pc

    print("== Run ==")
    print(f"work_dir: {work_dir}")
    if not probe_path.exists():
        print(f"missing_probe_result: {probe_path}")
    if not summary_path.exists():
        print(f"missing_rtos_summary: {summary_path}")
    if args.gdb_log and not args.gdb_log.exists():
        print(f"missing_gdb_log: {args.gdb_log}")
    print(f"exit_reason: {exit_reason}")
    print(f"pc: {pc_norm} -> {resolve_pc(args.elf, pc_norm) if pc_norm.startswith('0x') else 'unknown'}")
    print(f"unique_bbl: {probe.get('unique_bbl_executed', 'unknown')}")
    print(f"total_bbl: {probe.get('total_bbl_executed', 'unknown')}")
    print(f"missed_alarm_lines: {missed_alarm_count(qemu_log)}")

    print("\n== Milestones ==")
    for name, pc_value in MILESTONES.items():
        gdb = "yes" if markers[name] else "no"
        retained = "yes" if bbl_hits[name] else "no"
        print(f"{name:34s} gdb={gdb:3s} retained_bbl={retained:3s} pc={pc_value}")

    current = summary.get("current_thread") if isinstance(summary, dict) else None
    print("\n== RTOS ==")
    if isinstance(current, dict):
        print(
            "current_thread: "
            f"{current.get('name')} addr={current.get('addr')} "
            f"prio={current.get('priority')} state={current.get('state')}"
        )
    else:
        print("current_thread: unavailable")

    print("top scheduled threads:")
    for name, prio, state, count in top_threads(summary)[:8]:
        print(f"  {name:10s} prio={prio:3d} state={state:2d} scheduled={count}")

    if recent_counts:
        print("recent incoming thread switches:")
        for name, count in recent_counts.most_common(8):
            print(f"  {name:10s} {count}")

    print("\n== Verdict ==")
    print(verdict(markers, bbl_hits, summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
