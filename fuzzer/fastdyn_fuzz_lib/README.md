# FastDyn Unified Fuzzer

This directory contains a unified fuzzer which allows to fuzz a Courbet system with CPFuzz.

## Building

To build the fuzzer first get the 0.15.3 release of LibAFL from the [GitHub releases page](https://github.com/AFLplusplus/LibAFL/releases/tag/0.15.3).

Build the docker image:

```sh
cd LibAFL-0.15.3/
docker build -t libafl .
```

Then run the docker container with the following command:

```sh
./fuzzer/fastdyn_fuzz_lib/run_docker.sh
```

**Note:** Make sure to adjust the volume mount paths in `run_docker.sh` to point to the correct locations on your system.

This should drop you into our fuzzing directory. Inside the docker container, build the fuzzer with:

```sh
cargo build --release
```

The resulting binary will be located at `target/debug/fastdyn_split_fuzz`.