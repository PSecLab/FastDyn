"""
Parses structured content from LLM responses.

Handles two response formats:
1. Initial prompt responses: full C code in fenced code blocks
2. Revised prompt responses: SEARCH/REPLACE blocks for incremental patching
"""

import re
import logging
from dataclasses import dataclass, field
from typing import List

from .. import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


class ParsingError(Exception):
    """Raised when an LLM response cannot be parsed into the expected format."""


@dataclass
class SearchReplaceBlock:
    """One SEARCH/REPLACE edit block parsed from an LLM response."""
    target_file: str
    search_text: str
    replace_text: str
    block_index: int  # 1-based index within the response


def extract_c_code(response: str) -> str:
    """Extract C source code from an LLM response containing fenced code blocks.

    Looks for ```c ... ``` blocks. If multiple blocks are found, they are
    concatenated with blank-line separators into a single source file.

    Args:
        response: Raw text response from the LLM.

    Returns:
        The extracted C source code as a single string.

    Raises:
        ParsingError: If no fenced C code block is found in the response.
    """
    # Match ```c or ```C blocks (allow optional whitespace after the language tag)
    pattern = re.compile(r'```[cC]\s*\n(.*?)```', re.DOTALL)
    matches = pattern.findall(response)

    if not matches:
        # Fallback: try generic ``` blocks that look like C code
        generic_pattern = re.compile(r'```\s*\n(.*?)```', re.DOTALL)
        generic_matches = generic_pattern.findall(response)
        c_like = [
            m for m in generic_matches
            if _looks_like_c_code(m)
        ]
        if c_like:
            matches = c_like

    if not matches:
        raise ParsingError(
            "No fenced C code block found in the LLM response. "
            "Expected at least one ```c ... ``` block."
        )

    combined = "\n\n".join(block.strip("\r\n") for block in matches)
    fastdyn_log.info(
        "Extracted %d C code block(s) from LLM response (%d characters total)",
        len(matches), len(combined)
    )
    return combined


def parse_search_replace_blocks(response: str) -> List[SearchReplaceBlock]:
    """Parse SEARCH/REPLACE edit blocks from an LLM response.

    Expected format (may appear multiple times):
        // FILE: <filename>
        <<<<<<< SEARCH
        <exact old code to find>
        =======
        <new code to replace it with>
        >>>>>>> REPLACE

    Args:
        response: Raw text response from the LLM.

    Returns:
        List of SearchReplaceBlock objects, one per edit block.

    Raises:
        ParsingError: If the response contains no SEARCH/REPLACE blocks
            or if any block is malformed.
    """
    blocks = []

    # Split on the SEARCH marker to find each block
    raw_blocks = response.split("<<<<<<< SEARCH\n")

    # The first element is preamble text (before the first SEARCH), skip it
    if len(raw_blocks) <= 1:
        # Also try with \r\n line endings
        raw_blocks = response.split("<<<<<<< SEARCH\r\n")

    if len(raw_blocks) <= 1:
        raise ParsingError(
            "No SEARCH/REPLACE blocks found in the LLM response. "
            "Expected at least one <<<<<<< SEARCH ... >>>>>>> REPLACE block."
        )

    for i, raw_block in enumerate(raw_blocks[1:], start=1):
        # Extract the FILE header from the text before this SEARCH marker
        # Look in the preamble before this block
        preamble_end = response.find("<<<<<<< SEARCH\n")
        if preamble_end == -1:
            preamble_end = response.find("<<<<<<< SEARCH\r\n")

        # Find the target file by looking for // FILE: <name> before this block
        target_file = _find_target_file(response, raw_blocks, i)

        # Split into SEARCH and REPLACE parts
        try:
            search_part, rest = raw_block.split("\n=======\n", 1)
        except ValueError:
            raise ParsingError(
                "Malformed SEARCH/REPLACE block #%d: "
                "missing ======= separator." % i
            )

        try:
            replace_part, _ = rest.split("\n>>>>>>> REPLACE", 1)
        except ValueError:
            raise ParsingError(
                "Malformed SEARCH/REPLACE block #%d: "
                "missing >>>>>>> REPLACE marker." % i
            )

        blocks.append(SearchReplaceBlock(
            target_file=target_file,
            search_text=search_part,
            replace_text=replace_part,
            block_index=i,
        ))

    fastdyn_log.info("Parsed %d SEARCH/REPLACE block(s) from LLM response", len(blocks))
    return blocks


def _find_target_file(full_response: str, split_parts: list, block_index: int) -> str:
    """Find the // FILE: <filename> header preceding a SEARCH/REPLACE block.

    Searches backwards from the SEARCH marker in the preamble text.
    """
    # Reconstruct the text before this specific SEARCH marker
    # by joining all parts up to and including the preamble at block_index
    preamble = split_parts[block_index - 1] if block_index - 1 < len(split_parts) else ""

    # Look for the last // FILE: <filename> line in the preamble
    file_pattern = re.compile(r'//\s*FILE:\s*(\S+)', re.IGNORECASE)
    matches = file_pattern.findall(preamble)

    if matches:
        return matches[-1].strip()

    # If no FILE header found, return empty string (single-file context)
    return ""


def _looks_like_c_code(text: str) -> bool:
    """Heuristic check to determine if a text block looks like C code."""
    c_indicators = [
        "#include",
        "uint64_t",
        "uint32_t",
        "void ",
        "hwaddr",
        "typedef struct",
        "switch (",
        "case 0x",
    ]
    text_lower = text.lower()
    matches = sum(1 for indicator in c_indicators if indicator.lower() in text_lower)
    return matches >= 2
