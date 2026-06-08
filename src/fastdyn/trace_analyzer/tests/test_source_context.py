from __future__ import annotations

from pathlib import Path

from fastdyn.trace_analyzer.models import ResolvedFunction
from fastdyn.trace_analyzer.pipeline.source_context import build_source_context


def test_build_source_context_extracts_braced_function(tmp_path: Path) -> None:
    source_file = tmp_path / "clock.c"
    source_file.write_text(
        "\n".join(
            [
                "/* Doc comment with { ignored } */",
                "static int wait_for_clock(int x)",
                "{",
                "  const char *brace = \"}\";",
                "  while (x) {",
                "    x--;",
                "  }",
                "  return x;",
                "}",
                "",
                "int other(void) { return 1; }",
            ]
        ),
        encoding="utf-8",
    )
    resolved = ResolvedFunction(
        pc=0x1000,
        pc_hex="0x1000",
        name="wait_for_clock",
        source_path=source_file,
        source_root_relative="drivers/clock.c",
        line=2,
        confidence="high",
        source_map_entry={"provider": "dwarf"},
    )

    context = build_source_context(resolved)

    assert context.function == "wait_for_clock"
    assert context.source_path == source_file
    assert context.source_root_relative == "drivers/clock.c"
    assert context.line == 2
    assert context.start_line == 1
    assert context.end_line == 9
    assert context.extraction == "function"
    assert "static int wait_for_clock" in context.text
    assert "int other" not in context.text
    assert context.provider == "dwarf"
    assert context.warnings == []


def test_build_source_context_uses_source_map_fallback(tmp_path: Path) -> None:
    source_file = tmp_path / "clock.c"
    source_file.write_text("void mapped_function(void) {\n}\n", encoding="utf-8")
    resolved = ResolvedFunction(
        pc=0x1000,
        pc_hex="0x1000",
        name="mapped_function",
        confidence="high",
    )

    context = build_source_context(
        resolved,
        source_map=[
            {
                "function": "mapped_function",
                "exists_locally": True,
                "resolved_path": str(source_file),
                "source_root_relative": "drivers/clock.c",
                "line": "1",
                "provider": "dwarf",
            }
        ],
    )

    assert context.source_path == source_file
    assert context.source_root_relative == "drivers/clock.c"
    assert context.line == 1
    assert context.extraction == "function"
    assert context.text == "void mapped_function(void) {\n}"


def test_build_source_context_reports_missing_source(tmp_path: Path) -> None:
    missing_file = tmp_path / "missing.c"
    resolved = ResolvedFunction(
        pc=0x1000,
        pc_hex="0x1000",
        name="missing_function",
        source_path=missing_file,
        source_root_relative="drivers/missing.c",
        line=7,
        confidence="high",
    )

    context = build_source_context(resolved)

    assert context.function == "missing_function"
    assert context.source_path == missing_file
    assert context.source_root_relative == "drivers/missing.c"
    assert context.line == 7
    assert context.extraction == "unavailable"
    assert context.text == ""
    assert context.warnings == [f"source file does not exist: {missing_file}"]
