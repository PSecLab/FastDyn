# FastDyn In-Process Fuzzer

This directory contains the in-process LibAFL/AFLNet-style fuzzer. It targets
firmware loops and virtual instruction input points directly through the
FastDyn QEMU plugin.

For mission-level vehicle fuzzing with Rumoca FMI v3 plants, ArduPilot
firmware, MAVLink helpers, and `fastdyn swarm`, use OptiFuzz/CP-Explore in
`virtuals/fuzzer/libafl_phi`.

## Building

To build the fuzzer first get the 0.15.4 release of LibAFL from the [GitHub releases page](https://github.com/AFLplusplus/LibAFL/releases/tag/0.15.4).

Build the docker image:

```sh
cd LibAFL-0.15.4/
docker build -t libafl .
```

If you run into issues with this Dockerfile, try the following edits:

- Update line 2 to
  `FROM rust:1.91.0-bullseye AS libafl`
- Remove the dependency `libgcc-12-dev:i386`
- Add dependency `software-properties-common`
- Add this line before qemu install:
  `RUN python3 -m pip install tomli`
- Try to build docker image again

Then run the docker container with the following command:

```sh
./virtuals/fuzzer/fastdyn_fuzz_lib/run_docker.sh
```

**Note:** Make sure to adjust the volume mount paths in `run_docker.sh` to point to the correct locations on your system.

This should drop you into our fuzzing directory. Inside the docker container, build the fuzzer with:

```sh
cargo build --release
```

The resulting binary will be located at `target/release/libfastdyn_fuzzer.so`.

## Usage

To use this fuzzer, make sure `coverage = true` is enabled in the device TOML
file and the fuzzing library is enabled in the Makefile.

Then, use the virtual instructions anchor and assert for fuzzing in the toml file, examples shown below:

    [[CPU.cpu0.virtuals]]
    at          = "0x080006A0"
    instruction = "anchor"
    args        = ["1:0,2,0x333333"]

`1` is the id of this anchor. Each anchor must have a unique numerical id. The
following arguments are register numbers or addresses to fuzz.

    [[CPU.cpu0.virtuals]]
    at          = "0x800395e"
    instruction = "assert"
    args        = "*0x08001000"

When execution reaches that address, FastDyn logs a crash to the fuzzer and sets
the PC to the argument, which must start with `*`.
