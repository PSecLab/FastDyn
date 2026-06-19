import os
import glob
import logging
from fastdyn.llm.response_parser import (
    extract_c_code,
    parse_search_replace_blocks,
    ParsingError
)
from fastdyn.llm.patch import (
    apply_search_replace_patches,
    write_patched_file,
    write_model_file,
    PatchError
)
from fastdyn.llm.compiler import (
    compile_model,
    format_compilation_error,
    CompilationError
)

log = logging.getLogger(__name__)


def llm_history_next(history_dir: str) -> int:
    """Return the next iteration number for the LLM history directory.

    Scans for existing NNN_prompt.txt files and returns max(N) + 1, or 1 if none exist.
    """
    os.makedirs(history_dir, exist_ok=True)
    existing = glob.glob(os.path.join(history_dir, "*_prompt.txt"))
    if not existing:
        return 1
    nums = []
    for f in existing:
        base = os.path.basename(f)
        prefix = base.split("_prompt.txt")[0]
        if prefix.isdigit():
            nums.append(int(prefix))
    return max(nums) + 1 if nums else 1


def handle_initial_prompt(response_text, output_path, work_dir):
    """Process an initial prompt response: extract C code and write model file.

    Returns:
        Tuple of (success: bool, error_context: str).
    """
    try:
        c_code = extract_c_code(response_text)
    except ParsingError as e:
        log.error("Failed to extract C code from LLM response: %s", str(e))
        error_context = (
            "Failed to extract C code from the response. "
            "The response did not contain a properly fenced ```c code block.\n"
            "Please provide the complete C device model in a single fenced "
            "```c ... ``` code block."
        )
        return False, error_context

    write_model_file(output_path, c_code)
    log.info("Model written to %s", output_path)
    return True, ""


def handle_revised_prompt(response_text, output_paths, work_dir):
    """Process a revised prompt response: parse and apply SEARCH/REPLACE patches.

    Returns:
        Tuple of (success: bool, error_context: str).
    """
    try:
        patches = parse_search_replace_blocks(response_text)
    except ParsingError as e:
        log.error("Failed to parse SEARCH/REPLACE blocks: %s", str(e))
        error_context = (
            "Failed to parse SEARCH/REPLACE blocks from the response.\n"
            "Error: %s\n"
            "Please provide corrections using the exact SEARCH/REPLACE format:\n"
            "// FILE: <filename>\n"
            "<<<<<<< SEARCH\n"
            "[exact old code]\n"
            "=======\n"
            "[new code]\n"
            ">>>>>>> REPLACE" % str(e)
        )
        return False, error_context

    from collections import defaultdict
    patches_by_path = defaultdict(list)

    for patch in patches:
        if patch.target_file:
            matched = None
            for p in output_paths:
                if os.path.basename(p) == patch.target_file:
                    matched = p
                    break
            if matched:
                patches_by_path[matched].append(patch)
            else:
                log.warning("Target file %s not found in output arguments (-o). Defaulting to %s", patch.target_file, output_paths[0])
                patches_by_path[output_paths[0]].append(patch)
        else:
            patches_by_path[output_paths[0]].append(patch)

    # First pass: try to apply all patches in-memory to ensure atomic success
    patched_files = {}
    for path, file_patches in patches_by_path.items():
        try:
            patched_content = apply_search_replace_patches(path, file_patches)
            patched_files[path] = patched_content
        except PatchError as e:
            log.error("Patch application failed: %s", str(e))
            # Read the current file content to include in error context
            current_content = ""
            try:
                with open(path, "r", encoding="utf-8") as f:
                    current_content = f.read()
            except OSError:
                pass

            error_context = (
                "Patch application failed.\n"
                "Error: %s\n\n"
                "The SEARCH text in block #%d was not found in the file: %s.\n"
                "This usually means the SEARCH text does not exactly match the "
                "current file content (check whitespace, indentation, and exact characters).\n\n"
                "Current file content (first 5000 chars):\n%s"
                % (str(e), e.block_index, os.path.basename(path), current_content[:5000])
            )
            return False, error_context

    # Second pass: commit all patches to disk (atomic write)
    for path, patched_content in patched_files.items():
        write_patched_file(path, patched_content)
        log.info("Patches applied and saved to %s", path)

    return True, ""


def handle_compilation(sdk_dir):
    """Run model compilation and return result.

    Returns:
        Tuple of (success: bool, error_context: str, is_setup_error: bool).
    """
    try:
        success, build_output = compile_model(sdk_dir)
    except CompilationError as e:
        log.error("Compilation setup failed: %s", str(e))
        return False, str(e), True

    if not success:
        error_context = format_compilation_error(build_output)
        log.error("Model compilation failed. See error details above.")
        return False, error_context, False

    log.info("Model compiled successfully.")
    return True, "", False

def handle_routing(routing_json, work_dir):
    """Log routing recommendations without mutating files or config."""
    existing = routing_json.get("request_existing_models", []) or []
    create_new = routing_json.get("create_new_models", []) or []

    if existing:
        log.info("\n[ROUTING] Existing model context requested:")
        for item in existing:
            log.info(
                "  - %s (intent=%s)",
                item.get("name", "unknown"),
                item.get("intent", "context_only"),
            )

    if create_new:
        log.info("\n[ROUTING] New model/config materialization requested:")
        for item in create_new:
            attach = item.get("attach_to_peripheral")
            attach_text = f", attach_to={attach}" if attach else ""
            log.info(
                "  - %s (category=%s%s)",
                item.get("name", "unknown"),
                item.get("category", "unknown"),
                attach_text,
            )

        log.info(
            "Run `fastdyn trace-analyze ... --apply-routing` to review and "
            "materialize these recommendations interactively."
        )
    else:
        log.info(
            "Run `fastdyn trace-analyze` again to generate the routed "
            "implementation prompt."
        )

    return True, ""
