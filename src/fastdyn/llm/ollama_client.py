"""
Ollama HTTP client for FastDyn's LLM pipeline.

This backend talks to a reachable Ollama server URL. If Ollama is running on a
remote GPU host, expose it separately with SSH port forwarding and pass the
forwarded local URL to FastDyn.
"""

import json
import time
import logging
import socket
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Optional

from fastdyn.llm.llm_client import (
    CONVERSATION_RESET_LINE,
    LLMCallMetrics,
    LLMClientError,
)
from .. import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


@dataclass
class OllamaOptions:
    """Options sent under Ollama's request-level ``options`` object."""

    temperature: float = 0.1
    num_ctx: Optional[int] = 262144

    def to_payload(self):
        payload = {"temperature": self.temperature}
        if self.num_ctx:
            payload["num_ctx"] = self.num_ctx
        return payload


class OllamaClient:
    """Client for Ollama's non-streaming /api/chat endpoint."""

    def __init__(
        self,
        model: str,
        base_url: str = "http://127.0.0.1:11434",
        temperature: float = 0.1,
        num_ctx: Optional[int] = 262144,
        timeout: float = 1800.0,
    ):
        self._model = model
        self._base_url = base_url.rstrip("/")
        self._options = OllamaOptions(temperature=temperature, num_ctx=num_ctx)
        self._timeout = timeout
        fastdyn_log.info(
            "Ollama client initialized (model=%s, url=%s, temperature=%.2f, num_ctx=%s, timeout=%.0fs)",
            model,
            self._base_url,
            temperature,
            num_ctx if num_ctx else "default",
            timeout,
        )

    def send_prompt(self, prompt: str, stateless: bool = True):
        """Send an initial prompt to Ollama and return ``(content, metrics)``."""
        if not stateless:
            prompt = self._strip_reset_line(prompt)
            fastdyn_log.info("Stripped conversation reset line (stateless=False)")

        messages = [{"role": "user", "content": prompt}]
        return self._send_chat(messages, prompt_chars=len(prompt))

    def send_followup_prompt(
        self,
        original_prompt: str,
        previous_response: str,
        error_context: str,
        stateless: bool = True,
    ):
        """Send a retry conversation with compiler/parser error context."""
        if not stateless:
            original_prompt = self._strip_reset_line(original_prompt)

        followup_message = (
            "The previous response had an error that needs correction.\n\n"
            "--- ERROR DETAILS ---\n"
            "%s\n"
            "--- END OF ERROR DETAILS ---\n\n"
            "Please fix the issue and provide corrected SEARCH/REPLACE blocks. "
            "Make sure the SEARCH text exactly matches the current file content "
            "(character for character, including whitespace and indentation)."
            % error_context
        )

        messages = [
            {"role": "user", "content": original_prompt},
            {"role": "assistant", "content": previous_response},
            {"role": "user", "content": followup_message},
        ]
        prompt_chars = len(original_prompt) + len(previous_response) + len(followup_message)
        return self._send_chat(messages, prompt_chars=prompt_chars)

    def _send_chat(self, messages, prompt_chars: int):
        url = f"{self._base_url}/api/chat"
        payload = {
            "model": self._model,
            "messages": messages,
            "stream": False,
            "options": self._options.to_payload(),
        }

        request = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )

        fastdyn_log.info(
            "Sending prompt to Ollama model %s at %s (%d characters)...",
            self._model,
            self._base_url,
            prompt_chars,
        )

        start_time = time.time()
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as response:
                raw = response.read().decode("utf-8")
        except urllib.error.HTTPError as e:
            body = ""
            try:
                body = e.read().decode("utf-8", errors="replace")
            except Exception:
                pass
            raise LLMClientError(
                "Ollama request failed with HTTP %s from %s. %s"
                % (e.code, url, body[:500])
            )
        except (urllib.error.URLError, socket.timeout, TimeoutError) as e:
            raise LLMClientError(
                "Could not connect to Ollama at %s. Ensure the server is running "
                "or that your SSH tunnel is active, then test with: "
                "curl %s/api/tags. Error: %s"
                % (self._base_url, self._base_url, str(e))
            )

        latency = time.time() - start_time

        try:
            data = json.loads(raw)
        except json.JSONDecodeError as e:
            raise LLMClientError(
                "Ollama returned invalid JSON from %s: %s" % (url, str(e))
            )

        message = data.get("message") or {}
        content = message.get("content")
        if not content:
            raise LLMClientError(
                "Ollama returned an empty response. Raw response keys: %s"
                % ", ".join(sorted(data.keys()))
            )

        metrics = self._extract_metrics(data, latency, prompt_chars, len(content))
        fastdyn_log.info(
            "Received response from Ollama model %s (%d characters, %d tokens, %.1fs)",
            self._model,
            len(content),
            metrics.total_tokens,
            latency,
        )
        return content, metrics

    def _extract_metrics(
        self,
        data: dict,
        latency: float,
        prompt_chars: int,
        response_chars: int,
    ) -> LLMCallMetrics:
        prompt_tokens = int(data.get("prompt_eval_count") or 0)
        completion_tokens = int(data.get("eval_count") or 0)
        total_tokens = prompt_tokens + completion_tokens
        return LLMCallMetrics(
            model=data.get("model") or self._model,
            call_id="",
            prompt_tokens=prompt_tokens,
            completion_tokens=completion_tokens,
            reasoning_tokens=None,
            total_tokens=total_tokens,
            latency_seconds=round(latency, 3),
            prompt_chars=prompt_chars,
            response_chars=response_chars,
            timestamp=time.time(),
        )

    @staticmethod
    def _strip_reset_line(prompt: str) -> str:
        lines = prompt.split("\n")
        filtered = [
            line for line in lines
            if CONVERSATION_RESET_LINE not in line
        ]
        return "\n".join(filtered)
