"""CLI smoke tests for LLM provider selection."""

from unittest.mock import MagicMock, patch

from click.testing import CliRunner

from fastdyn.llm.llm_client import LLMCallMetrics
from fastdyn.main import cli


def test_llm_cli_ollama_provider_does_not_require_openai_key(tmp_path, monkeypatch):
    work_dir = tmp_path / "work"
    work_dir.mkdir()
    (work_dir / "initial_prompt.txt").write_text("Generate a model.", encoding="utf-8")
    output_path = tmp_path / "model.c"

    fake_client = MagicMock()
    fake_client.send_prompt.return_value = (
        "```c\nint generated_model;\n```",
        LLMCallMetrics(model="qwen3-coder-next"),
    )

    monkeypatch.delenv("OPENAI_API_KEY", raising=False)

    with patch("fastdyn.llm.ollama_client.OllamaClient", return_value=fake_client) as client_cls:
        result = CliRunner().invoke(
            cli,
            [
                "llm",
                "-d",
                str(work_dir),
                "-o",
                str(output_path),
                "--model",
                "qwen3-coder-next",
                "--model-provider",
                "ollama",
                "--ollama-url",
                "http://127.0.0.1:11434",
            ],
        )

    assert result.exit_code == 0, result.output
    assert "int generated_model;" in output_path.read_text(encoding="utf-8")
    client_cls.assert_called_once()
    _, kwargs = client_cls.call_args
    assert kwargs["model"] == "qwen3-coder-next"
    assert kwargs["base_url"] == "http://127.0.0.1:11434"
    assert kwargs["temperature"] == 0.1
    assert kwargs["num_ctx"] == 262144
    fake_client.send_prompt.assert_called_once()
