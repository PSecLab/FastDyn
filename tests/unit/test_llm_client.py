"""Tests for fastdyn.llm.llm_client module.

All tests use mocks -- no real API calls are made.
"""

import os
import pytest
from unittest.mock import patch, MagicMock

from fastdyn.llm.llm_client import (
    LLMClient,
    LLMClientError,
    load_api_key,
    CONVERSATION_RESET_LINE,
    _parse_env_file,
)


class TestLoadApiKey:
    """Tests for load_api_key()."""

    def test_loads_from_environment(self):
        with patch.dict(os.environ, {"OPENAI_API_KEY": "sk-test-env-key"}):
            key = load_api_key()
            assert key == "sk-test-env-key"

    def test_loads_from_env_file(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('OPENAI_API_KEY=sk-test-file-key\n')

        with patch.dict(os.environ, {}, clear=True):
            # Remove OPENAI_API_KEY from env if it exists
            os.environ.pop("OPENAI_API_KEY", None)
    def test_raises_when_no_key_found(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('OTHER_KEY=some-value\n')

        # Mock HOME so ~/.fastdyn.env resolves to a clean directory
        dummy_home = tmp_path / "home"
        dummy_home.mkdir()

        with patch.dict(os.environ, {"HOME": str(dummy_home)}, clear=True):
            os.environ.pop("OPENAI_API_KEY", None)
            with pytest.raises(LLMClientError, match="API key not found"):
                load_api_key(str(env_file))

    def test_env_var_takes_priority_over_file(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('OPENAI_API_KEY=sk-file-key\n')

        with patch.dict(os.environ, {"OPENAI_API_KEY": "sk-env-key"}):
            key = load_api_key(str(env_file))
            assert key == "sk-env-key"


class TestParseEnvFile:
    """Tests for _parse_env_file()."""

    def test_simple_key_value(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('MY_KEY=my_value\n')
        assert _parse_env_file(env_file, "MY_KEY") == "my_value"

    def test_quoted_value(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('MY_KEY="my_value"\n')
        assert _parse_env_file(env_file, "MY_KEY") == "my_value"

    def test_single_quoted_value(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text("MY_KEY='my_value'\n")
        assert _parse_env_file(env_file, "MY_KEY") == "my_value"

    def test_comments_ignored(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('# This is a comment\nMY_KEY=value\n')
        assert _parse_env_file(env_file, "MY_KEY") == "value"

    def test_blank_lines_ignored(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('\n\nMY_KEY=value\n\n')
        assert _parse_env_file(env_file, "MY_KEY") == "value"

    def test_missing_key(self, tmp_path):
        env_file = tmp_path / ".env"
        env_file.write_text('OTHER_KEY=value\n')
        assert _parse_env_file(env_file, "MY_KEY") == ""

    def test_nonexistent_file(self, tmp_path):
        env_file = tmp_path / "nonexistent.env"
        assert _parse_env_file(env_file, "MY_KEY") == ""


class TestLLMClientStripResetLine:
    """Tests for the conversation reset line handling."""

    def test_strip_reset_line_removes_line(self):
        prompt = (
            "Take this prompt independent from previous prompt history.\n"
            "\n"
            "You are an expert.\n"
        )
        result = LLMClient._strip_reset_line(prompt)
        assert CONVERSATION_RESET_LINE not in result
        assert "You are an expert." in result

    def test_strip_reset_line_keeps_other_content(self):
        prompt = (
            "Take this prompt independent from previous prompt history.\n"
            "\n"
            "Line one.\n"
            "Line two.\n"
        )
        result = LLMClient._strip_reset_line(prompt)
        assert "Line one." in result
        assert "Line two." in result

    def test_strip_reset_line_no_match(self):
        prompt = "This prompt has no reset line.\nJust normal content.\n"
        result = LLMClient._strip_reset_line(prompt)
        assert result == prompt


class TestLLMClientInit:
    """Tests for LLMClient initialization."""

    @patch("fastdyn.llm.llm_client.openai", create=True)
    def test_init_creates_client(self, mock_openai_module):
        # Mock the import at module level
        with patch.dict("sys.modules", {"openai": mock_openai_module}):
            client = LLMClient(api_key="sk-test", model="gpt-4o", temperature=0.5)
            mock_openai_module.OpenAI.assert_called_once_with(api_key="sk-test")
