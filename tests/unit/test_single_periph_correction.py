"""Tests for the single-peripheral verifier correction path.

Covers ``iteration_prompt_gen`` (in prompt_gen.py) — the function that
the verifier calls for single-peripheral runs (one ``-mname`` argument).
Mirrors the compositional test file's structure:

- Default branch (no ablation flags) — backward compatibility.
- ``no_encoder`` branch — raw HW/EM traces in place of encoder diffs.
- ``no_rca`` branch — diffs shown, full regeneration requested, no
  SEARCH/REPLACE strategy. Includes a regression check for the
  ``effective_model_name → peripheral_name`` skeleton fix.
- ``no_vio`` lens — composes with each branch above.

All tests use mock ``diff_obj`` instances and tmp model files; no real
verifier is run.
"""

from pathlib import Path
from types import SimpleNamespace

import pytest

from fastdyn.verifier.prompt_gen import (
    iteration_prompt_gen,
    qemu_api_list,
    qemu_base_api_list,
)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

def _make_diff_obj(
    platform_name="STM32H753x",
    diff_init_data="[init]",
    diff_loop_pattern_data="[loop]",
    rare_transitions_data="[rare]",
    diff_state_data="[state]",
    diff_entropy_data="[entropy]",
    diff_runtime_trace="[runtime]",
    isr_analysis_data="[isr]",
    svd_bitfields_data="[bitfields]",
):
    """Fake diff object matching what ``verify.verify_automata`` returns."""
    return SimpleNamespace(
        platform_name=platform_name,
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
def model_file(tmp_path):
    """A small C model source file under correction."""
    path = tmp_path / "uart3_model.c"
    path.write_text(
        "// UART3 model\n"
        "void* uart3_init(ConfigSection* model_info) { return 0; }\n"
    )
    return str(path)


@pytest.fixture
def io_logs(tmp_path):
    """Raw HW + EM io.logs with mixed MMIO and SysTick lines."""
    hw_path = tmp_path / "io_hw.log"
    em_path = tmp_path / "io_em.log"
    hw_path.write_text(
        "[ 0.1] Read:  address = 0x40004800, size = 4 bytes, value = 0x0, pc=0x8001\n"
        "[ 0.2] Interrupt Taken: Vector = 0x0000000F\n"
        "[ 0.3] Write: address = 0x40004808, size = 4 bytes, value = 0x1, pc=0x8002\n"
    )
    em_path.write_text(
        "[ 0.1] Read:  address = 0x40004800, size = 4 bytes, value = 0x0, pc=0x8001\n"
        "[ 0.2] Read:  address = 0x40004808, size = 4 bytes, value = 0x0, pc=0x8003\n"
    )
    return str(hw_path), str(em_path)


# ---------------------------------------------------------------------------
# Default (no ablation) — backward compatibility
# ---------------------------------------------------------------------------

class TestSinglePeriphDefault:
    """Default branch: revised_prompt.txt with SEARCH/REPLACE strategy.

    These tests pin the function's pre-refactor behavior so future edits
    can't silently regress the canonical correction prompt.
    """

    def test_writes_revised_prompt(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
        )
        assert Path(path).name == "revised_prompt.txt"

    def test_default_emits_search_replace_strategy(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
        )
        body = Path(path).read_text()
        assert "// FILE:" in body
        assert "SEARCH" in body or "search" in body.lower()

    def test_default_embeds_broken_model_source(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
        )
        body = Path(path).read_text()
        assert "// UART3 model" in body
        assert "uart3_init" in body

    def test_default_includes_encoder_diff_sections(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
        )
        body = Path(path).read_text()
        # The structured encoder analysis sections must be present.
        assert "Initialization Sequence" in body
        assert "Detected Runtime Loops" in body
        assert "Register Entropy Analysis" in body
        assert "ISR Analysis" in body

    def test_default_has_full_vio_apis(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
        )
        body = Path(path).read_text()
        assert "api_i2c_init_bus" in body
        assert "api_pty_fd_gen" in body
        assert "api_dma_request_data" in body  # new API present

    def test_max_model_chars_truncates_source(self, tmp_path):
        big = tmp_path / "big_model.c"
        big.write_text("X" * 5000)
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="P",
            out_dir=str(tmp_path / "out"),
            device_model_path=str(big),
            max_model_chars=200,
        )
        body = Path(path).read_text()
        assert "truncated" in body.lower()


# ---------------------------------------------------------------------------
# no_encoder branch
# ---------------------------------------------------------------------------

class TestSinglePeriphNoEncoder:
    """no_encoder: raw HW (and optionally EM) trace in place of encoder analysis.
    SEARCH/REPLACE strategy is preserved.
    """

    def test_writes_revised_prompt(self, model_file, io_logs, tmp_path):
        hw_log, em_log = io_logs
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=em_log,
        )
        assert Path(path).name == "revised_prompt.txt"

    def test_no_encoder_includes_raw_hw_and_em_traces(
        self, model_file, io_logs, tmp_path,
    ):
        hw_log, em_log = io_logs
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=em_log,
        )
        body = Path(path).read_text()
        # Both raw traces present.
        assert "## Raw Hardware I/O Trace" in body
        assert "## Raw Emulated Model I/O Trace" in body
        # Specific MMIO addresses survive.
        assert "0x40004800" in body
        assert "0x40004808" in body
        # Encoder-derived sections must be replaced, not added on top.
        assert "Detected Runtime Loops" not in body
        assert "Register Entropy Analysis" not in body

    def test_no_encoder_strips_systick(self, model_file, io_logs, tmp_path):
        hw_log, em_log = io_logs
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=em_log,
        )
        body = Path(path).read_text()
        assert "Vector = 0x0000000F" not in body

    def test_no_encoder_emulation_log_optional(self, model_file, io_logs, tmp_path):
        """When emulation hasn't run yet, the prompt still generates with HW only."""
        hw_log, _ = io_logs
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_encoder=True,
            hardware_log=hw_log,
            emulation_log=None,
        )
        body = Path(path).read_text()
        assert "## Raw Hardware I/O Trace" in body
        assert "## Raw Emulated Model I/O Trace" not in body


# ---------------------------------------------------------------------------
# no_rca branch — full regeneration, diffs shown, no SEARCH/REPLACE
# ---------------------------------------------------------------------------

class TestSinglePeriphNoRca:
    """no_rca: initial_prompt.txt with structured diffs and a regen ask.

    Includes regression coverage for the ``effective_model_name`` fix —
    when this function was inadvertently using an undefined variable, the
    branch raised NameError at runtime.
    """

    def test_writes_initial_prompt_not_revised(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_rca=True,
        )
        assert Path(path).name == "initial_prompt.txt"

    def test_no_rca_drops_search_replace_strategy(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_rca=True,
        )
        body = Path(path).read_text()
        # The output should ask for full code, not patches.
        assert "C Device Model Source Code" in body
        # Skeleton should be present so the LLM mirrors it.
        assert "// Device Model for USART3" in body

    def test_no_rca_skeleton_uses_peripheral_name_not_undefined_var(
        self, model_file, tmp_path,
    ):
        """Regression: the skeleton template must use ``peripheral_name``, not
        an undefined ``effective_model_name`` (which would raise NameError).
        Calling the function without exception is itself the test, but assert
        on the rendered output too so a future replace_all-style edit can't
        re-break this silently.
        """
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_rca=True,
        )
        body = Path(path).read_text()
        assert "usart3_read" in body
        assert "usart3_write" in body
        assert "usart3_init" in body
        assert "USART3State" in body
        # The undefined symbol must not have leaked into the rendered prompt.
        assert "effective_model_name" not in body

    def test_no_rca_includes_diff_data(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(diff_init_data="WRITE_INIT_X"),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_rca=True,
        )
        body = Path(path).read_text()
        # Verifier diff content reaches the prompt.
        assert "WRITE_INIT_X" in body


# ---------------------------------------------------------------------------
# no_vio lens — composes with default, no_encoder, no_rca branches
# ---------------------------------------------------------------------------

class TestSinglePeriphNoVio:
    """no_vio strips VIO APIs in any branch it composes with."""

    def _api_section(self, prompt):
        api_start = prompt.index("## Available APIs")
        api_end = prompt.index("```", prompt.index("```", api_start) + 3) + 3
        return prompt[api_start:api_end]

    def test_no_vio_with_default_branch(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_vio=True,
        )
        api = self._api_section(Path(path).read_text())
        assert "api_i2c_init_bus" not in api
        assert "api_dma_register_stream" not in api
        assert "api_pty_fd_gen" not in api
        assert "qemu_plugin_raise_irq" in api  # base API survives

    def test_no_vio_with_no_encoder(self, model_file, io_logs, tmp_path):
        hw_log, _ = io_logs
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_encoder=True,
            no_vio=True,
            hardware_log=hw_log,
        )
        body = Path(path).read_text()
        api = self._api_section(body)
        assert "api_pty_fd_gen" not in api
        assert "## Raw Hardware I/O Trace" in body  # no_encoder still in effect

    def test_no_vio_with_no_rca(self, model_file, tmp_path):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            no_rca=True,
            no_vio=True,
        )
        body = Path(path).read_text()
        api = self._api_section(body)
        assert "api_i2c_init_bus" not in api
        # Both flags remain in effect.
        assert Path(path).name == "initial_prompt.txt"


# ---------------------------------------------------------------------------
# show_prompt=False is a debug knob; it should warn (and gut the source)
# but still produce a valid prompt.
# ---------------------------------------------------------------------------

class TestShowPromptFalse:

    def test_show_prompt_false_hides_source_but_writes_prompt(
        self, model_file, tmp_path,
    ):
        path = iteration_prompt_gen(
            diff_obj=_make_diff_obj(),
            peripheral="USART3",
            out_dir=str(tmp_path / "out"),
            device_model_path=model_file,
            show_prompt=False,
        )
        # File still written (just with hidden source).
        assert Path(path).exists()
        body = Path(path).read_text()
        # Original source must not appear.
        assert "// UART3 model" not in body
        assert "uart3_init" in body or "USART3" in body  # skeleton + headers
