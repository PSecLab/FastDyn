# FastDyn Fuzzing Implementation

This is the directory for the current fuzzing implementation, which will be updated as we test the fuzzer on more interesting/complicated firmwares, and add/update the backends.

## Overview

To start, this is the guide for manually setting up the fuzzer to run.

fuzz.c is the core of the fuzzer, which is meant to act as a generic interface between the fuzzing backend and the model/firmware that is acting as the fuzzing harness. Currently, we support libAFL and a modified version of AFLNet as the backends for this fuzzer, which can be compiled in in the Makefile depending on the usecase. libAFL is good for producing a generic, single chunk per iteration, and is easier to get going on a new target. AFLNet is better for stateful protocols with a trace of messages, but requires a bit more work for a new protocol, which involves adding in support for that protocol into the modified AFLNet. We have currently added Ethernet and Modbus support in AFLNet which can be used as a reference along with the existing protocols

To use the fuzzers, the path to the built fuzzing backend must be included in the relevant environment variables along with other FastDyn requirements, with the following as a reference at the time of writing:
```sh
export LD_LIBRARY_PATH=/path/to/FastDyn/build:/path/to/FastDyn/device_models/postmartem/verifier:/path/to/FastDyn/virtuals/fuzzer/fastdyn_fuzz_lib/target/release
export PATH=/path/to/qemu/build:$PATH
export PATH=$PATH:/path/to/aflnet
```

## libAFL

### Build

To build the libAFL implementation, cd to the root folder of the project, and run the docker script with the following command, with elevated permissions if necessary:
```sh
./virtuals/fuzzer/fastdyn_fuzz_lib/run_docker.sh 
```

Once in the docker container, build the release version with the following command
```sh
cargo build --release
```
If you wish to compile with a different mode, make sure that the relevant path is updated in LD_LIBRARY_PATH

Then, in the Makefile, make sure to set the relevant flag
```Makefile
LIBFUZZ		 ?= true
```

to use the libAFL backend, along with enabling
```toml
coverage = true
```
in the relevant .toml configuration file

## AFLNet

As mentioned, we have a custom implementation of AFLNet. This implementation can be accessed at https://anonymous.4open.science/r/aflnet/README.md

### Build

To build the AFLNet implementation, cd into the root of the downloaded AFLNet backend used, and build with
```sh
make clean libaflnet.a
```

Then, in the Makefile, make sure to set the relevant flag
```Makefile
AFLNET 		 ?= true
```

## Generic Usage

The generic fuzzer obtains one input for each iteration and writes contiguous
parts of that input to the fields in a JSON schema. It is configured entirely
from the target TOML and does not need a target-specific injection callback.

Currently this supports libAFL

### Fuzzing Virtuals

The generic loop uses three virtual instructions:

- `fuzz_state_point` some firmware may have a long startup or a difficult to
  reach fuzzing target. In these cases, the state point is meant to be a
  more complete version of the snapshot that allows a fresh run to be started
  at a chosen point. It simply needs a previous run to have reached that point
  for the snapshot to be taken, subsequent runs can then begin there, skipping
  initialization. May not be necessary on all targets.
- `fuzz_snap_point` captures the per-input snapshot on its first visit. On
  every visit it restores the snapshot as necessary, obtains the next input,
  and invokes the generic schema writer.
- `fuzz_sync_point` marks the end of processing for one input. It records
  coverage, requests the next input, and restores the snapshot for the next
  iteration.

Choose a snap point immediately before the code that consumes the fuzzed data,
and a sync point after that code has completed. Redirect execution from the
sync point back to the snap point with a modifier. The redirected instruction
must be safe to skip; function epilogues are a common choice when the modifier
updates `r15` before the epilogue executes.

```toml
[Machine]
coverage = true
fuzzing = true
fuzzing_schema = "path/to/schema.json"

[[CPU.cpu0.virtuals]]
at = "0x08001000"
instruction = "fuzz_state_point"
args = []

[[CPU.cpu0.virtuals]]
at = "0x08002000"
instruction = "fuzz_snap_point"
args = []

[[CPU.cpu0.virtuals]]
at = "0x08002080"
instruction = "fuzz_sync_point"
args = []

[[CPU.cpu0.modifiers]]
at = "0x08002080"
patch = "r15 0x08002000"
```

The state point is normally reached once. The snap and sync points then form
the persistent loop: **snap → inject → target code → sync → snap**.

### Schema

`fuzzing_schema` names a JSON file with one top-level `fields` array. Each
field has exactly four properties: `name`, `location`, `type`, and `size`.
Fields consume input in array order, so the total requested fuzz input is the
sum of their sizes.

```json
{
  "fields": [
    {
      "name": "message",
      "location": "r2",
      "type": "random",
      "size": 291
    },
    {
      "name": "mode",
      "location": "reg(0)",
      "type": "random",
      "size": 4
    }
  ]
}
```

`random` is the only type currently supported. It copies the field's next
`size` bytes from the fuzz input to its resolved `location`, which can be
an expression, detailed blow.

Locations are evaluated once, when the schema is first loaded at the snapshot
point. This intentionally freezes register-derived addresses and pointer
chains for the campaign.

| Location | Meaning |
| --- | --- |
| `reg(1)` | Fuzz register `r1` itself. This form is valid only as the complete location. |
| `0x20001000` | Fuzz memory at an absolute address. |
| `r2` | Read `r2` and fuzz memory at the address it contains. |
| `r2 + 0x10` | Fuzz memory 16 bytes into the buffer addressed by `r2`. |
| `[r3]` | Read an unsigned 32-bit value from memory at `r3`; fuzz memory at the resulting address. |
| `u16[r3 + 6]` | Read a 16-bit unsigned value from `r3 + 6`; use it as the destination address. |
| `[[r0 + 0x20] + 0x8] + 0x10` | Follow a pointer at `r0 + 0x20`, then a pointer at offset `0x8` in that object, and fuzz 16 bytes into the final object. |

Memory expressions support `+`, `-`, `*`, `/`, parentheses, and nested
dereferences. A bare bracket dereference reads 32 bits; `uN[expression]`
reads an unsigned `N`-bit value. Non-byte-aligned widths are rounded up to a
byte read and masked to `N` bits. For example, if `r4` points to a connection,
the following resolves a three-step object chain before the bytes are written:

```json
{
  "name": "payload",
  "location": "[[[r4 + 0x14] + 0x8] + 0x20]",
  "type": "random",
  "size": 64
}
```

Here the parser reads a pointer from `r4 + 0x14`, follows a second pointer at
offset `0x8`, then follows a third pointer at offset `0x20`. Use parentheses
when they make a more complex arithmetic expression clearer.
