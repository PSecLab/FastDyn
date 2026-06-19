#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import context


DEFAULT_MODEL = "ollama/qwen3:14b"
DEFAULT_BASE_URL = "http://localhost:11434"
DEFAULT_STORAGE_DIR = Path("fastdyn_work") / "agentic-crewai-data"


@dataclass(frozen=True)
class AgentSetup:
    """Reusable agent setup."""

    agent: object

def _is_writable_dir(path):
    try:
        candidate = Path(path)
        candidate.mkdir(parents=True, exist_ok=True)
        return os.access(candidate, os.W_OK)
    except OSError:
        return False

def configure_agentic_storage(storage_dir=DEFAULT_STORAGE_DIR):
    storage_path = Path(storage_dir).resolve()
    storage_path.mkdir(parents=True, exist_ok=True)

    configured_data_home = os.environ.get("XDG_DATA_HOME")
    if configured_data_home is None or not _is_writable_dir(configured_data_home):
        os.environ["XDG_DATA_HOME"] = str(storage_path)

    os.environ.setdefault("CREWAI_STORAGE_DIR", "fastdyn-agentic")
    os.environ.setdefault("CREWAI_DISABLE_TELEMETRY", "true")
    os.environ.setdefault("CREWAI_DISABLE_TRACKING", "true")

def initialize_agentic_setup(
    model: str | None = None,
    base_url: str = DEFAULT_BASE_URL,
    verbose: bool = True,
    storage_dir=DEFAULT_STORAGE_DIR,
) -> AgentSetup:
    """Initialize the reusable CrewAI agent setup for one Ghidra program."""
    configure_agentic_storage(storage_dir)

    from crewai import Agent, LLM

    local_llm = LLM(
        model=model or DEFAULT_MODEL,
        base_url=base_url,
    )

    fuzz_agent = Agent(
        role="Branch-Targeted Fuzzing Input Editor",
        goal=(
            "Provide an input to satisfy the given condition."
        ),
        backstory=(
            "You are an expert at branch-targeted input mutation. The user will provide a current input, nearby source or decompiled code, assembly for the target branch, taint hints, and the destination they want the branch to reach. "
            "Your job is to infer which fields or bytes are likely checked by the condition, then propose a concrete modified input in the same format as the original. "
            "Make the smallest focused change that seems likely to satisfy the target condition. If the evidence is incomplete or ambiguous, still make your best actionable attempt and briefly state the assumption behind it. "
        ),
        tools=[],
        llm=local_llm,
        verbose=verbose,
    )

    return AgentSetup(
        agent=fuzz_agent,
    )

def prompt_agent(setup: AgentSetup, instruction: str) -> Any:
    """Run one instruction against the setup's target file."""
    from crewai import Crew, Process, Task

    edit_task = Task(
        description=(
            "Runtime status:\n"
            "- Information:\n{instruction}\n\n"
            "Required workflow:\n"
            "1. Read the input, source/decompilation, target branch, target destination, and taint hints.\n"
            "2. Identify the most likely input field or byte range controlling the condition.\n"
            "3. Respond only with a concrete mutated input in the same format as the original.\n\n"
            "Constraints:\n"
            "- Do not make guesses, take information strictly from the prompt\n"
            "- If unsure, still provide the best concrete input attempt\n"
            "- Output only the mutated input. Do not include explanations, markdown, labels, comments, or surrounding text.\n"
        ),
        expected_output=(
            "Only the concrete mutated input in the same format as the original"
        ),
        agent=setup.agent,
    )

    crew = Crew(
        agents=[setup.agent],
        tasks=[edit_task],
        process=Process.sequential,
    )

    return crew.kickoff(
        inputs={
            "instruction": instruction,
        }
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a CrewAI agent for branch-targeted fuzzing mutations."
    )
    parser.add_argument(
        "--instruction",
        required=True,
        help="What the agent should do to the target file once it is ready.",
    )
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help=f"CrewAI LLM model name. Default: {DEFAULT_MODEL}",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"Ollama base URL. Default: {DEFAULT_BASE_URL}",
    )
    parser.add_argument(
        "--work-dir",
        default="fastdyn_work",
        help="FastDyn work directory used for generated state.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    llm = initialize_agentic_setup(
        model=args.model,
        base_url=args.base_url,
        storage_dir=Path(args.work_dir) / "agentic-crewai-data",
    )
    prompt_agent(llm, args.instruction)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
