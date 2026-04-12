"""Tests for LLM evaluation metrics (--evaluate flag).

Tests the LLMCallMetrics dataclass, metrics extraction from mocked API
responses, and metrics JSONL writing in the main.py llm command loop.
All tests use mocks -- no real API calls are made.
"""

import json
import os
import pytest
from unittest.mock import patch, MagicMock, PropertyMock
from dataclasses import asdict

from fastdyn.llm.llm_client import (
    LLMClient,
    LLMClientError,
    LLMCallMetrics,
)


# ---------------------------------------------------------------------------
# LLMCallMetrics dataclass
# ---------------------------------------------------------------------------

class TestLLMCallMetrics:
    """Tests for the LLMCallMetrics dataclass."""

    def test_default_values(self):
        m = LLMCallMetrics()
        assert m.model == ""
        assert m.call_id == ""
        assert m.prompt_tokens == 0
        assert m.completion_tokens == 0
        assert m.reasoning_tokens is None
        assert m.total_tokens == 0
        assert m.latency_seconds == 0.0
        assert m.prompt_chars == 0
        assert m.response_chars == 0
        assert m.timestamp == 0.0

    def test_to_dict(self):
        m = LLMCallMetrics(
            model="gpt-5.4",
            call_id="chatcmpl-abc123",
            prompt_tokens=500,
            completion_tokens=200,
            reasoning_tokens=150,
            total_tokens=700,
            latency_seconds=3.456,
            prompt_chars=2000,
            response_chars=800,
            timestamp=1712345678.0,
        )
        d = m.to_dict()
        assert isinstance(d, dict)
        assert d["model"] == "gpt-5.4"
        assert d["call_id"] == "chatcmpl-abc123"
        assert d["prompt_tokens"] == 500
        assert d["completion_tokens"] == 200
        assert d["reasoning_tokens"] == 150
        assert d["total_tokens"] == 700
        assert d["latency_seconds"] == 3.456
        assert d["prompt_chars"] == 2000
        assert d["response_chars"] == 800

    def test_to_dict_none_reasoning(self):
        m = LLMCallMetrics(prompt_tokens=100, completion_tokens=50, total_tokens=150)
        d = m.to_dict()
        assert d["reasoning_tokens"] is None

    def test_to_dict_is_json_serializable(self):
        m = LLMCallMetrics(
            model="gpt-5.4",
            prompt_tokens=100,
            completion_tokens=50,
            total_tokens=150,
            latency_seconds=1.5,
        )
        serialized = json.dumps(m.to_dict())
        deserialized = json.loads(serialized)
        assert deserialized["model"] == "gpt-5.4"
        assert deserialized["prompt_tokens"] == 100


# ---------------------------------------------------------------------------
# _extract_metrics
# ---------------------------------------------------------------------------

def _make_mock_response(
    prompt_tokens=100,
    completion_tokens=50,
    total_tokens=150,
    reasoning_tokens=None,
    model="gpt-5.4",
    call_id="chatcmpl-test123",
    created=1712345678,
    content="Generated model code here.",
):
    """Build a mock OpenAI ChatCompletion response."""
    response = MagicMock()
    response.model = model
    response.id = call_id
    response.created = created
    response.choices = [MagicMock()]
    response.choices[0].message.content = content

    usage = MagicMock()
    usage.prompt_tokens = prompt_tokens
    usage.completion_tokens = completion_tokens
    usage.total_tokens = total_tokens

    if reasoning_tokens is not None:
        details = MagicMock()
        details.reasoning_tokens = reasoning_tokens
        usage.completion_tokens_details = details
    else:
        usage.completion_tokens_details = None

    response.usage = usage
    return response


class TestExtractMetrics:
    """Tests for LLMClient._extract_metrics()."""

    def _make_client(self):
        with patch("fastdyn.llm.llm_client.openai", create=True) as mock_openai:
            mock_openai.OpenAI = MagicMock()
            return LLMClient(api_key="sk-test", model="gpt-5.4")

    def test_basic_extraction(self):
        client = self._make_client()
        response = _make_mock_response(
            prompt_tokens=500,
            completion_tokens=200,
            total_tokens=700,
        )
        metrics = client._extract_metrics(response, latency=2.5, prompt_chars=2000, response_chars=800)

        assert isinstance(metrics, LLMCallMetrics)
        assert metrics.model == "gpt-5.4"
        assert metrics.call_id == "chatcmpl-test123"
        assert metrics.prompt_tokens == 500
        assert metrics.completion_tokens == 200
        assert metrics.total_tokens == 700
        assert metrics.reasoning_tokens is None
        assert metrics.latency_seconds == 2.5
        assert metrics.prompt_chars == 2000
        assert metrics.response_chars == 800

    def test_with_reasoning_tokens(self):
        client = self._make_client()
        response = _make_mock_response(
            prompt_tokens=1000,
            completion_tokens=500,
            total_tokens=1500,
            reasoning_tokens=350,
        )
        metrics = client._extract_metrics(response, latency=5.0, prompt_chars=4000, response_chars=2000)

        assert metrics.reasoning_tokens == 350
        assert metrics.prompt_tokens == 1000

    def test_latency_rounding(self):
        client = self._make_client()
        response = _make_mock_response()
        metrics = client._extract_metrics(response, latency=1.23456789, prompt_chars=100, response_chars=50)
        assert metrics.latency_seconds == 1.235


# ---------------------------------------------------------------------------
# send_prompt returns tuple
# ---------------------------------------------------------------------------

class TestSendPromptReturnsTuple:
    """Tests that send_prompt returns (content, metrics)."""

    def test_returns_content_and_metrics(self):
        mock_response = _make_mock_response(content="```c\nint x = 1;\n```")

        with patch("fastdyn.llm.llm_client.openai", create=True) as mock_openai:
            mock_openai.OpenAI = MagicMock()
            client = LLMClient(api_key="sk-test", model="gpt-5.4")
            client._client.chat.completions.create = MagicMock(return_value=mock_response)

            content, metrics = client.send_prompt("Generate a model.")

            assert "int x = 1;" in content
            assert isinstance(metrics, LLMCallMetrics)
            assert metrics.prompt_tokens == 100
            assert metrics.model == "gpt-5.4"

    def test_followup_returns_content_and_metrics(self):
        mock_response = _make_mock_response(content="Fixed code here.")

        with patch("fastdyn.llm.llm_client.openai", create=True) as mock_openai:
            mock_openai.OpenAI = MagicMock()
            client = LLMClient(api_key="sk-test", model="gpt-5.4")
            client._client.chat.completions.create = MagicMock(return_value=mock_response)

            content, metrics = client.send_followup_prompt(
                original_prompt="Generate model.",
                previous_response="Bad code.",
                error_context="Compilation failed.",
            )

            assert content == "Fixed code here."
            assert isinstance(metrics, LLMCallMetrics)


# ---------------------------------------------------------------------------
# Metrics JSONL writing
# ---------------------------------------------------------------------------

class TestMetricsJsonlWriting:
    """Tests for metrics JSONL file format and content."""

    def test_write_and_read_metrics_jsonl(self, tmp_path):
        metrics_path = tmp_path / "metrics.jsonl"

        m1 = LLMCallMetrics(
            model="gpt-5.4", call_id="call-1",
            prompt_tokens=500, completion_tokens=200,
            total_tokens=700, latency_seconds=2.5,
        )
        m2 = LLMCallMetrics(
            model="gpt-5.4", call_id="call-2",
            prompt_tokens=800, completion_tokens=300,
            reasoning_tokens=200, total_tokens=1100,
            latency_seconds=5.1,
        )

        # Write two entries (simulating what main.py does)
        for i, m in enumerate([m1, m2], 1):
            entry = m.to_dict()
            entry["iteration"] = 1
            entry["attempt"] = i
            entry["type"] = "initial" if i == 1 else "followup"
            entry["result"] = "compile_fail" if i == 1 else "success"
            with open(metrics_path, "a") as f:
                f.write(json.dumps(entry) + "\n")

        # Read back and verify
        lines = metrics_path.read_text().strip().split("\n")
        assert len(lines) == 2

        e1 = json.loads(lines[0])
        assert e1["call_id"] == "call-1"
        assert e1["prompt_tokens"] == 500
        assert e1["type"] == "initial"
        assert e1["result"] == "compile_fail"
        assert e1["reasoning_tokens"] is None

        e2 = json.loads(lines[1])
        assert e2["call_id"] == "call-2"
        assert e2["reasoning_tokens"] == 200
        assert e2["type"] == "followup"
        assert e2["result"] == "success"

    def test_metrics_accumulate_across_appends(self, tmp_path):
        metrics_path = tmp_path / "metrics.jsonl"

        # Simulate multiple runs appending to same file
        for run in range(3):
            entry = LLMCallMetrics(model="gpt-5.4", prompt_tokens=100 * (run + 1)).to_dict()
            entry["iteration"] = run + 1
            with open(metrics_path, "a") as f:
                f.write(json.dumps(entry) + "\n")

        lines = metrics_path.read_text().strip().split("\n")
        assert len(lines) == 3
        tokens = [json.loads(l)["prompt_tokens"] for l in lines]
        assert tokens == [100, 200, 300]
