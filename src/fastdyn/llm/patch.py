"""
Applies SEARCH/REPLACE patches to model source files.

This module replaces the external patch.patch + script.py workflow
with in-process Python logic. Each patch is a straightforward exact
string replacement (not a unified diff).
"""

import logging
from pathlib import Path
from typing import List

from .. import fastdyn_log as fastdyn_log_conf
from .response_parser import SearchReplaceBlock

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


class PatchError(Exception):
    """Raised when a SEARCH/REPLACE patch cannot be applied."""

    def __init__(self, message: str, block_index: int = 0, search_preview: str = ""):
        self.block_index = block_index
        self.search_preview = search_preview
        super().__init__(message)


def apply_search_replace_patches(
    model_path: str,
    patches: List[SearchReplaceBlock],
) -> str:
    """Apply a sequence of SEARCH/REPLACE patches to a model file.

    Each patch's SEARCH text must be an exact character-for-character match
    in the file content. Only the first occurrence of each SEARCH text is
    replaced to avoid unintended duplicate edits.

    Args:
        model_path: Path to the C model source file to patch.
        patches: Ordered list of SearchReplaceBlock objects to apply.

    Returns:
        The fully patched file content as a string.

    Raises:
        PatchError: If any SEARCH text is not found in the file, or if
            the model file cannot be read.
    """
    model_file = Path(model_path)

    if not model_file.exists():
        raise PatchError(
            "Model file not found: %s" % model_path
        )

    content = model_file.read_text(encoding="utf-8")
    fastdyn_log.info(
        "Applying %d patch(es) to %s (%d characters)",
        len(patches), model_path, len(content)
    )

    success_count = 0

    for patch in patches:
        if patch.search_text not in content:
            # Build a helpful preview of what we were looking for
            preview_lines = patch.search_text.strip().split("\n")
            preview = preview_lines[0] if preview_lines else "(empty)"
            if len(preview) > 120:
                preview = preview[:120] + "..."

            raise PatchError(
                "Block #%d failed: the exact SEARCH text was not found "
                "in %s. First line of SEARCH: %s"
                % (patch.block_index, model_path, preview),
                block_index=patch.block_index,
                search_preview=preview,
            )

        # Replace only the first occurrence
        content = content.replace(patch.search_text, patch.replace_text, 1)
        success_count += 1

        fastdyn_log.info(
            "Block #%d applied successfully", patch.block_index
        )

    fastdyn_log.info(
        "All %d patch(es) applied cleanly to %s",
        success_count, model_path
    )
    return content


def write_patched_file(model_path: str, content: str) -> None:
    """Write patched content back to the model file.

    Args:
        model_path: Path to the file to write.
        content: The patched file content.
    """
    model_file = Path(model_path)
    model_file.write_text(content, encoding="utf-8")
    fastdyn_log.info("Patched file written to %s", model_path)


def write_model_file(model_path: str, content: str) -> None:
    """Write new model content to a file (for initial prompt results).

    Creates parent directories if they do not exist.

    Args:
        model_path: Path to the file to write.
        content: The model source code content.
    """
    model_file = Path(model_path)
    model_file.parent.mkdir(parents=True, exist_ok=True)
    model_file.write_text(content, encoding="utf-8")
    fastdyn_log.info("Model file written to %s", model_path)
