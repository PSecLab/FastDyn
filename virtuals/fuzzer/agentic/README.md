# Agentic fuzzing

This directory contains FastDyn's agent-assisted, branch-targeted fuzzing pipeline. It adds two pieces to the normal FastDyn/LibAFL fuzzer:

1. An interactive harness generator that uses Ghidra and an LLM to identify an input-processing function, generate a C input-injection callback, and propose the required FastDyn hooks and Triton setup.
2. A runtime monitor that watches coverage-increasing LibAFL inputs, finds nearby uncovered branches, optionally identifies the input bytes that affect those branches with Triton, and asks an LLM to produce focused mutations. Those mutations are fed back into LibAFL.

This document assumes that normal FastDyn, QEMU, and firmware setup is already working. See the parent [fuzzer README](../README.md) and [LibAFL backend README](../fastdyn_fuzz_lib/README.md) for the non-agentic build details, building LibAFL is required.

## Current constraints

Read these before setting up a campaign:

- Run commands from the FastDyn repository root.
- Much of the code currently uses `./fastdyn_work` in C and Rust, and should not be overriden without an explicit reason
- The current taint implementation is ARM32-specific and expects an ELF file. Its target-dependent input location must match the C injection harness.
- The C fuzzer has one stateless snap callback. A campaign therefore selects one generated injection callback in `fuzz.c` at a time.
- Harness generation writes the accepted C file, but only prints the TOML and `flow.py` snippets. Save or copy those printed snippets before closing the terminal.
- Ghidra 12.0 is the version used during development and testing.
- `ollama/qwen3:30b` is the smallest model that was reliably functional for harness generation in testing and was a good baseline for stable performance on average targets. Smaller models usually performed poorly on input-location inference and code generation. Larger models may improve results.

## Prerequisites specific to this pipeline

The Python packages used by the pipeline are included in `requirements.txt`: PyGhidra, CrewAI, Triton, NetworkX, pydot, pyelftools, and their supporting packages. Make sure that the Python interpreter used by both `fastdyn` and the child monitor has these packages.

### Ghidra

Install Ghidra 12.0 and point PyGhidra at its top-level installation directory:

```sh
export GHIDRA_INSTALL_DIR=/absolute/path/to/ghidra_12.0_PUBLIC
```

The variable must be exported in the shell that starts `fastdyn`; the runtime monitor inherits that environment. If it is missing, the monitor fails in the background and the error appears in `fastdyn_work/agentic_fuzz-monitor.log` rather than necessarily appearing in the main terminal.

The first analysis of a large firmware can take several minutes. The result is cached in `fastdyn_work/ghidra_projects` and reused while that directory is preserved.

For this reason, it is recommended to run fastdyn in persistent mode with -p when re-running on the same target to avoid this startup cost.

### Ollama and the model

The default endpoint is `http://localhost:11434`. Pull the Ollama model name without CrewAI's `ollama/` provider prefix:

```sh
ollama pull qwen3:30b
```

If Ollama does not start as a service on the host, keep it running in another terminal:

```sh
ollama serve
```

Start the server before `ollama pull` if the pull command cannot connect.

FastDyn/CrewAI receives the provider-qualified name:

```text
ollama/qwen3:30b
```

Harness generation supports a custom endpoint only when invoking `harness.py` directly with `--base-url`:

```sh
python virtuals/fuzzer/agentic/harness.py \
  virtuals/physics/flight_controllers/courbet/bin/ardurover_v462.stripped \
  --model ollama/qwen3:30b \
  --base-url http://model-host:11434
```

The integrated runtime monitor currently uses the local default endpoint.

### LibAFL and FastDyn builds

Build `virtuals/fuzzer/fastdyn_fuzz_lib` in release mode as described in its README, then build FastDyn with the LibAFL backend enabled. From the FastDyn root, a configured tree can be rebuilt with:

```sh
make
```

For a fresh FastDyn configuration, ensure `enable_libfuzz` is true; the repository Makefile exposes this as `LIBFUZZ`:

```
LIBFUZZ ?= true
```

Rebuild FastDyn after adding or changing a generated C harness, its header declaration, its Meson source entry, its callback registration, or the snapshot-delay code. Changes confined to the Python files do not require rebuilding the plugin.

## End-to-end setup

This setup will refer to the example harness setup for ardupilot's rover, which can be seen in configs/rover462_fuzz.toml. The target function for this is GCS_MAVLINK::packetReceived at 0x808b038.

### 1. Start Ollama and export Ghidra's location

For example:

```sh
cd /path/to/FastDyn
export GHIDRA_INSTALL_DIR=/opt/ghidra_12.0_PUBLIC
ollama pull qwen3:30b
```

Run `ollama serve` in another terminal if the server is not already active.

### 2. Generate the harness

The rover 4.6.2 example used during development is:

```sh
fastdyn harness \
  -b virtuals/physics/flight_controllers/courbet/bin/ardurover_v462.stripped \
  -m ollama/qwen3:30b
```

The command defaults to `fastdyn_work` and performs these interactive steps:

1. Import and analyze the binary in a PyGhidra project.
2. Rank parser-like functions. It is based on heuristics, the results are not a declaration that a function is the firmware's true input boundary.
3. Show candidates ten at a time. Enter a result index, an address of a known target function such as `0x808b038`, or press Enter for more results.
4. Ask the LLM where the raw input exists at the selected function's exact entry and how large it is. Press Enter to accept, or type concrete feedback to revise the answer. Depending on the function, the whole input may not be used which doesn't give the LLM good information, and may require some feedback to update the input size. A correct size is essential for the harness to work its best.
5. Generate a C injection callback. Again, press Enter to accept or provide correction text.
6. Ask where to write the C file. The default is `virtuals/fuzzer/protocol_fuzzers/<function_name>.c`.
7. Print TOML snap/sync hooks and target-dependent Triton snippets for `flow.py`.

Inspect every LLM result before accepting it. In particular, verify the calling convention in the entry disassembly, the input pointer or direct register values, the length field, the amount of memory written, and every generated exit address.

#### Selecting a better function manually

The ranked candidates are often input-processing functions slightly below the real input boundary. That can be useful because they expose parsing logic, but it may be difficult to reach them to snapshot state without running inputs reaching that code first.

In addition, selecting a bad function can cause problems, especially if it is especially large, confusing the LLM. Use these as a reference to help find the input source if you are able.

If a candidate is too deep in the call chain, use it as a Ghidra reference, walk upward to a caller where the raw buffer and length are still available, and rerun the generator by entering that caller's address directly. Conversely, if a known input source is too broad, enter a specific downstream function address and use the revision prompts to tell the LLM which input object matters.

Examples of useful feedback are:

```text
The bytes are pointed to by r2 at entry; r1 is unrelated state. The packet is exactly 291 bytes.
```

```text
I want to fuzz the payload reached through this wrapper, not the command ID passed directly in r0. Follow the object pointer loaded from r2.
```

Choose a point that is both semantically useful and mechanically repeatable: the rehosting should be able to initially reach the function, and execution must reach a sync point after each input.

### 3. Integrate the generated C callback

Assume the generated file is `protocol_fuzzers/packetreceived.c` and its callback is:

```c
void fuzz_packetreceived_inject(void);
```

Complete all three integration changes:

1. Add the C file to `protocol_fuzzers/meson.build` inside the `ENABLE_LIBFUZZ` source list:

   ```meson
   if ENABLE_LIBFUZZ
       srcs += files(
           'packetreceived.c',
       )
   endif
   ```

2. Add the callback prototype to `protocol_fuzzers/protocol_fuzzers.h`:

   ```c
   void fuzz_packetreceived_inject(void);
   ```

3. Select that callback in `fuzz_init()` in `fuzz.c:812`:

   ```c
   fuzz_register_snap_callback(fuzz_packetreceived_inject);
   ```

Only one callback can be registered this way. Replace the previous target's registration rather than registering several callbacks and assuming they will all run.

The callback runs at each `fuzz_snap_point` after the backend has made an input available. It normally calls `fuzz_get_data`, then writes bytes through `fuzz_write_memory` and/or updates entry registers with `fuzz_set_register`.

#### Target-specific validation

Generated code is a starting point. Common manual changes include:

- Correcting a pointer register or input length.
- Limiting the generated buffer to the parser's actual maximum. Unknown sizes default to 4096 bytes in generated code, which is not automatically safe for the target object.
- Preserving required fixed headers while fuzzing only a payload range.
- Repairing a checksum, length, magic, or sequence field after obtaining fuzz data.

Many firmware formats have validity rules that are intentionally not inferred automatically. For a checksum-protected input, the simplest approaches are usually to bypass the checksum check with a modifier or to calculate and insert a valid checksum in the injection callback. The same principle applies to cryptographic tags, redundant lengths, framing bytes, and state counters.

### 4. Install the generated Triton hooks

At the top of `agentic/flow.py`, replace the target-dependent value and function bodies identified by the comments `Input from harness generation`:

```python
TRITON_TARGET_INPUT_SIZE = 291

def triton_write_target_input(ctx, snap, input_bytes):
    base = snap["registers"][2]
    _set_triton_memory(ctx, base, triton_target_input_bytes(input_bytes))

def triton_taint_target_input_range(ctx, snap, start, end):
    base = snap["registers"][2]
    for offset in triton_target_input_range(start, end):
        ctx.taintMemory(MemoryAccess(base + offset, CPUSIZE.BYTE))
```

This is the ardupilot rover example: the 291-byte object is pointed to by `r2` at the snapshot. Use the snippets printed for the selected target.

Keep the generic helper functions such as `triton_target_input_bytes`, `triton_target_input_range`, and `_set_triton_memory`. The Triton hooks must reproduce the C harness's logical input placement exactly for correct taint tracking.

If these disagree, taint may report no relevant bytes or may attribute a branch to the wrong bytes. Set `agentic_fuzz_taint=false` temporarily when bringing up a harness if Triton setup is not ready yet, or if problems are encountered with running taint.

The process can work without the taint analysis in cases where taint fails for some given reason, however the LLM assisted mutations will have less context, and less relevant branches may be chosen.

### 5. Add snap, sync, and control-flow hooks to the TOML

The generated entries use the selected function's entry as the snapshot/injection point and each detected function exit as a sync point. A minimal shape is:

```toml
[[CPU.cpu0.virtuals]]
at = "0x808b038"
instruction = "fuzz_snap_point"
args = []

[[CPU.cpu0.virtuals]]
at = "0x808b0b8"
instruction = "fuzz_sync_point"
args = []

[[CPU.cpu0.modifiers]]
at = "0x808b0b8"
patch = "r15 0x808b038"
```

The sync hook tells LibAFL that the current input completed, requests/restores the next iteration, and the modifier sends execution back to the snap point. If any real return, tail call, error path, or long-running path bypasses the sync point, the campaign can freeze waiting for completion.

The relevant portion of `configs/rover462_fuzz.toml` is:

```toml
[Machine]
coverage = true
agentic_fuzz = true
agentic_fuzz_taint = true
agentic_fuzz_model = "ollama/qwen3:30b"

[[CPU.cpu0]]
binary = "virtuals/physics/flight_controllers/courbet/bin/ardurover_v462"

[[CPU.cpu0.virtuals]]
at = "0x8004280"
instruction = "assert"
args = ["*0"]

[[CPU.cpu0.virtuals]]
at = "0x808b038"
instruction = "fuzz_snap_point"
args = [""]

[[CPU.cpu0.virtuals]]
at = "0x808b0b8"
instruction = "fuzz_sync_point"
args = [""]

[[CPU.cpu0.modifiers]]
at = "0x808b0b8"
patch = "r15 0x808b038"

# Rover-specific shortcuts/escape paths, not generic generated entries.
[[CPU.cpu0.modifiers]]
at = "0x80144b4"
patch = "r15 0x808b038"

[[CPU.cpu0.modifiers]]
at = "0x8005454"
patch = "r15 0x808b0b8"
```

`existing_config_path` imports the target's existing `virtuals.txt` and `modifiers.txt`; the TOML entries are added to them. Check the resulting merged files in `fastdyn_work/virtuals` when debugging duplicate or conflicting hooks.

For a fatal crash hook, `assert` expects an argument beginning with `*`; `*0` reports a fatal assertion, while a nonzero address reports a recoverable assertion and redirects the PC there. Configure this deliberately for the target rather than copying the rover placeholder blindly:

```toml
[[CPU.cpu0.virtuals]]
at = "0x8004280" # hard-fault handler entry for this example only
instruction = "assert"
args = ["*0"]
```

### 6. Configure the agentic runtime

These `[Machine]` settings are recognized:

| Setting | Default | Purpose |
| --- | --- | --- |
| `coverage` | `false` | Must be `true`; the agentic pipeline requires FastDyn coverage. |
| `agentic_fuzz` | `false` | Starts the Python monitor as a child of the QEMU/plugin process. |
| `agentic_fuzz_taint` | `true` | Uses Triton to reject untainted frontier branches and report influential byte ranges. |
| `agentic_fuzz_model` | `ollama/qwen3:14b` | CrewAI model used for branch-targeted runtime mutations, does not need to be as powerful as the model for harness generation. |
| `agentic_fuzz_python` | `python3` | Interpreter used for the child monitor. Use an absolute virtual-environment path if `python3` does not contain the required packages. |
| `agentic_fuzz_script` | bundled `monitor.py` | Advanced override for the monitor script. |
| `agentic_fuzz_in_dir` | `corpus` under the work directory | Directory containing `interestingN.raw`/`.trace` pairs. The LibAFL producer is currently hard-coded to `fastdyn_work/corpus`. |
| `agentic_fuzz_out_dir` | `corpus-agentic` under the work directory | Directory for `mutatedN.raw`. The LibAFL importer is currently hard-coded to `fastdyn_work/corpus-agentic`. |

If FastDyn itself is installed in a virtual environment but the child monitor reports missing modules, set:

```toml
agentic_fuzz_python = "/absolute/path/to/venv/bin/python"
```

Do not change the in/out directories independently unless the matching C/Rust paths are changed too; otherwise the monitor and LibAFL will quietly watch different directories.

### 7. Build and run

After integrating the C files and rebuilding, run from the FastDyn root:

```sh
fastdyn run -c configs/rover462_fuzz.toml -p
```

The harness command creates `fastdyn_work`, so `-p` preserves its Ghidra analysis. For a genuinely fresh campaign, run without `-p`; FastDyn deletes and recreates the work directory. On every later restart where campaign state should survive, use `-p`:

```sh
fastdyn run -c configs/rover462_fuzz.toml -p
```

During the first coverage-enabled run, FastDyn may prompt for writable ELF segments to include in `bin-writable-ranges`. Include the RAM segments needed by the parser and its state. Selecting every writable segment is the usual starting point, but very large, aliased, or invalid regions should be removed to improve performance. Each line is an address and byte size:

```text
0x20000000    0x80000
```

If this file is edited, delete the old `snapshot.bin` before restarting so the snapshot layout is regenerated consistently.

The monitor's stdout and stderr go to:

```sh
tail -f fastdyn_work/agentic_fuzz-monitor.log
```

Use this log to verify Ghidra startup, CFG creation, input selection, taint decisions, prompts, LLM responses, and failures. The main FastDyn terminal primarily shows QEMU and LibAFL activity.

## What happens during a campaign

The feedback loop is:

```text
LibAFL input -> snap callback injects bytes -> firmware executes -> sync point
      |                                                        |
      +---- new coverage: save interestingN.raw + .trace <-----+
                               |
                         agentic monitor
                               |
             choose uncovered neighboring branch
                               |
              Triton taint + Ghidra context + LLM
                               |
                    write mutatedN.raw
                               |
                         LibAFL imports it
```

More precisely:

1. LibAFL performs ordinary havoc mutation and coverage feedback.
2. The first snap point loads an existing compatible snapshot or saves registers and writable memory to `snapshot.bin`, obtains an input, and calls the selected C injection callback.
3. A sync point completes the input and restores the saved state for the next iteration.
4. When an input increases coverage, LibAFL runs it once with tracing enabled and writes a paired `interestingN.raw` and `interestingN.trace`.
5. The monitor maps QEMU translation-block coverage and traces onto Ghidra basic blocks.
6. It identifies edges from reached blocks to globally uncovered blocks and favors destinations with more uncovered descendants.
7. With taint enabled, Triton reconstructs the snapshot and input placement, discards candidate branches not influenced by the input, and reports byte ranges affecting the selected branch.
8. Ghidra decompilation, branch assembly, the desired destination, the current byte array, and any taint range are sent to the mutation agent.
9. The LLM's byte-array response is encoded to `corpus-agentic/mutatedN.raw`.
10. LibAFL evaluates each new agentic input and writes `mutatedN.raw.interesting` containing `1` if it entered the corpus/solutions or `0` otherwise as a basic feedback mechanism to the user.

An agentic mutation is therefore a candidate, not automatically a finding. The `.interesting` marker and subsequent coverage/crash artifacts show whether it helped.

### Inspecting and using the output

For testing a raw candidate or crash into the same byte-array format shown to the LLM with:

```sh
python virtuals/fuzzer/agentic/target.py decode \
  fastdyn_work/corpus-agentic/mutated0.raw
```

After editing a saved array, encode it again with:

```sh
python virtuals/fuzzer/agentic/target.py encode input.txt replay.raw
```

In future revisions target specific schemas will be generated to improve this feedback.

Treat a raw file as a reproducer only together with the firmware binary, TOML, generated callback, `flow.py` target hooks, and snapshot/campaign state that produced it. Check `crashes/`, `fuzzer.log`, and `timeout.log` for objective results.

Timeouts and crashes should be carefully inspected for rehosting or harness related problems.

## Work-directory reference

The following files and directories are relevant to agentic fuzzing:

| Path under `fastdyn_work` | Owner and purpose | Safe handling |
| --- | --- | --- |
| `ghidra_projects/` | Shared PyGhidra project, normally `test.gpr` plus `test.rep`; caches imported programs and analysis. | Preserve to avoid reanalysis. Do not open concurrently from multiple pipeline processes. |
| `agentic-crewai-data/` | Local CrewAI/XDG state for harness and mutation agents. | Usually preserve; not campaign coverage state. |
| `virtuals/virtuals.txt` | Merged virtual hooks from `existing_config_path` and TOML. | Inspect to confirm snap, sync, assert, and target virtuals. Regenerated by FastDyn. |
| `virtuals/modifiers.txt` | Merged PC/register modifiers. | Inspect to confirm all exits and escape paths. Regenerated by FastDyn. |
| `bin-writable-ranges` | Address/size pairs included in each memory snapshot. | Regenerate after changing the ELF or memory layout. Editing it invalidates the old snapshot. |
| `snapshot.bin` | Sixteen little-endian ARM register values followed by bytes for every writable range. Used by both the C restore path and Triton. | Remove/regenerate after changing binary, snap point, or writable ranges. A same-sized stale snapshot may not be detected automatically. |
| `bbl.txt` | Cumulative reached translation blocks, instruction counts, and timestamps. Used as global coverage by the monitor. | Preserve for campaign continuation; reset for a genuinely new coverage campaign. |
| `cfg_bb.dot` | Ghidra basic-block CFG generated by the monitor. | Diagnostic/visualization output; safe to regenerate. |
| `cfg_func.dot` | Function-call CFG produced only by the optional `monitor.py analyze --dump-cfg` path. | Diagnostic output; not required by a normal campaign. |
| `corpus/interestingN.raw` | Coverage-increasing input captured by LibAFL. | Keep with its same-numbered trace. |
| `corpus/interestingN.trace` | QEMU PC trace for the matching raw input. | The monitor ignores an input until both files exist. |
| `corpus-agentic/mutatedN.raw` | LLM-generated candidate fed back into LibAFL. | May be replayed or inspected as raw bytes. |
| `corpus-agentic/mutatedN.raw.interesting` | One byte of text, `1` or `0`, recording LibAFL's evaluation. | Result marker only. |
| `agentic-input.txt` | Scratch text containing the selected raw input as an editable byte array, then the latest LLM response. | Useful for diagnosing malformed or unexpectedly shortened model output. Overwritten repeatedly. |
| `agentic_fuzz-monitor.log` | All runtime monitor, PyGhidra, Triton, CrewAI, and LLM output. | First place to inspect when agentic output stops. |
| `state.bin` | Serialized LibAFL campaign state. | Clear if LibAFL runs into strange errors on startup. |
| `crashes/` | LibAFL objective corpus and metadata. | Treat as findings to triage and reproduce. |
| `fuzzer.log` | Inputs associated with fatal errors. | Diagnostic evidence. |
| `timeout.log` | Inputs that exceeded the backend's current 60-second completion timeout. | Diagnose missed sync points and firmware hangs before treating every entry as a real target timeout. |
| `cvg.bin` | Serialized edge coverage written when the fuzzer exits. | Diagnostic campaign state. |
| `qemu.log` | QEMU log selected by the TOML `log_options`. | Can become very large and hurt throughput when verbose instruction/TCG logging is enabled. |
| `dev_config.json` | FastDyn's generated device-model configuration passed to the plugin. | Standard FastDyn artifact; regenerate from TOML rather than editing it for agentic behavior. |
| `qmp.sock` | Per-run QEMU Machine Protocol socket. | Standard transient QEMU artifact. A live socket usually means an old QEMU process should be checked before restart. |
| `gdb_init.txt` | Generated GDB connection script when GDB support is enabled. | Standard FastDyn artifact, unrelated to agent decisions. |

Temporary `.mutatedN.raw.tmp` files may remain after an interrupted encode; only completed `.raw` files are imported.

## Target-specific control-flow techniques

Agentic harness generation finds a likely local function boundary. Real firmware often needs additional shortcuts to make that boundary more repeatable.

### Jump directly from startup or `main` to a handler

Once the initial snapshot is take, it is safe to skip to the target fuzzing function, which can save time and make testing more forgiving. Resolve both addresses in Ghidra or a symbol map, then add a PC modifier at the concrete `main` address:

```toml
[[CPU.cpu0.modifiers]]
at = "0x08001234"       # main
patch = "r15 0x0808b038" # selected handler/parser entry
```

This skips initialization, so it is only valid if the snapshot and injection callback construct everything the handler needs. A direct jump that leaves object pointers, stack state, globals, or length arguments uninitialized will produce misleading coverage or faults.

### Escape a freezing ISR or error path

If execution enters a handler such as `port_isr` and never returns, redirect it to the sync point:

```toml
[[CPU.cpu0.modifiers]]
at = "0x08005454"       # freezing handler entry
patch = "r15 0x0808b0b8" # fuzz_sync_point
```

Use the sync point, not the snap point. Sync completes the current backend iteration and requests/restores the next input. Jumping directly to snap can inject again without completing the old input and leave LibAFL waiting forever.

Whenever fuzzing halts and output halts unexpectedly, use the built in gdb connection to debug where it gets stuck.

### Disable or bypass interrupts

Frequent interrupts in some firmware can lower fuzzer throughput and decrease stability, non-essential interrupts to the fuzzing campaign should be disabled

- Stub a known, irrelevant ISR by returning from its entry when that return behavior is valid for the firmware:

  ```toml
  [[CPU.cpu0.modifiers]]
  at = "0x0800abcd"
  patch = "r15 r14"
  ```

- If an ISR represents the end of a failed or hung test iteration, redirect it to the sync point instead.

Do not suppress interrupts that initialize or deliver the input state the selected parser requires, or that report faults. Confirm the resulting path in `qemu.log`, GDB, or the captured traces.

### Parser does not return cleanly

The generated sync points are based on static function exits. The sync point simply needs to be at a common post-processing point, if the actual target loops, tail-calls, throws into an error handler, or returns through a wrapper, manually place it at a better post-processing point.

## Snapshot timing and state

By default the first visit to `fuzz_snap_point` currently snapshots at its first execution, which will almost always be the intended behavior. However, in some targets, especially those that call input handlers asynchronously, it is possible that initialization is not complete by the first call. `fuzz_snap_point` has a built in function that causes it to wait until x seconds have passed since first call before it takes the snapshot. This can be modified in the following function:

```c
fuzz_snap_init_timeout(0)
```

If temporarily increasing this to cause a later snapshot, change this to the number of seconds you would like to delay, rebuild, and do a run without any fuzzing virtuals or modifiers, only run to take the snapshot. Once this snapshot is taken, return the delay to zero and re-enable fuzzing related virtuals.

## Troubleshooting

### The main terminal runs, but no agentic mutations appear

Check:

```sh
tail -f fastdyn_work/agentic_fuzz-monitor.log
```

Common causes are an unset `GHIDRA_INSTALL_DIR`, Ollama not running, the model not pulled, the child `python3` missing dependencies, or the monitor waiting for files that the harness has not produced yet.

### The monitor waits for `bbl.txt`

Confirm `coverage=true`, that FastDyn was built with LibAFL support, that the campaign uses the default `fastdyn_work`, and that firmware execution reaches instrumented code. Inspect `fastdyn_work/virtuals/virtuals.txt` for the expected hooks.

### The monitor waits for `snapshot.bin`

This wait occurs only with taint enabled. The firmware has not reached a working `fuzz_snap_point`, snapshot ranges could not be parsed, or runtime files are split across work directories. Fix the snap path or temporarily set `agentic_fuzz_taint=false` to debug the rest of the loop.

### There are no `interestingN.raw`/`.trace` pairs

LibAFL has not yet observed new coverage, or the input never completes at a sync point. Verify that the C callback consumes and injects data, execution leaves the target, and every path reaches sync. Seed generation occurs automatically when the LibAFL corpus is empty.

### Inputs time out or the campaign freezes

The backend completion timeout is currently 60 seconds. The usual cause is a missed sync path, a blocking parser, or an interrupt/error handler that never returns. Add or move a sync hook, and redirect known freezing paths to the sync point to cause those runs to end instead of freeze.

### Taint rejects every branch

First verify that `flow.py` writes the input to exactly the same location as the C harness. Check the register at the snapshot, target size, and whether the snapshot occurs before or after the relevant prologue. If continuously running into this error, it is possible that the input dependent paths have been mostly exhausted in many inputs, or that the taint engine is not able to track the flow. Consider running with `agentic_fuzz_taint=false` to run without the taint engine.

### Ghidra reports a locked project

An abruptly stopped monitor can leave the project open. Find and terminate the actual stale `monitor.py`, `harness.py`, PyGhidra/Python, or Java process first. The QEMU process normally owns the monitor child, so stop the old QEMU campaign as well. Only after confirming that no process is using the project should stale `fastdyn_work/ghidra_projects/test.lock*` files be removed. Deleting a live lock risks project corruption.

### The LLM response is malformed or unexpectedly short

Inspect `agentic-input.txt` and the monitor log. `target.py` accepts Python-style arrays, decimal/hex byte runs, byte strings, and compact hex, clamps values to bytes, and chooses the longest parseable candidate. That tolerance cannot recover bytes the model omitted. A `.interesting` value of `0` is normal for an unsuccessful but well-formed mutation.

### A custom work directory behaves inconsistently

Return to the FastDyn root and use `fastdyn_work`. Current Python paths are configurable, but snapshot, coverage, LibAFL state, source corpus, and agentic import paths still include hard-coded `fastdyn_work` values in native code.

### The fuzzer starts but immediately freezes

Loading a large corpus can sometimes cause problems for LibAFL at the moment, consider trimming down the corpus size if possible, or starting from a clean run

### No interesting LLM mutations

The goal of the LLM assisted mutations is to help increase the coverage when the fuzzer on its own has difficulty satisfying some check, such as a branch requiring many specific values to line up. However, many branches depend on global state, or much more complicated checks such as checksums or other math heavy operations. In these cases, LLMs will also often fall short, which can lead to these problems.

In addition, the limitation of the context available to the LLM can also limit its reasoning over how the input is processed. The main focus in future revision is in this area of providing better context to the LLM, and giving it the ability to see the results of its own input so it can revise, instead of the current process of giving it one try.

## Source-file reference

| File | Responsibility | Normally target-specific? |
| --- | --- | --- |
| `harness.py` | Interactive Ghidra/LLM workflow; writes C callback and prints TOML/Triton snippets. | No, although prompt/generation policy can be tuned here. |
| `parsers.py` | Scores and ranks parser-like functions using Ghidra decompiler structure. | No. |
| `monitor.py` | Runtime orchestrator; watches paired inputs/traces, builds the basic-block CFG, and invokes branch selection and mutation. | No. |
| `selection.py` | Chooses underused inputs and high-impact uncovered branch edges; invokes Triton filtering. | Usually no; branch scoring can be experimented with here. |
| `flow.py` | Reconstructs concrete ARM32 execution and performs Triton taint analysis. | **Yes:** input size, write location, and taint location at the top must match the harness. |
| `context.py` | Builds the LLM report from the byte array, taint ranges, target assembly, destination, and marked decompilation. | No. |
| `agent.py` | Defines the CrewAI branch-targeted mutation agent and local Ollama defaults. | Model/prompt policy may be tuned here; set the model in TOML for normal use. |
| `target.py` | Converts raw files to editable byte arrays and forgivingly converts LLM output back to raw bytes. | Extend this if a campaign needs a structured, non-byte-array representation. |

Outside this directory, the most relevant integration points are `virtuals/fuzzer/fuzz.c`, `virtuals/fuzzer/protocol_fuzzers/`, `virtuals/fuzzer/fastdyn_fuzz_lib/src/lib.rs`, `src/fastdyn/targets/qemu_target.py`, and the campaign TOML.
