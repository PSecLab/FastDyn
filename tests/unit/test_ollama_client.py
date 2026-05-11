"""Tests for fastdyn.llm.ollama_client.

All tests use mocks -- no real Ollama server is contacted.
"""

import json
import socket
import urllib.error
from unittest.mock import patch

import pytest

from fastdyn.llm.llm_client import LLMCallMetrics, LLMClientError
from fastdyn.llm.ollama_client import OllamaClient


class _FakeHTTPResponse:
    def __init__(self, payload):
        self._payload = payload

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def read(self):
        return json.dumps(self._payload).encode("utf-8")


def _capture_urlopen(payload):
    captured = {}

    def fake_urlopen(request, timeout):
        captured["request"] = request
        captured["timeout"] = timeout
        captured["body"] = json.loads(request.data.decode("utf-8"))
        return _FakeHTTPResponse(payload)

    return captured, fake_urlopen


def test_send_prompt_posts_non_streaming_chat_request():
    payload = {
        "model": "qwen3-coder-next",
        "message": {"role": "assistant", "content": "```c\nint x;\n```"},
        "prompt_eval_count": 11,
        "eval_count": 7,
    }
    captured, fake_urlopen = _capture_urlopen(payload)

    with patch("fastdyn.llm.ollama_client.urllib.request.urlopen", fake_urlopen):
        client = OllamaClient(
            model="qwen3-coder-next",
            base_url="http://127.0.0.1:11434/",
            temperature=0.1,
            num_ctx=262144,
            timeout=123,
        )
        content, metrics = client.send_prompt("Generate C.")

    assert content == "```c\nint x;\n```"
    assert captured["request"].full_url == "http://127.0.0.1:11434/api/chat"
    assert captured["timeout"] == 123
    assert captured["body"]["model"] == "qwen3-coder-next"
    assert captured["body"]["stream"] is False
    assert captured["body"]["messages"] == [{"role": "user", "content": "Generate C."}]
    assert captured["body"]["options"] == {"temperature": 0.1, "num_ctx": 262144}
    assert isinstance(metrics, LLMCallMetrics)
    assert metrics.prompt_tokens == 11
    assert metrics.completion_tokens == 7
    assert metrics.total_tokens == 18


def test_followup_prompt_preserves_chat_history_shape():
    payload = {
        "model": "qwen3-coder-next",
        "message": {"role": "assistant", "content": "fixed"},
    }
    captured, fake_urlopen = _capture_urlopen(payload)

    with patch("fastdyn.llm.ollama_client.urllib.request.urlopen", fake_urlopen):
        client = OllamaClient(model="qwen3-coder-next")
        content, _ = client.send_followup_prompt(
            original_prompt="original",
            previous_response="bad response",
            error_context="compile failed",
        )

    assert content == "fixed"
    messages = captured["body"]["messages"]
    assert [m["role"] for m in messages] == ["user", "assistant", "user"]
    assert messages[0]["content"] == "original"
    assert messages[1]["content"] == "bad response"
    assert "compile failed" in messages[2]["content"]


def test_connection_error_mentions_tags_smoke_test():
    def fake_urlopen(request, timeout):
        raise urllib.error.URLError(socket.timeout("timed out"))

    with patch("fastdyn.llm.ollama_client.urllib.request.urlopen", fake_urlopen):
        client = OllamaClient(model="qwen3-coder-next", base_url="http://127.0.0.1:11434")
        with pytest.raises(LLMClientError, match="curl http://127.0.0.1:11434/api/tags"):
            client.send_prompt("Generate C.")


def test_empty_ollama_response_is_error():
    payload = {"model": "qwen3-coder-next", "message": {"role": "assistant"}}
    _, fake_urlopen = _capture_urlopen(payload)

    with patch("fastdyn.llm.ollama_client.urllib.request.urlopen", fake_urlopen):
        client = OllamaClient(model="qwen3-coder-next")
        with pytest.raises(LLMClientError, match="empty response"):
            client.send_prompt("Generate C.")
