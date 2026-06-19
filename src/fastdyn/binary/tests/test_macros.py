import gzip
import json

from fastdyn.binary.binary_utils.macros import (
    collect_readelf_definitions,
    definition_map,
    final_definition_list,
    match_compile_command,
    parse_preprocessor_defines,
    parse_readelf_macro_dump,
    prepare_preprocess_command,
    stable_unit_id,
)
from fastdyn.binary.passes.macros import _context_index, _write_unit_artifact
from fastdyn.binary.static_analyze import MacroAnalysisOptions


def test_macro_options_defaults_disabled_when_section_absent():
    options = MacroAnalysisOptions.from_toml({})

    assert options.enabled is False
    assert options.strategy == "auto"


def test_macro_options_section_defaults_enabled():
    options = MacroAnalysisOptions.from_toml({"strategy": "rebuild"})

    assert options.enabled is True
    assert options.strategy == "rebuild"


def test_stable_unit_id_is_deterministic():
    unit = {
        "source_root_relative": "modules/ChibiOS/os/hal/hal_lld.c",
        "original_comp_dir": "/build/CubeBlack",
    }

    assert stable_unit_id(unit, 4) == stable_unit_id(unit, 4)
    assert stable_unit_id(unit, 4).endswith("_hal_lld.c")


def test_parse_preprocessor_defines_and_last_definition_wins():
    definitions = parse_preprocessor_defines(
        "#define STM32_HSE_ENABLED TRUE\n"
        "#define STM32_HSE_ENABLED FALSE\n"
        "#define FOO(x) ((x) + 1)\n"
    )

    final = definition_map(final_definition_list(definitions))
    assert final["STM32_HSE_ENABLED"] == "FALSE"
    assert final["FOO"] == "((x) + 1)"


def test_readelf_macro_dump_imports_are_collected():
    dump = """
  Offset:                      0x10
  Version:                     5

 DW_MACRO_define_strp - lineno : 0 macro : BUILTIN 1

  Offset:                      0x20
  Version:                     5

 DW_MACRO_import - offset : 0x10
 DW_MACRO_start_file - lineno: 0 filenum: 1 filename: main.c
 DW_MACRO_define_strp - lineno : 4 macro : LOCAL_VALUE 7
 DW_MACRO_end_file
"""

    blocks = parse_readelf_macro_dump(dump)
    definitions = collect_readelf_definitions(blocks, 0x20)
    macro_map = definition_map(final_definition_list(definitions))

    assert macro_map["BUILTIN"] == "1"
    assert macro_map["LOCAL_VALUE"] == "7"


def test_compile_command_matching_and_preprocess_rewrite(tmp_path):
    source = tmp_path / "src" / "main.c"
    source.parent.mkdir()
    source.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    obj = tmp_path / "main.o"
    entry = {
        "directory": str(tmp_path),
        "file": "src/main.c",
        "arguments": [
            "arm-none-eabi-gcc",
            "-Iinclude",
            "-DVALUE=1",
            "-MMD",
            "-c",
            "src/main.c",
            "-o",
            str(obj),
        ],
    }
    unit = {"resolved_path": str(source)}

    matched = match_compile_command(unit, [entry])
    command = prepare_preprocess_command(matched["arguments"], str(source))

    assert matched == entry
    assert command[:3] == ["arm-none-eabi-gcc", "-dM", "-E"]
    assert "-DVALUE=1" in command
    assert "-c" not in command
    assert "-MMD" not in command
    assert str(obj) not in command
    assert command[-1] == str(source)


def test_unit_writer_deduplicates_full_compressed_contexts(tmp_path):
    units_dir = tmp_path / "macros" / "units"
    contexts_dir = tmp_path / "macros" / "contexts"
    units_dir.mkdir(parents=True)
    contexts_dir.mkdir(parents=True)
    definitions = [
        {"kind": "define", "name": "STM32_HSECLK", "signature": "STM32_HSECLK", "value": "24000000U"},
        {"kind": "define", "name": "STM32_PLLM_VALUE", "signature": "STM32_PLLM_VALUE", "value": "24"},
    ]

    first = _write_unit_artifact(
        units_dir=units_dir,
        contexts_dir=contexts_dir,
        unit={"source_root_relative": "a.c"},
        unit_id="unit_a",
        provider="dwarf_debug_macro",
        confidence="exact",
        strategy="dwarf",
        definitions=definitions,
        provenance={},
        limitations=[],
    )
    second = _write_unit_artifact(
        units_dir=units_dir,
        contexts_dir=contexts_dir,
        unit={"source_root_relative": "b.c"},
        unit_id="unit_b",
        provider="dwarf_debug_macro",
        confidence="exact",
        strategy="dwarf",
        definitions=list(reversed(definitions)),
        provenance={},
        limitations=[],
    )

    assert first["context_hash"] == second["context_hash"]
    assert len(list(contexts_dir.glob("*.json.gz"))) == 1

    context_path = tmp_path / first["context_artifact"]
    with gzip.open(context_path, "rt", encoding="utf-8") as f:
        payload = json.load(f)

    assert payload["macro_count"] == 2
    assert payload["definition_map"]["STM32_HSECLK"] == "24000000U"
    assert _context_index([
        {"context_hash": first["context_hash"], "context_artifact": first["context_artifact"], "macro_count": 2},
        {"context_hash": second["context_hash"], "context_artifact": second["context_artifact"], "macro_count": 2},
    ])[0]["unit_count"] == 2
