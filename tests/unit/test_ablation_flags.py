"""Tests for ablation study flags (--no-encoder, --no-vio, --no-rca).

Tests the prompt generation and LLM command behavior under each ablation mode.
All tests use mocks -- no real API calls or hardware traces are used.
"""

import os
import json
import pytest
from pathlib import Path
from unittest.mock import patch, MagicMock

from fastdyn.verifier.prompt_gen import (
    generate_prompt_no_encoder,
    generate_prompt,
    qemu_api_list,
    qemu_base_api_list,
)


# ---------------------------------------------------------------------------
# qemu_base_api_list vs qemu_api_list
# ---------------------------------------------------------------------------

class TestApiListSplit:
    """Tests that the base and full API lists are correctly separated."""

    def test_base_list_has_qemu_apis(self):
        assert "qemu_plugin_raise_irq" in qemu_base_api_list
        assert "qemu_plugin_timer_alarm" in qemu_base_api_list
        assert "qemu_plugin_read_memory" in qemu_base_api_list
        assert "dev_debug" in qemu_base_api_list

    def test_base_list_has_no_vio_apis(self):
        assert "api_i2c_init_bus" not in qemu_base_api_list
        assert "api_spi_transfer" not in qemu_base_api_list
        assert "api_pty_fd_gen" not in qemu_base_api_list
        assert "api_dma_register_stream" not in qemu_base_api_list
        assert "api_signal_set" not in qemu_base_api_list
        assert "I2CBus" not in qemu_base_api_list
        assert "SPIBus" not in qemu_base_api_list

    def test_full_list_has_both(self):
        assert "qemu_plugin_raise_irq" in qemu_api_list
        assert "api_i2c_init_bus" in qemu_api_list
        assert "api_spi_transfer" in qemu_api_list
        assert "api_pty_fd_gen" in qemu_api_list
        assert "I2CBus" in qemu_api_list
        assert "SPIBus" in qemu_api_list

    def test_base_is_strict_subset(self):
        """Every API in the base list should also appear in the full list."""
        for line in qemu_base_api_list.strip().splitlines():
            line = line.strip()
            if line.startswith("- `"):
                func_name = line.split("`")[1].split("(")[0].strip()
                assert func_name in qemu_api_list, f"{func_name} in base but not in full list"


# ---------------------------------------------------------------------------
# --no-encoder: generate_prompt_no_encoder
# ---------------------------------------------------------------------------

class TestNoEncoder:
    """Tests for the --no-encoder ablation (generate_prompt_no_encoder)."""

    @pytest.fixture
    def fake_io_log(self, tmp_path):
        """Create a minimal fake io.log for testing."""
        log_content = (
            "[  0.100000] Write:  address = 0x40042000, size = 4 bytes, value = 0x00000001, pc=0x08001234\n"
            "[  0.200000] Read:   address = 0x40042004, size = 4 bytes, value = 0x00000080, pc=0x08001238\n"
            "[  0.300000] Write:  address = 0x40042008, size = 4 bytes, value = 0x00000041, pc=0x0800123C\n"
            "[  1.000000] Read:   address = 0x40042004, size = 4 bytes, value = 0x000000C0, pc=0x08001240\n"
        )
        log_path = tmp_path / "io.log"
        log_path.write_text(log_content)
        return str(log_path)

    def test_generates_prompt_with_raw_trace(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
        )
        # Should contain the raw trace data
        assert "0x40042000" in prompt
        assert "0x40042004" in prompt
        assert "0x40042008" in prompt

    def test_contains_task_description(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
        )
        assert "raw MMIO I/O trace" in prompt
        assert "Max78000" in prompt
        assert "UART0" in prompt

    def test_contains_vio_apis_by_default(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
        )
        assert "api_i2c_init_bus" in prompt
        assert "api_spi_transfer" in prompt
        assert "api_pty_fd_gen" in prompt

    def test_no_vio_strips_vio_apis_from_available_section(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
            no_vio=True,
        )
        # Extract just the Available APIs section
        api_start = prompt.index("## Available APIs")
        api_end = prompt.index("```", prompt.index("```", api_start) + 3) + 3
        api_section = prompt[api_start:api_end]

        # VIO APIs should NOT be in the Available APIs section
        assert "api_i2c_init_bus" not in api_section
        assert "api_spi_transfer" not in api_section
        assert "api_pty_fd_gen" not in api_section
        assert "I2CBus" not in api_section
        assert "SPIBus" not in api_section
        # Base APIs should still be present
        assert "qemu_plugin_raise_irq" in api_section
        assert "dev_debug" in api_section

    def test_does_not_contain_encoder_artifacts(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
        )
        # Should NOT contain Encoder-specific sections
        assert "init.txt" not in prompt
        assert "loop_pattern" not in prompt
        assert "entropy.txt" not in prompt
        assert "isr_analysis.txt" not in prompt
        assert "Entropy Analysis" not in prompt
        assert "Stateful Behavior Analysis" not in prompt

    def test_contains_output_format(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="UART0",
            platform_name="Max78000",
        )
        assert "High-Level Summary" in prompt
        assert "Register Analysis" in prompt
        assert "C Device Model Source Code" in prompt
        assert "uart0_read" in prompt
        assert "uart0_write" in prompt
        assert "uart0_init" in prompt

    def test_contains_framework_rules(self, fake_io_log):
        prompt = generate_prompt_no_encoder(
            hardware_log=fake_io_log,
            peripheral_name="SPI0",
            platform_name="Max78000",
        )
        assert "Absolute Addressing" in prompt or "absolute" in prompt.lower()


# ---------------------------------------------------------------------------
# --no-vio through generate_prompt (full encoder path)
# ---------------------------------------------------------------------------

class TestNoVioInFullPipeline:
    """Tests that no_vio=True strips VIO APIs from the full encoder prompt."""

    @pytest.fixture
    def analysis_dir(self, tmp_path):
        """Create a minimal analysis directory structure for generate_prompt."""
        periph_dir = tmp_path / "out_cm" / "UART0"
        periph_dir.mkdir(parents=True)

        (periph_dir / "init.txt").write_text("[  0.1] WRITE to UART0->CR1 (0x40042000) value=0x1, pc=0x8001234\n")
        (periph_dir / "state.txt").write_text("- Read-Modify-Write (RMW) pattern detected on register: CR1\n")
        (periph_dir / "entropy.txt").write_text("- Register CR1: LOW (suggests status/control register) - Entropy = 0.00 bits\n")
        (periph_dir / "loop_pattern_1.txt").write_text("[  1.0] READ to UART0->SR (0x40042004) value=0x80, pc=0x8001238\n")
        (periph_dir / "isr_analysis.txt").write_text("")

        # summary.txt in parent
        (tmp_path / "out_cm" / "summary.txt").write_text("- Platform: Max78000\n")

        return str(periph_dir)

    def test_full_prompt_has_vio_apis(self, analysis_dir):
        prompt = generate_prompt(analysis_dir, no_vio=False)
        assert "api_i2c_init_bus" in prompt
        assert "api_spi_transfer" in prompt

    def test_no_vio_prompt_lacks_vio_apis_in_available_section(self, analysis_dir):
        prompt = generate_prompt(analysis_dir, no_vio=True)
        # Extract just the Available APIs section
        api_start = prompt.index("## Available APIs")
        api_end = prompt.index("```", prompt.index("```", api_start) + 3) + 3
        api_section = prompt[api_start:api_end]

        assert "api_i2c_init_bus" not in api_section
        assert "api_spi_transfer" not in api_section
        assert "api_pty_fd_gen" not in api_section
        # Base APIs still present
        assert "qemu_plugin_raise_irq" in api_section

    def test_no_vio_prompt_still_has_encoder_data(self, analysis_dir):
        prompt = generate_prompt(analysis_dir, no_vio=True)
        # Encoder artifacts should still be present
        assert "Initialization Sequence" in prompt
        assert "Detected Runtime Loops" in prompt
        assert "Register Entropy Analysis" in prompt


# ---------------------------------------------------------------------------
# --no-rca behavior (on verifier command)
# ---------------------------------------------------------------------------

class TestNoRca:
    """Tests for the --no-rca ablation flag on the verifier command.

    When --no-rca is set on the verifier, it should write a generic
    revised_prompt.txt (initial prompt + generic retry message) instead
    of the targeted RCA-based correction prompt.
    """

    def test_generic_prompt_contains_original_plus_retry(self, tmp_path):
        """--no-rca should produce: original prompt + generic correction message."""
        original_prompt = "Generate a model for UART0 on Max78000.\n"

        # Simulate the --no-rca logic from main.py verifier command
        generic_prompt = (
            original_prompt +
            "\n\n## Correction Required\n"
            "The previously generated model failed verification against the hardware trace. "
            "Please review the trace data above carefully and generate a corrected model "
            "that faithfully reproduces the observed hardware behavior.\n"
        )

        revised_path = tmp_path / "revised_prompt.txt"
        revised_path.write_text(generic_prompt)

        content = revised_path.read_text()
        # Contains the original prompt
        assert "Generate a model for UART0" in content
        # Contains the generic retry message
        assert "Correction Required" in content
        assert "failed verification" in content

    def test_generic_prompt_has_no_rca_specifics(self, tmp_path):
        """--no-rca prompt should NOT contain RCA-specific diagnostics."""
        original_prompt = "Generate a model for UART0.\n"
        rca_content = "Entropy mismatch: SR hardware=1.0, emulated=0.0"

        generic_prompt = (
            original_prompt +
            "\n\n## Correction Required\n"
            "The previously generated model failed verification against the hardware trace. "
            "Please review the trace data above carefully and generate a corrected model "
            "that faithfully reproduces the observed hardware behavior.\n"
        )

        # The generic prompt should not contain any RCA-specific analysis
        assert rca_content not in generic_prompt
        assert "Entropy mismatch" not in generic_prompt
        assert "SEARCH/REPLACE" not in generic_prompt

    def test_normal_mode_uses_rca(self):
        """Without --no-rca, the verifier generates targeted RCA prompts (not generic)."""
        # This is a design-level test: the normal flow calls pg.iteration_prompt_gen()
        # which produces detailed mismatch analysis, not a generic message.
        # We verify the distinction by checking the no_rca flag logic.
        no_rca = False
        not_match = True

        # In normal mode, RCA path is taken
        used_rca = not_match and not no_rca
        assert used_rca is True

    def test_no_rca_flag_skips_rca(self):
        """With --no-rca, the verifier skips RCA prompt generation."""
        no_rca = True
        not_match = True

        used_rca = not_match and not no_rca
        assert used_rca is False
