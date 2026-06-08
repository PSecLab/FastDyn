from __future__ import annotations

from fastdyn.trace_analyzer.pipeline.exec_trace import build_exec_trace_context


def test_build_exec_trace_context_detects_alternating_tail() -> None:
    probe_result = {
        "pc": "0x00001012",
        "bbl_trace": [
            "0x00001000",
            "0x00001004",
            "0x00001010",
            "0x00001014",
            "0x00001010",
            "0x00001014",
            "0x00001010",
            "0x00001014",
        ],
    }
    functions = [
        {
            "name": "wait_for_clock",
            "start": "0x1000",
            "end": "0x1020",
            "confidence": "high",
        }
    ]
    symbols = [
        {
            "name": "wait_for_clock",
            "address": "0x1000",
            "size": "0x20",
        }
    ]
    source_map = [
        {
            "function": "wait_for_clock",
            "source_root_relative": "drivers/clock.c",
            "line": 42,
            "exists_locally": False,
        }
    ]

    context = build_exec_trace_context(
        probe_result,
        functions=functions,
        symbols=symbols,
        source_map=source_map,
        recent_limit=6,
    )

    assert context.final_pc == "0x00001012"
    assert context.final_function == "wait_for_clock"
    assert context.raw_bbl_count == 8
    assert context.recent_bbl_trace == [
        "0x00001010",
        "0x00001014",
        "0x00001010",
        "0x00001014",
        "0x00001010",
        "0x00001014",
    ]
    assert context.collapsed_sequence == [{"name": "wait_for_clock", "count": 6}]
    assert context.loop_hint == {
        "kind": "alternating_bbl_pair",
        "bbl_pair": ["0x00001010", "0x00001014"],
        "function_pair": ["wait_for_clock", "wait_for_clock"],
        "repeat_count": 3,
        "tail_length": 6,
    }
    assert context.resolved_recent_bbls[-1]["source_root_relative"] == "drivers/clock.c"
    assert context.resolved_recent_bbls[-1]["line"] == 42


def test_build_exec_trace_context_detects_repeated_bbl_tail() -> None:
    probe_result = {
        "pc": "0x1004",
        "bbl_trace": ["0x1000", "0x1004", "0x1004", "0x1004"],
    }
    functions = [
        {
            "name": "spin_once",
            "start": "0x1000",
            "end": "0x1010",
        }
    ]

    context = build_exec_trace_context(
        probe_result,
        functions=functions,
        symbols=[],
        recent_limit=4,
    )

    assert context.final_function == "spin_once"
    assert context.loop_hint == {
        "kind": "repeated_bbl",
        "bbl": "0x1004",
        "function": "spin_once",
        "repeat_count": 3,
        "tail_length": 3,
    }
