import argparse
import logging
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import pyghidra

import parsers


FASTDYN_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_WORK_DIR = "fastdyn_work"
DEFAULT_PROJECT_DIR = Path(DEFAULT_WORK_DIR) / "ghidra_projects"
DEFAULT_PROJECT_NAME = "test"
DEFAULT_MODEL = "ollama/qwen3:30b"
DEFAULT_BASE_URL = "http://localhost:11434"
DEFAULT_LLM_STORAGE_DIR = Path(DEFAULT_WORK_DIR) / "agentic-crewai-data"
DEFAULT_GENERATED_INPUT_SIZE = 4096
DEFAULT_PROTOCOL_FUZZERS_DIR = (
    FASTDYN_ROOT
    / "virtuals"
    / "fuzzer"
    / "protocol_fuzzers"
)


def configure_logging():
    fastdyn_src = FASTDYN_ROOT / "src"
    if fastdyn_src.is_dir() and str(fastdyn_src) not in sys.path:
        sys.path.insert(0, str(fastdyn_src))

    try:
        from fastdyn import fastdyn_log

        fastdyn_log.setLogConfig()
    except Exception:
        logging.basicConfig(
            level=logging.INFO,
            format="%(name)s|%(levelname)s|  %(message)s",
        )


configure_logging()
log = logging.getLogger(__name__)


@dataclass(frozen=True)
class SelectedFunction:
    address: int
    name: str = None
    index: int = None


@dataclass(frozen=True)
class InstructionPoint:
    address: int
    text: str
    flow_type: str


@dataclass(frozen=True)
class FunctionPointsOfInterest:
    function_address: int
    function_name: str
    entry: InstructionPoint
    exits: tuple
    decompilation: str
    disassembly: tuple


@dataclass(frozen=True)
class LLMHandle:
    llm: object
    model: str
    base_url: str


def configure_llm_storage(storage_dir=DEFAULT_LLM_STORAGE_DIR):
    storage_path = Path(storage_dir).resolve()
    storage_path.mkdir(parents=True, exist_ok=True)

    configured_data_home = os.environ.get("XDG_DATA_HOME")
    if configured_data_home is None or not _is_writable_dir(configured_data_home):
        os.environ["XDG_DATA_HOME"] = str(storage_path)

    os.environ.setdefault("CREWAI_STORAGE_DIR", "fastdyn-agentic-harness")
    os.environ.setdefault("CREWAI_DISABLE_TELEMETRY", "true")
    os.environ.setdefault("CREWAI_DISABLE_TRACKING", "true")


def _is_writable_dir(path):
    try:
        candidate = Path(path)
        candidate.mkdir(parents=True, exist_ok=True)
        return os.access(candidate, os.W_OK)
    except OSError:
        return False


def initialize_llm(model=None, base_url=DEFAULT_BASE_URL, storage_dir=DEFAULT_LLM_STORAGE_DIR):
    configure_llm_storage(storage_dir)

    from crewai import LLM

    selected_model = model or DEFAULT_MODEL
    return LLMHandle(
        llm=LLM(
            model=selected_model,
            base_url=base_url,
            api_key=os.environ.get("OPENAI_API_KEY", "ollama"),
        ),
        model=selected_model,
        base_url=base_url,
    )


def prompt_llm(handle, prompt, system_prompt=None):
    if system_prompt is None:
        return handle.llm.call(prompt)

    return handle.llm.call(
        [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": prompt},
        ]
    )


def load_or_import_program(project, binary_path: Path, program_path: str):
    """
    Return (program, consumer, existed) from the Ghidra project.

    This mirrors monitor.py's load path so the harness and monitor reuse the
    same project layout and program names.
    """
    try:
        program, consumer = pyghidra.consume_program(project, program_path)
        return program, consumer, True
    except FileNotFoundError:
        pass

    loader = (
        pyghidra.program_loader()
        .project(project)
        .source(str(binary_path))
        .name(binary_path.name)
    )

    with loader.load() as load_results:
        load_results.save(pyghidra.task_monitor())

    program, consumer = pyghidra.consume_program(project, program_path)
    return program, consumer, False


def points_of_interest(program, target):
    """Return the function entry instruction and instructions that can exit it."""
    if target is None:
        return None

    target_address = target.address if hasattr(target, "address") else int(target)
    function = _resolve_function(program, target_address)
    if function is None:
        raise ValueError(f"no Ghidra function contains {hex(target_address)}")

    listing = program.getListing()
    entry_instruction = listing.getInstructionAt(function.getEntryPoint())
    if entry_instruction is None:
        entry_instruction = listing.getInstructionContaining(function.getEntryPoint())
    if entry_instruction is None:
        raise ValueError(f"no instruction found at {function.getEntryPoint()}")

    exits = []
    disassembly = []
    seen_exits = set()
    instructions = listing.getInstructions(function.getBody(), True)
    while instructions.hasNext():
        instruction = instructions.next()
        disassembly.append(_instruction_point(instruction))

        if not _is_function_exit_instruction(function, instruction):
            continue

        address = int(instruction.getAddress().getOffset())
        if address in seen_exits:
            continue

        seen_exits.add(address)
        exits.append(_instruction_point(instruction))

    decompilation = _decompile_function(function)

    return FunctionPointsOfInterest(
        function_address=int(function.getEntryPoint().getOffset()),
        function_name=function.getName(),
        entry=_instruction_point(entry_instruction),
        exits=tuple(exits),
        decompilation=decompilation,
        disassembly=tuple(disassembly),
    )


def _decompile_function(function):
    from ghidra.app.decompiler import DecompInterface

    ifc = DecompInterface()
    if not ifc.openProgram(function.getProgram()):
        return "<failed to open program in decompiler>"

    results = ifc.decompileFunction(function, 30, pyghidra.task_monitor())
    if not results.decompileCompleted():
        return "<decompilation failed>"

    decompiled = results.getDecompiledFunction()
    if decompiled is None:
        return "<decompilation unavailable>"

    return decompiled.getC()


def _resolve_function(program, target_address):
    function_manager = program.getFunctionManager()
    address_space = program.getAddressFactory().getDefaultAddressSpace()

    for offset in _candidate_function_offsets(target_address):
        address = address_space.getAddress(offset)
        function = function_manager.getFunctionAt(address)
        if function is not None:
            return function

        function = function_manager.getFunctionContaining(address)
        if function is not None:
            return function

    return None


def _candidate_function_offsets(address):
    yield address
    if address & 1:
        yield address & ~1


def _instruction_point(instruction):
    return InstructionPoint(
        address=int(instruction.getAddress().getOffset()),
        text=str(instruction),
        flow_type=str(instruction.getFlowType()),
    )


def _flow_type_matches(flow_type, method_name):
    method = getattr(flow_type, method_name, None)
    if method is None:
        return False
    return bool(method())


def _body_contains(body, address):
    return address is not None and bool(body.contains(address))


def _instruction_flows(instruction):
    flows = instruction.getFlows()
    return [flows[i] for i in range(len(flows))]


def _is_function_exit_instruction(function, instruction):
    body = function.getBody()
    flow_type = instruction.getFlowType()

    if _flow_type_matches(flow_type, "isReturn"):
        return True

    if _flow_type_matches(flow_type, "isTerminal"):
        return True

    fallthrough = instruction.getFallThrough()
    flows = _instruction_flows(instruction)

    if fallthrough is not None and not _body_contains(body, fallthrough):
        return True

    is_call = _flow_type_matches(flow_type, "isCall")
    if is_call:
        # Normal calls target code outside the function but return through fallthrough.
        return fallthrough is None

    for destination in flows:
        if not _body_contains(body, destination):
            return True

    return fallthrough is None and not flows


def _candidate_label(candidate):
    if isinstance(candidate, (tuple, list)) and candidate:
        return str(candidate[0])
    return str(candidate)


def _split_candidate_label(label):
    match = re.match(r"^\s*(0x[0-9a-fA-F]+)\s*:\s*(.+?)\s*$", label)
    if match is None:
        return None, label

    return int(match.group(1), 16), match.group(2)


def _parse_address(text):
    token = text.strip()
    if token.lower().startswith("addr "):
        token = token.split(None, 1)[1].strip()

    if re.fullmatch(r"0[xX][0-9a-fA-F]+", token):
        return int(token, 16)

    if re.fullmatch(r"[0-9a-fA-F]+", token):
        return int(token, 16)

    return None


def _select_manual_address(input_fn, output_fn):
    while True:
        choice = input_fn("Enter function address, or q to quit: ").strip()
        if choice.lower() in {"q", "quit", "exit"}:
            return None

        address = _parse_address(choice)
        if address is not None:
            return SelectedFunction(address=address)

        output_fn("Please enter an address such as 0x8001234.")


def select_function(potential_parsers, batch_size=10, input_fn=input, output_fn=print):
    candidates = list(potential_parsers)
    if not candidates:
        output_fn("No parser candidates were found.")
        return _select_manual_address(input_fn, output_fn)

    shown = 0
    while True:
        end = min(shown + batch_size, len(candidates))

        for index, candidate in enumerate(candidates[shown:end], start=shown + 1):
            address, name = _split_candidate_label(_candidate_label(candidate))
            address_text = hex(address) if address is not None else "unknown"
            output_fn(f"{index:>4}. {address_text:<12} {name}")

        if end < len(candidates):
            prompt = (
                "Select index/address, press Enter for more, "
                "or q to quit: "
            )
        else:
            prompt = "Select index/address, or q to quit: "

        choice = input_fn(prompt).strip()
        lowered = choice.lower()

        if not choice or lowered in {"m", "more", "n", "next"}:
            if end < len(candidates):
                shown = end
            else:
                output_fn("No more candidates.")
            continue

        if lowered in {"q", "quit", "exit"}:
            return None

        if re.fullmatch(r"\d+", choice):
            index = int(choice)
            if 1 <= index <= len(candidates):
                address, name = _split_candidate_label(_candidate_label(candidates[index - 1]))
                if address is None:
                    output_fn("Selected candidate has no parseable address.")
                    continue
                return SelectedFunction(address=address, name=name, index=index)

        address = _parse_address(choice)
        if address is not None:
            return SelectedFunction(address=address)

        output_fn("Enter a result index, an address such as 0x8001234, or more.")


def with_analyzed_program(
    binary,
    callback,
    project_dir=DEFAULT_PROJECT_DIR,
    project_name=DEFAULT_PROJECT_NAME,
):
    target = Path(binary).resolve()
    target_base = "/" + target.name

    project_path = Path(project_dir).resolve()
    project_path.mkdir(parents=True, exist_ok=True)

    log.info("analyzing %s in %s", target, project_path)

    pyghidra.start()

    from ghidra.program.util import GhidraProgramUtilities

    with pyghidra.open_project(project_path, project_name, create=True) as project:
        program, consumer, existed = load_or_import_program(project, target, target_base)

        try:
            if existed:
                log.info("loaded existing program")
            else:
                log.info("opened new file")

            if not GhidraProgramUtilities.isAnalyzed(program):
                log.info("running Ghidra analysis")
                analysis_log = pyghidra.analyze(program, pyghidra.task_monitor(600))
                program.save("PyGhidra analysis", pyghidra.task_monitor())
                if analysis_log:
                    log.info("Ghidra analysis log:\n%s", analysis_log)
                log.info("analysis complete")
            else:
                log.info("already analyzed")

            return callback(program)
        finally:
            program.release(consumer)


def parser_candidates(binary, project_dir=DEFAULT_PROJECT_DIR, project_name=DEFAULT_PROJECT_NAME):
    """
    Open binary in Ghidra, run analysis if needed, and return parser candidates.

    The returned value is parsers.parser_search(program)'s ranked list of
    (function_name, score, support) tuples.
    """
    return with_analyzed_program(
        binary,
        parsers.parser_search,
        project_dir=project_dir,
        project_name=project_name,
    )


def print_points_of_interest(points):
    if points is None:
        return

    print("Entry of function:")
    print(f"    {hex(points.entry.address)} {points.entry.text}")
    print()
    print("Exits of function:")

    if not points.exits:
        print("    <none found>")
    else:
        for point in points.exits:
            print(f"    {hex(point.address)} {point.text}")

    print()
    print("Deocmpilation of function:")
    print(points.decompilation)
    print()
    print("Disassembly of function:")
    if not points.disassembly:
        print("    <none found>")
    else:
        for point in points.disassembly:
            print(f"    {hex(point.address)} {point.text}")


def _format_instruction_points(points):
    if not points:
        return "    <none found>"

    return "\n".join(
        f"    {hex(point.address)} {point.text}"
        for point in points
    )


def build_input_location_prompt(points):
    return f"""You are locating the raw input object at the exact entry point of a parser-like function.

Use the function entry instruction, exit instructions, Ghidra decompilation, and disassembly below to infer where the input is stored when control enters the function, before the prologue or body has transformed it. The answer should identify the entry-time storage location and the input size if it can be inferred.

Focus only on:
- where the input bytes are at function entry
- how to address them at entry
- how many bytes are available or consumed

Do not enumerate individual input fields. Do not describe parser semantics.

Inference rules:
- Prefer the machine-level calling convention and entry disassembly over decompiler variable names.
- If the input pointer is passed in a register, report pointer_in_register and the register name, such as r0, x0, a0, edi, or rdi.
- If the input bytes themselves are passed directly in registers, report register_values and list the registers.
- If the function reads from a global/static buffer, report global_memory and include the address or symbol if visible.
- If the entry register is immediately spilled to stack by the prologue, still report the original entry register and mention the stack spill only in notes.
- If there is a separate length/size argument, identify its entry register, stack slot, global, or constant source.
- Infer size from explicit length parameters, bounds checks, memcpy/memcmp sizes, array indexing, parser loop limits, or constants. If size is variable, say variable and name the size source.
- If the size cannot be inferred, say unknown. Do not invent a fixed size.
- If multiple possible input locations exist, list candidates with confidence and choose the most likely one.

Preferred output shape:
Input location at entry:
  storage: <pointer_in_register|register_values|stack_pointer|global_memory|unknown>
  location: <register/stack slot/global address/expression>
  dereference: <yes|no|unknown>
  size:
    kind: <constant|variable|minimum|unknown>
    bytes: <number, expression, or unknown>
    source: <entry register/constant/check/call/etc or unknown>
  candidates:
    - storage: <kind>
      location: <where>
      confidence: <high|medium|low>
  notes:
    - <short caveat only when needed>

Function:
    {hex(points.function_address)} {points.function_name}

Entry of function:
{_format_instruction_points([points.entry])}

Exits of function:
{_format_instruction_points(points.exits)}

Decompilation of function:
{points.decompilation}

Disassembly of function:
{_format_instruction_points(points.disassembly)}
"""


def build_input_location_revision_prompt(points, previous_answer, feedback):
    return f"""{build_input_location_prompt(points)}

Previous input-location answer:
{previous_answer}

IMPORTANT USER FEEDBACK:
{feedback}

Revise the input-location answer to follow the user feedback. Treat the feedback as higher priority than your previous guess unless it directly contradicts the code context. Keep the output focused only on the entry-time input storage location and size.
"""


def infer_input_location(llm_handle, points):
    system_prompt = (
        "You infer where raw input is stored at function entry from calling "
        "conventions, decompilation, and assembly. Report the entry-time "
        "storage location and input size only. Avoid parser semantics."
    )
    return prompt_llm(
        llm_handle,
        build_input_location_prompt(points),
        system_prompt=system_prompt,
    )


def revise_input_location(llm_handle, points, previous_answer, feedback):
    system_prompt = (
        "You revise an entry-time raw input location answer from parser "
        "decompilation, assembly, a previous answer, and explicit user "
        "feedback. User feedback is high priority. Report only the input "
        "storage location and size."
    )
    return prompt_llm(
        llm_handle,
        build_input_location_revision_prompt(points, previous_answer, feedback),
        system_prompt=system_prompt,
    )


def review_input_location_loop(llm_handle, points, input_fn=input, output_fn=print):
    answer = infer_input_location(llm_handle, points)

    while True:
        output_fn(answer)
        feedback = input_fn(
            "\nPress Enter to accept, or type feedback to revise the input location: "
        ).strip()
        if not feedback:
            return answer

        answer = revise_input_location(llm_handle, points, answer, feedback)


def _c_identifier(name):
    ident = re.sub(r"[^0-9A-Za-z_]", "_", name)
    ident = re.sub(r"_+", "_", ident).strip("_")
    if not ident:
        return "target"
    if ident[0].isdigit():
        ident = f"target_{ident}"
    return ident.lower()


def build_harness_prompt(points, input_location):
    target_name = _c_identifier(points.function_name)
    hook_name = f"fuzz_{target_name}_inject"
    return f"""Generate a simple C fuzz injection harness for FastDyn.

The previous step identified where the raw input is stored at the exact function entry. Use that accepted input-location answer to write one small callback function that injects fuzz bytes into the firmware state before the target function consumes them. The function will be called through the existing FastDyn snap-point callback path, not as a virtual_register hook.

Output only C code. Do not use markdown fences. Do not explain the code.

Callback type:
typedef void (*fuzz_callback_t)();

Required fuzz helper prototypes to assume:
size_t fuzz_get_data(char* buf, size_t len);
uint32_t fuzz_get_register(int reg);
void fuzz_set_register(uint32_t value, int reg);
int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

Required harness shape:
- Injection callback prototype must be compatible with fuzz_callback_t:
  void {hook_name}(void)
- Do not make the injection callback static; it will be referenced externally as a callback.
- Do not generate an init function.
- Do not call virtual_register.
- Include any needed static buffer or static variables above the function.
- The buffer should be zeroed before getting fuzz data.
- Get bytes with fuzz_get_data((char *)buffer, sizeof(buffer)).
- Use the accepted input location to inject bytes:
  - pointer in register: read pointer with fuzz_get_register(reg), then fuzz_write_memory(pointer, buffer, len_to_write)
  - input bytes directly in registers: pack bytes from buffer into integer values and fuzz_set_register(value, reg)
  - global/static memory: fuzz_write_memory(global_address, buffer, len_to_write)
  - size register/argument: if known, fuzz_set_register(len_to_write, size_reg) or write size to the known memory location
  - existing firmware object: use fuzz_read_memory to read the object, patch the relevant pointer/length/payload fields, then fuzz_write_memory it back
- Use the inferred size when available. If size is unknown or variable, use #define FUZZ_INPUT_SIZE {DEFAULT_GENERATED_INPUT_SIZE}.
- Clamp writes to the available target size when a size is known.
- Avoid malloc and complicated protocol encoding.
- The generated function should be safe to call repeatedly from the snap-point callback path.

Accepted input-location answer:
{input_location}

Target function:
    {hex(points.function_address)} {points.function_name}

Entry of function:
{_format_instruction_points([points.entry])}

Exits of function:
{_format_instruction_points(points.exits)}

Decompilation of function:
{points.decompilation}

Disassembly of function:
{_format_instruction_points(points.disassembly)}
"""


def build_harness_revision_prompt(points, input_location, previous_harness, feedback):
    return f"""{build_harness_prompt(points, input_location)}

Previous generated harness:
{previous_harness}

IMPORTANT USER FEEDBACK:
{feedback}

Revise the harness to follow the user feedback. Treat the feedback as higher priority than your previous generated code unless it directly contradicts the accepted input-location answer. Keep the output as C code only.
"""


def generate_injection_harness(llm_handle, points, input_location):
    system_prompt = (
        "You generate small no-argument C FastDyn fuzz injection callbacks "
        "compatible with typedef void (*fuzz_callback_t)(). Use the "
        "accepted entry-time input location to inject fuzz_get_data bytes "
        "with fuzz_get_register, fuzz_set_register, fuzz_read_memory, and "
        "fuzz_write_memory. Do not emit an init function and do not call "
        "virtual_register. Output C code only."
    )
    return prompt_llm(
        llm_handle,
        build_harness_prompt(points, input_location),
        system_prompt=system_prompt,
    )


def revise_injection_harness(
    llm_handle,
    points,
    input_location,
    previous_harness,
    feedback,
):
    system_prompt = (
        "You revise small no-argument C FastDyn fuzz injection callbacks from "
        "explicit user feedback. Preserve compatibility with typedef void "
        "(*fuzz_callback_t)(), preserve helper function usage, and do not add "
        "an init function or virtual_register call. Output C code only."
    )
    return prompt_llm(
        llm_handle,
        build_harness_revision_prompt(
            points,
            input_location,
            previous_harness,
            feedback,
        ),
        system_prompt=system_prompt,
    )


def review_injection_harness_loop(
    llm_handle,
    points,
    input_location,
    input_fn=input,
    output_fn=print,
):
    harness = generate_injection_harness(llm_handle, points, input_location)

    while True:
        output_fn(harness)
        feedback = input_fn(
            "\nPress Enter to accept, or type feedback to revise the harness: "
        ).strip()
        if not feedback:
            return harness

        harness = revise_injection_harness(
            llm_handle,
            points,
            input_location,
            harness,
            feedback,
        )


def _parse_integer_token(token):
    match = re.search(
        r"(?<![A-Za-z0-9_])(?:0[xX][0-9a-fA-F]+|\d+)(?![A-Za-z0-9_])",
        token,
    )
    if match is None:
        return None

    value = int(match.group(0), 0)
    return value if value >= 0 else None


def parse_constant_input_size(input_location):
    """
    Best-effort extraction of a fixed input size from the accepted LLM answer.

    The input-location prompt asks for a size block with "kind" and "bytes".
    Only constant/fixed/exact answers are treated as safe fixed sizes.
    """
    text = str(input_location)
    lines = text.splitlines()
    size_kind = None
    size_value = None
    in_size_block = False

    for line in lines:
        stripped = line.strip()
        lowered = stripped.lower()

        if not stripped:
            continue

        if re.match(r"^size\s*:", lowered):
            in_size_block = True
            value = _parse_integer_token(stripped.split(":", 1)[1])
            if value is not None:
                size_value = value
            continue

        size_field = re.match(r"^(kind|bytes|source)\s*:", lowered)
        if in_size_block and line[:1] not in {" ", "\t", "-"} and size_field is None:
            in_size_block = False

        if not in_size_block:
            continue

        kind_match = re.match(r"^kind\s*:\s*([A-Za-z_ -]+)", lowered)
        if kind_match is not None:
            size_kind = kind_match.group(1).strip()
            continue

        bytes_match = re.match(r"^bytes\s*:\s*(.+)", stripped, flags=re.IGNORECASE)
        if bytes_match is not None:
            value = _parse_integer_token(bytes_match.group(1))
            if value is not None:
                size_value = value

    if size_kind is not None:
        fixed_kinds = {"constant", "fixed", "exact", "known"}
        if size_kind not in fixed_kinds:
            return None

    if size_value is not None:
        return size_value

    fallback = re.search(
        r"(?:input|target|buffer)?\s*size\s*(?:is|=|:)\s*"
        r"(0[xX][0-9a-fA-F]+|\d+)\s*(?:bytes?)?",
        text,
        flags=re.IGNORECASE,
    )
    if fallback is None:
        return None

    return int(fallback.group(1), 0)


def build_triton_snippet_prompt(points, input_location, generated_harness):
    target_name = _c_identifier(points.function_name)
    parsed_input_size = parse_constant_input_size(input_location)
    input_size_literal = parsed_input_size if parsed_input_size is not None else "None"
    return f"""Generate target-dependent Python code snippets for FastDyn/virtuals/fuzzer/agentic/flow.py's Triton taint setup.

The generated code will be copied into flow.py to replace the generic target input hooks. triton_get_taint and triton_is_tainted already call these hooks, so the output should define the two generic hook functions exactly as named below.

Output only Python code. Do not use markdown fences. Do not explain the code outside Python comments.

Accepted input-location answer:
{input_location}

Parsed fixed input size from the accepted answer:
TRITON_TARGET_INPUT_SIZE = {input_size_literal}

Accepted FastDyn C injection harness:
{_strip_markdown_code_fence(generated_harness)}

Target function:
    {hex(points.function_address)} {points.function_name}

Entry of function:
{_format_instruction_points([points.entry])}

Exits of function:
{_format_instruction_points(points.exits)}

Relevant flow.py context:
- flow.py imports Triton with: from triton import *
- The Triton context variable is named ctx.
- Snapshot loading returns snap from load_memory_register_snapshot(ctx, regions, snapshot).
- snap["registers"] is a list of 16 ARM registers saved as little-endian uint32 values:
  snap["registers"][0] is r0, [1] is r1, [2] is r2, ... [13] is sp, [14] is lr, [15] is pc.
- input_bytes is the raw input bytes read from the current .raw file.
- In triton_get_taint, chunk_start and chunk_end identify the input offset range currently being tainted.
- In triton_is_tainted, all bytes in input_bytes should be written and tainted.
- flow.py calls triton_write_target_input(ctx, snap, input_bytes) after loading the snapshot and before processing trace instructions.
- flow.py calls triton_taint_target_input_range(ctx, snap, start, end) to taint byte offsets in the same logical raw input object.
- TRITON_TARGET_INPUT_SIZE is a global target hook constant. When it is an integer, all input writes should be exactly that many bytes: zero-pad short raw inputs and truncate long raw inputs. When it is None, preserve the full raw input length.
- triton_target_input_bytes(input_bytes) returns input_bytes padded/truncated according to TRITON_TARGET_INPUT_SIZE.
- triton_target_input_range(start, end) returns a range clamped to TRITON_TARGET_INPUT_SIZE when known.

Relevant Triton APIs and helpers available in flow.py:
- ctx.registers.r0, ctx.registers.r1, ctx.registers.r2, ..., ctx.registers.r15
- ctx.registers.sp, ctx.registers.lr, ctx.registers.pc
- ctx.getConcreteRegisterValue(ctx.registers.r0)
- ctx.setConcreteRegisterValue(ctx.registers.r0, value)
- ctx.getConcreteMemoryValue(addr)
- ctx.setConcreteMemoryValue(addr, byte_value)
- ctx.setConcreteMemoryAreaValue(addr, bytes_value)
- _set_triton_memory(ctx, addr, data) writes a bytes-like object and falls back to byte writes if needed.
- MemoryAccess(addr, CPUSIZE.BYTE)
- ctx.taintMemory(MemoryAccess(addr, CPUSIZE.BYTE))
- ctx.taintRegister(ctx.registers.r0)
- ctx.isMemoryTainted(MemoryAccess(addr, CPUSIZE.BYTE))
- ctx.isRegisterTainted(ctx.registers.r0)

Current default target hooks in flow.py to replace:
TRITON_TARGET_INPUT_SIZE = None

def triton_write_target_input(ctx, snap, input_bytes):
    \"\"\"Write raw input bytes into this target's Triton concrete state.\"\"\"
    base = snap["registers"][2]
    _set_triton_memory(ctx, base, triton_target_input_bytes(input_bytes))

def triton_taint_target_input_range(ctx, snap, start, end):
    \"\"\"Taint raw input byte offsets [start, end) in this target's Triton state.\"\"\"
    base = snap["registers"][2]
    for offset in triton_target_input_range(start, end):
        ctx.taintMemory(MemoryAccess(base + offset, CPUSIZE.BYTE))

Required output shape:
# Target-dependent Triton input hooks for {target_name}
TRITON_TARGET_INPUT_SIZE = {input_size_literal}

def triton_write_target_input(ctx, snap, input_bytes):
    ...

def triton_taint_target_input_range(ctx, snap, start, end):
    ...

Implementation rules:
- Use the accepted input-location answer and accepted C harness as the source of truth.
- If TRITON_TARGET_INPUT_SIZE above is an integer, copy that exact value into the output. If it is None but you can infer a fixed size from the context, set it to that integer; otherwise leave it as None.
- When writing memory input, pass triton_target_input_bytes(input_bytes) to _set_triton_memory or iterate over triton_target_input_bytes(input_bytes), not the raw input_bytes directly.
- When tainting memory input, iterate over triton_target_input_range(start, end), not range(start, end), so long inputs cannot taint past the fixed target input object.
- When packing direct-register input, pack from triton_target_input_bytes(input_bytes) so short inputs are zero-filled and long inputs are truncated before packing.
- triton_write_target_input must write input_bytes into the concrete Triton state at the target's exact entry-time input location. It should also update a known length register or memory field when the accepted input-location answer identifies one.
- triton_taint_target_input_range must taint the same logical raw input object for byte offsets in [start, end). If the input is direct register data, taint the register or registers that overlap that byte range.
- If the input is pointed to by an entry register, use snap["registers"][index] or ctx.getConcreteRegisterValue(ctx.registers.rN) to recover the pointer, write bytes to that memory, and taint the corresponding memory bytes.
- If the input length is represented by a register or memory field at entry, set it to the clamped input length when the location is known.
- If the input bytes are directly in registers, pack bytes little-endian into register-width values with zero-fill, call ctx.setConcreteRegisterValue for those registers, and taint those registers.
- If the input lives in global/static memory, write and taint memory starting at that address.
- Clamp writes and tainting to any known target input size. If the size is unknown, use len(input_bytes).
- Avoid target semantics, assertions, imports, file I/O, or changes to ctx architecture/modes.
- Keep the snippet simple enough to paste directly over the default target hooks in flow.py.
"""


def generate_triton_target_snippets(
    llm_handle,
    points,
    input_location,
    generated_harness,
):
    system_prompt = (
        "You generate paste-ready Python snippets for Triton taint setup in "
        "FastDyn/virtuals/fuzzer/agentic/flow.py. Use accepted target input-location and "
        "injection-harness context to replace target-dependent input memory "
        "write and taint code. Output Python code only."
    )
    return prompt_llm(
        llm_handle,
        build_triton_snippet_prompt(points, input_location, generated_harness),
        system_prompt=system_prompt,
    )


def print_triton_target_snippets(snippets, output_fn=print):
    output_fn("\nCopy/paste flow.py Triton target snippets:")
    output_fn(_strip_markdown_code_fence(str(snippets)))


def default_harness_path(points):
    return DEFAULT_PROTOCOL_FUZZERS_DIR / f"{_c_identifier(points.function_name)}.c"


def _strip_markdown_code_fence(code):
    stripped = code.strip()
    if not stripped.startswith("```"):
        return stripped

    lines = stripped.splitlines()
    if lines and lines[0].startswith("```"):
        lines = lines[1:]
    if lines and lines[-1].strip() == "```":
        lines = lines[:-1]
    return "\n".join(lines).strip()


def _strip_callback_typedef(code):
    return re.sub(
        r"^\s*typedef\s+void\s*\(\s*\*\s*fuzz_callback_t\s*\)\s*\(\s*\)\s*;\s*\n?",
        "",
        code,
        flags=re.MULTILINE,
    ).strip()


def build_harness_file_contents(generated_harness):
    generated = _strip_callback_typedef(_strip_markdown_code_fence(generated_harness))
    return f"""#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz.h"

typedef void (*fuzz_callback_t)();

{generated}
"""


def choose_harness_output_path(points, input_fn=input, output_fn=print):
    default_path = default_harness_path(points)

    while True:
        raw_path = input_fn(
            f"\nOutput C file path, or press Enter for default [{default_path}]: "
        ).strip()
        path = Path(raw_path).expanduser() if raw_path else default_path
        if not path.is_absolute():
            path = path.resolve()

        if not path.exists():
            return path

        overwrite = input_fn(f"{path} exists. Overwrite? [y/N]: ").strip().lower()
        if overwrite in {"y", "yes"}:
            return path

        output_fn("Choose a different path, or press Enter to use the default.")


def write_harness_file(points, generated_harness, input_fn=input, output_fn=print):
    path = choose_harness_output_path(points, input_fn=input_fn, output_fn=output_fn)
    contents = build_harness_file_contents(generated_harness)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")
    log.info("wrote %s", path)

    return path


def _hex_addr(address):
    return hex(int(address))


def _unique_instruction_points(points):
    seen = set()
    unique = []
    for point in points:
        if point.address in seen:
            continue
        seen.add(point.address)
        unique.append(point)
    return unique


def build_toml_entries(points):
    entry = _hex_addr(points.entry.address)
    exits = _unique_instruction_points(points.exits)

    chunks = [
        f"""[[CPU.cpu0.virtuals]]
at = "{entry}"
instruction = "fuzz_snap_point"
args = []"""
    ]

    for point in exits:
        exit_addr = _hex_addr(point.address)
        chunks.append(
            f"""[[CPU.cpu0.virtuals]]
at = "{exit_addr}"
instruction = "fuzz_sync_point"
args = []"""
        )
        chunks.append(
            f'''[[CPU.cpu0.modifiers]]
at = "{exit_addr}"
patch = "r15 {entry}"'''
        )

    return "\n\n".join(chunks)


def print_toml_entries(points, output_fn=print):
    output_fn("\nCopy/paste TOML entries:")
    output_fn(build_toml_entries(points))


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", help="Path to binary to analyze")
    parser.add_argument(
        "--work-dir",
        default=DEFAULT_WORK_DIR,
        help=f"FastDyn work directory used for generated state. Default: {DEFAULT_WORK_DIR}",
    )
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help=f"LLM model name. Default: {DEFAULT_MODEL}",
    )
    parser.add_argument(
        "--base-url",
        default=os.environ.get("OPENAI_BASE_URL", DEFAULT_BASE_URL),
        help=f"LLM base URL. Default: $OPENAI_BASE_URL or {DEFAULT_BASE_URL}",
    )
    return parser.parse_args()


def run_harness(program, llm_handle):
    candidates = parsers.parser_search(program)
    selected = select_function(candidates)
    if selected is None:
        return None

    points = points_of_interest(program, selected)
    input_location = review_input_location_loop(llm_handle, points)
    generated_harness = review_injection_harness_loop(llm_handle, points, input_location)
    path = write_harness_file(points, generated_harness)
    print_toml_entries(points)
    triton_snippets = generate_triton_target_snippets(
        llm_handle,
        points,
        input_location,
        generated_harness,
    )
    print_triton_target_snippets(triton_snippets)
    return path


def main():
    args = parse_args()
    work_dir = Path(args.work_dir).resolve()
    work_dir.mkdir(parents=True, exist_ok=True)

    general_llm = initialize_llm(
        model=args.model,
        base_url=args.base_url,
        storage_dir=work_dir / "agentic-crewai-data",
    )
    with_analyzed_program(
        args.binary,
        lambda program: run_harness(program, general_llm),
        project_dir=work_dir / "ghidra_projects",
    )


if __name__ == "__main__":
    main()
