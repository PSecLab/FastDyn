"""Tests for the compositional (multi-mname) verifier prompt path.

Covers:
- iteration_prompt_gen_multiple_periph branches: default, no_verifier,
  no_encoder, no_rca, and the orthogonal no_vio lens.
- The five helpers extracted from that function:
  _build_compositional_model_source_blocks, _render_apis_and_rules,
  _build_compositional_diff_blocks, _read_trace_strip_systick,
  _write_prompt.
- Backward compatibility: the default branch (no flags) must produce
  the same shape as before the refactor.
- Forward compatibility: each ablation flag produces a prompt with the
  correct contents and writes the expected filename.

All tests use mocks or temporary fixtures — no real verifier or LLM.
"""

import os
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import pytest

from fastdyn.verifier.prompt_gen import (
    _build_compositional_diff_blocks,
    _build_compositional_model_source_blocks,
    _read_trace_strip_systick,
    _render_apis_and_rules,
    _write_prompt,
    iteration_prompt_gen_multiple_periph,
    qemu_api_list,
    qemu_base_api_list,
)


# ---------------------------------------------------------------------------
# Shared fixtures
# ---------------------------------------------------------------------------

def _make_diff_obj(
    diff_init_data="[init]",
    diff_loop_pattern_data="[loop]",
    rare_transitions_data="[rare]",
    diff_state_data="[state]",
    diff_entropy_data="[entropy]",
    diff_runtime_trace="[runtime]",
    isr_analysis_data="[isr]",
    svd_bitfields_data="[bitfields]",
):
    """Build a fake diff object matching what verify_automata returns."""
    return SimpleNamespace(
        diff_init_data=diff_init_data,
        diff_loop_pattern_data=diff_loop_pattern_data,
        rare_transitions_data=rare_transitions_data,
        diff_state_data=diff_state_data,
        diff_entropy_data=diff_entropy_data,
        diff_runtime_trace=diff_runtime_trace,
        isr_analysis_data=isr_analysis_data,
        svd_bitfields_data=svd_bitfields_data,
    )


@pytest.fixture
def two_models(tmp_path):
    """Two model files, mimicking ADC+DMA. Returns {model_name: file_path}."""
    adc_path = tmp_path / "model.c"
    dma_path = tmp_path / "model2.c"
    adc_path.write_text("// ADC model\nvoid* adc_init(void) { return 0; }\n")
    dma_path.write_text(
        "// DMA model\nvoid* dma_with_dmamux1_init(void) { return 0; }\n"
    )
    return {
        "ADC": str(adc_path),
        "DMA_with_DMAMUX1": str(dma_path),
    }


@pytest.fixture
def two_model_sources_map():
    """ADC+DMA -ms mapping required by --no-rca / --no-verifier in compositional mode."""
    return {
        "ADC": ["ADC1", "ADC2", "ADC12_Common"],
        "DMA_with_DMAMUX1": ["DMA1", "DMAMUX1"],
    }


@pytest.fixture
def cm_paths(tmp_path):
    """Minimal encoder-output directory layout that the verifier expects."""
    hw = tmp_path / "out_cm_hw"
    em = tmp_path / "out_cm_em"
    hw.mkdir()
    em.mkdir()
    (hw / "summary.txt").write_text("Platform: STM32H753x\n")
    (em / "summary.txt").write_text("Platform: STM32H753x\n")
    return str(hw), str(em)


@pytest.fixture
def io_logs(tmp_path):
    """Raw HW + EM io.logs with mixed MMIO and SysTick lines."""
    hw_path = tmp_path / "io_hw.log"
    em_path = tmp_path / "io_em.log"
    hw_path.write_text(
        "[  0.1] Read:  address = 0x40020000, size = 4 bytes, value = 0x0, pc=0x8001\n"
        "[  0.2] Interrupt Taken: Vector = 0x0000000F\n"
        "[  0.3] Write: address = 0x40020800, size = 4 bytes, value = 0x9, pc=0x8002\n"
    )
    em_path.write_text(
        "[  0.1] Read:  address = 0x40020000, size = 4 bytes, value = 0x0, pc=0x8001\n"
        "[  0.2] Read:  address = 0x40020028, size = 4 bytes, value = 0x0, pc=0x8003\n"
    )
    return str(hw_path), str(em_path)


def _patched_verify(matchmap):
    """Return a fake verify_automata that uses ``matchmap`` to set match status.

    ``matchmap`` is {peripheral_name: not_match_bool}.
    """
    def fake_verify_automata(automata1, automata2, peripheral):
        not_match = matchmap.get(peripheral, False)
        return not_match, _make_diff_obj()
    return fake_verify_automata


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class TestBuildCompositionalModelSourceBlocks:
    """Cover _build_compositional_model_source_blocks."""

    def test_one_block_per_model(self, two_models):
        blocks = _build_compositional_model_source_blocks(
            two_models, show_prompt=True, max_model_chars=120000,
        )
        assert len(blocks) == 2

    def test_each_block_has_label_and_filename(self, two_models):
        blocks = _build_compositional_model_source_blocks(
            two_models, show_prompt=True, max_model_chars=120000,
        )
        joined = "\n".join(blocks)
        assert "## Model: `ADC`  |  File: `model.c`" in joined
        assert "## Model: `DMA_with_DMAMUX1`  |  File: `model2.c`" in joined

    def test_source_truncated_at_max_chars(self, tmp_path):
        big_path = tmp_path / "big.c"
        big_path.write_text("X" * 5000)
        blocks = _build_compositional_model_source_blocks(
            {"BIG": str(big_path)}, show_prompt=True, max_model_chars=100,
        )
        assert "truncated" in blocks[0].lower()
        # Truncation marker is appended after the cap, so block fits within
        # cap + small marker overhead, not the original 5000 bytes.
        assert len(blocks[0]) < 500

    def test_show_prompt_false_hides_source(self, two_models):
        blocks = _build_compositional_model_source_blocks(
            two_models, show_prompt=False, max_model_chars=120000,
        )
        joined = "\n".join(blocks)
        # File body is stubbed out; original source must not appear.
        assert "model source hidden" in joined
        assert "void* adc_init" not in joined
        assert "void* dma_with_dmamux1_init" not in joined


class TestRenderApisAndRules:
    """Cover _render_apis_and_rules — the no_vio lens."""

    def test_default_includes_vio_apis_and_guidance(self):
        rendered = _render_apis_and_rules(
            api_list=qemu_api_list,
            fw_rules="STUB_RULES",
            no_vio=False,
        )
        assert "## Available APIs" in rendered
        assert "STUB_RULES" in rendered
        # VIO APIs must be present in the rendered API block.
        assert "api_i2c_init_bus" in rendered
        assert "api_dma_request_data" in rendered
        # VIO-specific guidance sections must be included.
        assert "Observability" in rendered or "observability" in rendered.lower()
        assert "Inter-Peripheral" in rendered or "inter-peripheral" in rendered.lower()

    def test_no_vio_excludes_vio_apis_and_guidance(self):
        rendered = _render_apis_and_rules(
            api_list=qemu_base_api_list,
            fw_rules="STUB_RULES",
            no_vio=True,
        )
        assert "## Available APIs" in rendered
        # VIO APIs gone.
        assert "api_i2c_init_bus" not in rendered
        assert "api_dma_request_data" not in rendered
        assert "api_pty_fd_gen" not in rendered
        # VIO-specific guidance sections gone too.
        assert "Inter-Peripheral Communication" not in rendered
        assert "Observability & Host I/O" not in rendered
        # The framework rules string the caller passes through is preserved.
        assert "STUB_RULES" in rendered

    def test_api_list_argument_is_respected(self):
        """The caller controls which API list is rendered (base vs full)."""
        full = _render_apis_and_rules(qemu_api_list, "X", no_vio=False)
        base = _render_apis_and_rules(qemu_base_api_list, "X", no_vio=True)
        # Full has VIO; base does not.
        assert "api_i2c_init_bus" in full
        assert "api_i2c_init_bus" not in base


class TestBuildCompositionalDiffBlocks:
    """Cover _build_compositional_diff_blocks."""

    def test_status_label_reflects_match_state(self):
        results = {
            "ADC1": (True, _make_diff_obj()),
            "ADC2": (False, _make_diff_obj()),
        }
        blocks = _build_compositional_diff_blocks(["ADC1", "ADC2"], results)
        joined = "\n".join(blocks)
        assert "## Peripheral: `ADC1`  [MISMATCH]" in joined
        assert "## Peripheral: `ADC2`  [PASSING]" in joined

    def test_passing_peripheral_carries_no_regression_warning(self):
        results = {"ADC2": (False, _make_diff_obj())}
        blocks = _build_compositional_diff_blocks(["ADC2"], results)
        assert "do not regress" in blocks[0]

    def test_mismatch_peripheral_carries_fix_request(self):
        results = {"ADC1": (True, _make_diff_obj())}
        blocks = _build_compositional_diff_blocks(["ADC1"], results)
        assert "trace mismatches that require a fix" in blocks[0]

    def test_no_data_state_does_not_get_polling_instructions(self):
        """The polling-interpretation suffix is only appended when state data exists."""
        results = {"X": (True, _make_diff_obj(diff_state_data="[NO DATA — file not found]"))}
        blocks = _build_compositional_diff_blocks(["X"], results)
        # The polling-interpretation block contains a recognizable phrase from
        # `polling_interpretation_instructions`; ensure it's NOT appended.
        assert "INFINITE POLLING LOOP" not in blocks[0]


class TestReadTraceStripSystick:
    """Cover _read_trace_strip_systick."""

    def test_strips_systick_lines_only(self, tmp_path):
        log = tmp_path / "io.log"
        log.write_text(
            "[ 0.1] Read: address = 0x40020000\n"
            "[ 0.2] Interrupt Taken:  Vector = 0x0000000F\n"
            "[ 0.3] Interrupt Served: Vector = 0x0000000F\n"
            "[ 0.4] Write: address = 0x40020800\n"
        )
        out = _read_trace_strip_systick(str(log))
        assert "0x40020000" in out
        assert "0x40020800" in out
        assert "Vector = 0x0000000F" not in out

    def test_other_irq_vectors_are_kept(self, tmp_path):
        """Only the SysTick (0x0F) bookkeeping is stripped; other IRQs stay."""
        log = tmp_path / "io.log"
        log.write_text(
            "[ 0.1] Interrupt Taken:  Vector = 0x0000001C\n"  # DMA1_Stream1
            "[ 0.2] Interrupt Taken:  Vector = 0x0000000F\n"  # SysTick (drop)
        )
        out = _read_trace_strip_systick(str(log))
        assert "0x0000001C" in out
        assert "0x0000000F" not in out


class TestWritePrompt:
    """Cover _write_prompt."""

    def test_writes_file_and_returns_path(self, tmp_path):
        out = tmp_path / "child"  # nonexistent — should be created
        path = _write_prompt(str(out), "revised_prompt.txt", "hello world")
        assert Path(path).exists()
        assert Path(path).read_text() == "hello world\n"

    def test_creates_parent_dir_if_missing(self, tmp_path):
        deep = tmp_path / "a" / "b" / "c"
        path = _write_prompt(str(deep), "initial_prompt.txt", "x")
        assert Path(path).exists()
        assert Path(path).parent == deep


# ---------------------------------------------------------------------------
# iteration_prompt_gen_multiple_periph: backward-compat (default branch)
# ---------------------------------------------------------------------------

class TestCompositionalDefaultBranch:
    """When no ablation flag is set the function must behave as before the
    refactor: SEARCH/REPLACE strategy, structured per-peripheral diffs,
    revised_prompt.txt, full VIO API list.
    """

    def test_default_writes_revised_prompt_with_search_replace_strategy(
        self, two_models, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        out_dir = tmp_path / "out"
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True, "DMA1": False})):
            path = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(out_dir),
            )
        assert path is not None
        assert Path(path).name == "revised_prompt.txt"
        body = Path(path).read_text()
        assert "SEARCH/REPLACE" in body or "// FILE:" in body
        # Both peripherals' status labels are present.
        assert "[MISMATCH]" in body
        assert "[PASSING]" in body
        # VIO APIs are included by default.
        assert "api_i2c_init_bus" in body
        assert "api_dma_request_data" in body

    def test_all_passing_returns_none(self, two_models, cm_paths, tmp_path):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": False, "DMA1": False})):
            path = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
            )
        assert path is None

    def test_default_embeds_all_model_sources(self, two_models, cm_paths, tmp_path):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True, "DMA1": True})):
            path = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
            )
        body = Path(path).read_text()
        assert "// ADC model" in body
        assert "// DMA model" in body


# ---------------------------------------------------------------------------
# iteration_prompt_gen_multiple_periph: no_verifier branch
# ---------------------------------------------------------------------------

class TestCompositionalNoVerifier:
    """no_verifier in compositional mode → one initial_prompt.txt per --mname,
    each with that model's broken source and no diffs. Joint prompt is the
    single-mname fallback only.
    """

    def test_compositional_returns_dict_of_per_model_paths(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
            model_sources_map=two_model_sources_map,
        )
        assert isinstance(result, dict)
        assert set(result.keys()) == {"ADC", "DMA_with_DMAMUX1"}
        for mname, path in result.items():
            assert Path(path).name == "initial_prompt.txt"
            # Sibling-dir layout: parent is `<out_dir>_<mname>`, not a subdir.
            # This keeps prompts outside the verifier's work_dir so subsequent
            # verifier reruns (which rmtree work_dir) don't wipe them.
            assert Path(path).parent.name == f"out_{mname}"
            assert Path(path).parent.parent == tmp_path

    def test_no_verifier_skips_verify_automata(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata") as mock_v:
            iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_verifier=True,
                model_sources_map=two_model_sources_map,
            )
            assert not mock_v.called

    def test_per_model_prompt_excludes_diff_data(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
            model_sources_map=two_model_sources_map,
        )
        for path in result.values():
            body = Path(path).read_text()
            assert "[MISMATCH]" not in body
            assert "[PASSING]" not in body
            assert "Detected Runtime Loops" not in body

    def test_each_per_model_prompt_includes_only_its_own_source(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        """ADC's prompt must not contain the DMA model source, and vice versa."""
        hw, em = cm_paths
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
            model_sources_map=two_model_sources_map,
        )
        adc_body = Path(result["ADC"]).read_text()
        dma_body = Path(result["DMA_with_DMAMUX1"]).read_text()
        assert "// ADC model" in adc_body
        assert "// DMA model" not in adc_body
        assert "// DMA model" in dma_body
        assert "// ADC model" not in dma_body

    def test_compositional_no_verifier_requires_ms_map(
        self, two_models, cm_paths, tmp_path,
    ):
        """Forgetting --ms-map in compositional --no-verifier is a hard error."""
        hw, em = cm_paths
        with pytest.raises(ValueError, match="model_sources_map"):
            iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_verifier=True,
                model_sources_map=None,
            )

    def test_single_mname_falls_back_to_joint_prompt(self, tmp_path, cm_paths):
        """Single -mname (non-compositional) keeps the original joint behavior:
        a single initial_prompt.txt at out_dir, no per-model splitting."""
        hw, em = cm_paths
        adc_path = tmp_path / "model.c"
        adc_path.write_text("// ADC model\n")
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path={"ADC": str(adc_path)},
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
        )
        assert isinstance(result, str)
        assert Path(result).name == "initial_prompt.txt"
        assert Path(result).parent == tmp_path / "out"


# ---------------------------------------------------------------------------
# iteration_prompt_gen_multiple_periph: no_encoder branch
# ---------------------------------------------------------------------------

class TestCompositionalNoEncoder:
    """no_encoder → revised_prompt.txt with raw HW+EM traces."""

    def test_writes_revised_prompt_with_raw_traces(
        self, two_models, cm_paths, io_logs, tmp_path,
    ):
        hw, em = cm_paths
        hw_log, em_log = io_logs
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=em_log,
        )
        assert Path(path).name == "revised_prompt.txt"
        body = Path(path).read_text()
        assert "## Raw Hardware I/O Trace" in body
        assert "## Raw Emulated Model I/O Trace" in body
        # Specific MMIO addresses from the fake logs survive.
        assert "0x40020000" in body
        assert "0x40020800" in body
        # No per-peripheral structured diff blocks.
        assert "[MISMATCH]" not in body
        assert "[PASSING]" not in body

    def test_no_encoder_skips_verify_automata(
        self, two_models, cm_paths, io_logs, tmp_path,
    ):
        hw, em = cm_paths
        hw_log, _ = io_logs
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata") as mock_v:
            iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1"],
                model_names=["ADC"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_encoder=True,
                hardware_log=hw_log,
            )
            assert not mock_v.called

    def test_no_encoder_strips_systick_from_traces(
        self, two_models, cm_paths, io_logs, tmp_path,
    ):
        hw, em = cm_paths
        hw_log, em_log = io_logs
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=em_log,
        )
        body = Path(path).read_text()
        assert "Vector = 0x0000000F" not in body

    def test_no_encoder_requires_hardware_log(self, two_models, cm_paths, tmp_path):
        hw, em = cm_paths
        with pytest.raises(ValueError, match="hardware_log"):
            iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1"],
                model_names=["ADC"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_encoder=True,
                hardware_log=None,
            )

    def test_no_encoder_caps_emulation_trace_at_2x_hw(
        self, two_models, cm_paths, tmp_path,
    ):
        """A pathological infinite-loop EM trace must be clipped, with a marker."""
        hw_log = tmp_path / "io_hw.log"
        hw_log.write_text("[0.1] Read: address = 0x1\n[0.2] Read: address = 0x2\n")
        em_log = tmp_path / "io_em.log"
        em_log.write_text("\n".join(
            f"[0.{i}] Read: address = 0x{i:x}" for i in range(20)
        ))
        hw, em = cm_paths
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            hardware_log=str(hw_log),
            emulation_log=str(em_log),
        )
        body = Path(path).read_text()
        assert "TRUNCATED" in body
        assert "infinite polling loop" in body.lower()

    def test_no_encoder_emulation_log_optional(
        self, two_models, cm_paths, io_logs, tmp_path,
    ):
        """no_encoder should still produce a prompt without an EM log
        (e.g. when the elder run hasn't been done yet)."""
        hw, em = cm_paths
        hw_log, _ = io_logs
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=None,
        )
        body = Path(path).read_text()
        assert "## Raw Hardware I/O Trace" in body
        # No emulation section when no log was provided.
        assert "## Raw Emulated Model I/O Trace" not in body


# ---------------------------------------------------------------------------
# iteration_prompt_gen_multiple_periph: no_rca branch
# ---------------------------------------------------------------------------

class TestCompositionalNoRca:
    """no_rca in compositional mode → one initial_prompt.txt per --mname,
    each containing only that model's source and only its peripherals' diff
    blocks. Joint prompt is the single-mname fallback only.
    """

    def test_compositional_returns_dict_of_per_model_paths(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True, "DMA1": True})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "ADC2", "ADC12_Common", "DMA1", "DMAMUX1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_rca=True,
                model_sources_map=two_model_sources_map,
            )
        assert isinstance(result, dict)
        assert set(result.keys()) == {"ADC", "DMA_with_DMAMUX1"}

    def test_each_per_model_prompt_has_only_its_own_diffs(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True, "DMA1": True})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "ADC2", "ADC12_Common", "DMA1", "DMAMUX1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_rca=True,
                model_sources_map=two_model_sources_map,
            )
        adc_body = Path(result["ADC"]).read_text()
        dma_body = Path(result["DMA_with_DMAMUX1"]).read_text()
        # ADC prompt has ADC1/ADC2/ADC12_Common diffs only.
        assert "Peripheral: `ADC1`" in adc_body
        assert "Peripheral: `DMA1`" not in adc_body
        # DMA prompt has DMA1/DMAMUX1 diffs only.
        assert "Peripheral: `DMA1`" in dma_body
        assert "Peripheral: `ADC1`" not in dma_body

    def test_each_per_model_prompt_drops_search_replace_strategy(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True, "DMA1": True})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "ADC2", "ADC12_Common", "DMA1", "DMAMUX1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_rca=True,
                model_sources_map=two_model_sources_map,
            )
        for path in result.values():
            body = Path(path).read_text()
            assert "Do not attempt SEARCH/REPLACE" in body

    def test_compositional_no_rca_requires_ms_map(
        self, two_models, cm_paths, tmp_path,
    ):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True})):
            with pytest.raises(ValueError, match="model_sources_map"):
                iteration_prompt_gen_multiple_periph(
                    cm_path_hardware=hw,
                    cm_path_emulation=em,
                    peripherals=["ADC1", "DMA1"],
                    model_names=["ADC", "DMA_with_DMAMUX1"],
                    model_to_path=two_models,
                    out_dir=str(tmp_path / "out"),
                    no_rca=True,
                    model_sources_map=None,
                )

    def test_single_mname_falls_back_to_joint_prompt(self, tmp_path, cm_paths):
        """Single -mname (non-compositional) keeps the original joint
        behavior: one initial_prompt.txt with all diff blocks, no splitting."""
        hw, em = cm_paths
        adc_path = tmp_path / "model.c"
        adc_path.write_text("// ADC model\n")
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1"],
                model_names=["ADC"],
                model_to_path={"ADC": str(adc_path)},
                out_dir=str(tmp_path / "out"),
                no_rca=True,
            )
        assert isinstance(result, str)
        assert Path(result).name == "initial_prompt.txt"

    def test_no_rca_skips_models_with_no_mismatches(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        """If a model's peripherals all pass, no prompt is written for that model."""
        hw, em = cm_paths
        # Only ADC1 mismatches; DMA peripherals all pass.
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "ADC2", "ADC12_Common", "DMA1", "DMAMUX1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_rca=True,
                model_sources_map=two_model_sources_map,
            )
        assert "ADC" in result
        assert "DMA_with_DMAMUX1" not in result

    def test_no_rca_returns_none_when_no_mismatches(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        """If every peripheral passes, no prompts are written and None is returned."""
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({})):
            result = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1", "DMA1"],
                model_names=["ADC", "DMA_with_DMAMUX1"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_rca=True,
                model_sources_map=two_model_sources_map,
            )
        assert result is None


# ---------------------------------------------------------------------------
# no_vio: orthogonal lens — composes with each branch
# ---------------------------------------------------------------------------

class TestCompositionalNoVio:
    """no_vio strips VIO APIs in any branch it composes with."""

    def test_no_vio_with_default_branch(self, two_models, cm_paths, tmp_path):
        hw, em = cm_paths
        with patch("fastdyn.verifier.prompt_gen.verify.verify_automata",
                   side_effect=_patched_verify({"ADC1": True})):
            path = iteration_prompt_gen_multiple_periph(
                cm_path_hardware=hw,
                cm_path_emulation=em,
                peripherals=["ADC1"],
                model_names=["ADC"],
                model_to_path=two_models,
                out_dir=str(tmp_path / "out"),
                no_vio=True,
            )
        body = Path(path).read_text()
        api_section = body.split("## Available APIs")[1].split("```")[1]
        assert "api_i2c_init_bus" not in api_section
        assert "api_dma_register_stream" not in api_section
        assert "qemu_plugin_raise_irq" in api_section  # base API survives

    def test_no_vio_with_no_encoder(self, two_models, cm_paths, io_logs, tmp_path):
        hw, em = cm_paths
        hw_log, _ = io_logs
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            no_vio=True,
            hardware_log=hw_log,
        )
        body = Path(path).read_text()
        api_section = body.split("## Available APIs")[1].split("```")[1]
        assert "api_dma_request_data" not in api_section
        assert "## Raw Hardware I/O Trace" in body  # no_encoder still in effect

    def test_no_vio_with_no_verifier(
        self, two_models, two_model_sources_map, cm_paths, tmp_path,
    ):
        """no_vio + no_verifier in compositional mode: per-model split with
        VIO APIs stripped from each per-model prompt."""
        hw, em = cm_paths
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
            no_vio=True,
            model_sources_map=two_model_sources_map,
        )
        assert isinstance(result, dict)
        for path in result.values():
            body = Path(path).read_text()
            api_section = body.split("## Available APIs")[1].split("```")[1]
            assert "api_pty_fd_gen" not in api_section
            assert "[MISMATCH]" not in body
            assert Path(path).name == "initial_prompt.txt"


# ---------------------------------------------------------------------------
# Branch precedence: ablation flags are mutually-exclusive correction
# strategies. The function evaluates them in a fixed order
# (no_verifier → no_encoder → no_rca → default). Verify that contract.
# ---------------------------------------------------------------------------

class TestCompositionalBranchPrecedence:

    def test_no_verifier_wins_over_no_encoder_and_no_rca(
        self, two_models, two_model_sources_map, cm_paths, io_logs, tmp_path,
    ):
        hw, em = cm_paths
        hw_log, _ = io_logs
        result = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1", "DMA1"],
            model_names=["ADC", "DMA_with_DMAMUX1"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_verifier=True,
            no_encoder=True,
            no_rca=True,
            hardware_log=hw_log,
            model_sources_map=two_model_sources_map,
        )
        # no_verifier wins → per-model dict with no diff or raw-trace data.
        assert isinstance(result, dict)
        for path in result.values():
            body = Path(path).read_text()
            assert Path(path).name == "initial_prompt.txt"
            assert "## Raw Hardware I/O Trace" not in body
            assert "[MISMATCH]" not in body

    def test_no_encoder_wins_over_no_rca(
        self, two_models, cm_paths, io_logs, tmp_path,
    ):
        hw, em = cm_paths
        hw_log, _ = io_logs
        path = iteration_prompt_gen_multiple_periph(
            cm_path_hardware=hw,
            cm_path_emulation=em,
            peripherals=["ADC1"],
            model_names=["ADC"],
            model_to_path=two_models,
            out_dir=str(tmp_path / "out"),
            no_encoder=True,
            no_rca=True,
            hardware_log=hw_log,
        )
        # Wins → revised_prompt.txt with raw HW trace, no diff blocks.
        assert Path(path).name == "revised_prompt.txt"
        body = Path(path).read_text()
        assert "## Raw Hardware I/O Trace" in body
        assert "[MISMATCH]" not in body
