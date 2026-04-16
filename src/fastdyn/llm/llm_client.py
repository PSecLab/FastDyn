"""
LLM client for communicating with the OpenAI ChatGPT API.

Handles API key loading, prompt submission, and response retrieval
using the official OpenAI Python SDK.
"""

import os
import json
import time
import logging
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import Optional

from .. import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


class LLMClientError(Exception):
    """Raised when the LLM client encounters an unrecoverable error."""


@dataclass
class LLMCallMetrics:
    """Metrics captured from a single LLM API call."""
    model: str = ""
    call_id: str = ""
    prompt_tokens: int = 0
    completion_tokens: int = 0
    reasoning_tokens: Optional[int] = None
    total_tokens: int = 0
    latency_seconds: float = 0.0
    prompt_chars: int = 0
    response_chars: int = 0
    timestamp: float = 0.0

    def to_dict(self):
        return asdict(self)


# The line that prompt_gen.py hardcodes to reset conversation context.
# When stateless=False, this line is stripped before sending to the API.
CONVERSATION_RESET_LINE = "Take this prompt independent from previous prompt history."


def load_api_key(env_file: str = None) -> str:
    """Load the OpenAI API key from environment or a .env file.

    Resolution order:
    1. OPENAI_API_KEY environment variable (if already set)
    2. The specified env_file (parsed manually, no dotenv dependency at import)
    3. Default location: ~/.fastdyn.env

    Args:
        env_file: Optional path to a .env file containing OPENAI_API_KEY.

    Returns:
        The API key string.

    Raises:
        LLMClientError: If no API key is found in any location.
    """
    # 1. Check environment first
    api_key = os.environ.get("OPENAI_API_KEY")
    if api_key:
        fastdyn_log.info("API key loaded from OPENAI_API_KEY environment variable")
        return api_key

    # 2. Try the specified env file or default
    env_paths = []
    if env_file:
        env_paths.append(Path(env_file).expanduser())
    env_paths.append(Path("~/.fastdyn.env").expanduser())

    for env_path in env_paths:
        if env_path.is_file():
            api_key = _parse_env_file(env_path, "OPENAI_API_KEY")
            if api_key:
                fastdyn_log.info("API key loaded from %s", env_path)
                return api_key

    raise LLMClientError(
        "OpenAI API key not found. Set it using one of:\n"
        "  1. Export OPENAI_API_KEY environment variable\n"
        "  2. Create ~/.fastdyn.env with: OPENAI_API_KEY=sk-...\n"
        "  3. Pass --env-file /path/to/.env"
    )


def _parse_env_file(path: Path, key: str) -> str:
    """Parse a simple KEY=VALUE .env file and return the value for the given key.

    Handles:
    - Lines with KEY=VALUE (with or without quotes around VALUE)
    - Comments (lines starting with #)
    - Blank lines

    Args:
        path: Path to the .env file.
        key: The key to look for.

    Returns:
        The value string, or empty string if not found.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    k, v = line.split("=", 1)
                    k = k.strip()
                    v = v.strip()
                    # Remove surrounding quotes if present
                    if len(v) >= 2 and v[0] == v[-1] and v[0] in ('"', "'"):
                        v = v[1:-1]
                    if k == key:
                        return v
    except OSError as e:
        fastdyn_log.warning("Could not read env file %s: %s", path, e)
    return ""


class LLMClient:
    """Client for sending prompts to the OpenAI ChatGPT API.

    Uses the official openai Python SDK. The API key is passed at
    construction time (use load_api_key() to resolve it first).
    """

    def __init__(self, api_key: str, model: str = "gpt-4o", temperature: float = 0.2, reasoning_effort: str = None):
        """Initialize the LLM client.

        Args:
            api_key: OpenAI API key.
            model: Model identifier (e.g., "gpt-4o", "gpt-4.1").
            temperature: Sampling temperature. Lower values produce more
                deterministic output. Default 0.2 is tuned for structured
                C code generation.
            reasoning_effort: Effort parameter for supported models ('low', 'medium', 'high').
        """
        try:
            import openai
        except ImportError:
            raise LLMClientError(
                "The 'openai' package is not installed. "
                "Install it with: pip install openai>=1.0.0"
            )

        self._client = openai.OpenAI(api_key=api_key)
        self._model = model
        self._temperature = temperature
        self._reasoning_effort = reasoning_effort
        
        is_reasoning = (reasoning_effort and reasoning_effort != "none") or "o1" in model or "o3" in model or "gpt-5" in model
        
        if is_reasoning and temperature != 0.2:
            raise LLMClientError(
                "Cannot specify a custom --temperature when using a reasoning model "
                "(e.g., o1, o3, gpt-5) or when --reasoning-effort is provided."
            )
        
        if is_reasoning:
            info_extra = []
            if reasoning_effort and reasoning_effort != "none":
                info_extra.append(f"reasoning_effort={reasoning_effort}")
            fastdyn_log.info(
                "LLM client initialized (model=%s%s)",
                model, ", " + ", ".join(info_extra) if info_extra else ""
            )
        else:
            fastdyn_log.info(
                "LLM client initialized (model=%s, temperature=%.2f)",
                model, temperature
            )

    def send_prompt(self, prompt: str, stateless: bool = True):
        """Send a prompt to the ChatGPT API and return the response text.

        Args:
            prompt: The full prompt text to send.
            stateless: If True, the conversation reset line is kept in the
                prompt (each call is independent). If False, the line is
                stripped before sending to allow multi-turn context.

        Returns:
            Tuple of (content_str, LLMCallMetrics) when evaluate mode is
            available, but for backward compatibility always returns a tuple.
            Callers that only need content can use: content, _ = client.send_prompt(...)
            or content = client.send_prompt(...)[0]

        Raises:
            LLMClientError: If the API call fails.
        """
        import openai

        # Handle the conversation reset line
        if not stateless:
            prompt = self._strip_reset_line(prompt)
            fastdyn_log.info(
                "Stripped conversation reset line (stateless=False)"
            )

        prompt_chars = len(prompt)
        fastdyn_log.info(
            "Sending prompt to %s (%d characters)...",
            self._model, prompt_chars
        )

        try:
            kwargs = {}
            if self._reasoning_effort and self._reasoning_effort != "none":
                kwargs["reasoning_effort"] = self._reasoning_effort

            # OpenAI strictly enforces that temperature cannot be customized for reasoning models.
            is_reasoning = self._reasoning_effort or "o1" in self._model or "o3" in self._model or "gpt-5" in self._model
            if not is_reasoning:
                kwargs["temperature"] = self._temperature

            start_time = time.time()
            response = self._client.chat.completions.create(
                model=self._model,
                messages=[
                    {"role": "user", "content": prompt}
                ],
                **kwargs
            )
            latency = time.time() - start_time
        except openai.AuthenticationError:
            raise LLMClientError(
                "Authentication failed. Check that your API key is valid."
            )
        except openai.RateLimitError:
            raise LLMClientError(
                "Rate limit exceeded. Wait a moment and try again, "
                "or check your OpenAI usage limits."
            )
        except openai.APIConnectionError as e:
            raise LLMClientError(
                "Could not connect to the OpenAI API: %s" % str(e)
            )
        except openai.APIError as e:
            raise LLMClientError(
                "OpenAI API error: %s" % str(e)
            )

        content = response.choices[0].message.content
        if not content:
            raise LLMClientError(
                "The LLM returned an empty response."
            )

        metrics = self._extract_metrics(response, latency, prompt_chars, len(content))

        fastdyn_log.info(
            "Received response from %s (%d characters, %d tokens, %.1fs)",
            self._model, len(content), metrics.total_tokens, latency
        )
        return content, metrics

    def send_followup_prompt(
        self,
        original_prompt: str,
        previous_response: str,
        error_context: str,
        stateless: bool = True,
    ):
        """Send a follow-up prompt that includes error context for retry.

        Constructs a two-message conversation: the original exchange,
        plus a follow-up message describing what went wrong.

        Args:
            original_prompt: The prompt that was originally sent.
            previous_response: The LLM's previous response.
            error_context: Description of the error (patch failure,
                compilation error, etc.).
            stateless: Whether to strip the conversation reset line.

        Returns:
            Tuple of (content_str, LLMCallMetrics).

        Raises:
            LLMClientError: If the API call fails.
        """
        import openai

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

        prompt_chars = len(original_prompt) + len(previous_response) + len(followup_message)
        fastdyn_log.info(
            "Sending follow-up prompt to %s with error context (%d characters)...",
            self._model, len(followup_message)
        )

        try:
            kwargs = {}
            if self._reasoning_effort and self._reasoning_effort != "none":
                kwargs["reasoning_effort"] = self._reasoning_effort

            is_reasoning = self._reasoning_effort or "o1" in self._model or "o3" in self._model or "gpt-5" in self._model
            if not is_reasoning:
                kwargs["temperature"] = self._temperature

            start_time = time.time()
            response = self._client.chat.completions.create(
                model=self._model,
                messages=[
                    {"role": "user", "content": original_prompt},
                    {"role": "assistant", "content": previous_response},
                    {"role": "user", "content": followup_message},
                ],
                **kwargs
            )
            latency = time.time() - start_time
        except openai.AuthenticationError:
            raise LLMClientError(
                "Authentication failed. Check that your API key is valid."
            )
        except openai.RateLimitError:
            raise LLMClientError(
                "Rate limit exceeded. Wait a moment and try again."
            )
        except openai.APIConnectionError as e:
            raise LLMClientError(
                "Could not connect to the OpenAI API: %s" % str(e)
            )
        except openai.APIError as e:
            raise LLMClientError(
                "OpenAI API error: %s" % str(e)
            )

        content = response.choices[0].message.content
        if not content:
            raise LLMClientError(
                "The LLM returned an empty follow-up response."
            )

        metrics = self._extract_metrics(response, latency, prompt_chars, len(content))

        fastdyn_log.info(
            "Received follow-up response from %s (%d characters, %d tokens, %.1fs)",
            self._model, len(content), metrics.total_tokens, latency
        )
        return content, metrics

    def _extract_metrics(self, response, latency: float, prompt_chars: int, response_chars: int) -> LLMCallMetrics:
        """Extract usage metrics from an OpenAI API response."""
        usage = response.usage
        reasoning_tokens = None
        if hasattr(usage, 'completion_tokens_details') and usage.completion_tokens_details:
            reasoning_tokens = getattr(usage.completion_tokens_details, 'reasoning_tokens', None)

        return LLMCallMetrics(
            model=response.model,
            call_id=response.id,
            prompt_tokens=usage.prompt_tokens,
            completion_tokens=usage.completion_tokens,
            reasoning_tokens=reasoning_tokens,
            total_tokens=usage.total_tokens,
            latency_seconds=round(latency, 3),
            prompt_chars=prompt_chars,
            response_chars=response_chars,
            timestamp=response.created,
        )

    @staticmethod
    def _strip_reset_line(prompt: str) -> str:
        """Remove the conversation reset line from a prompt."""
        lines = prompt.split("\n")
        filtered = [
            line for line in lines
            if CONVERSATION_RESET_LINE not in line
        ]
        return "\n".join(filtered)
