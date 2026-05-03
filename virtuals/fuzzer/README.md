# FastDyn Fuzzing Implementation

This is the directory for the current fuzzing implementation, which will be updated as we test the fuzzer on more interesting/complicated firmwares, and add/update the backends.

## Overview

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

## Usage

### fuzz.c interface

fuzz.c is meant to provide a generic interface for fuzzing, whichever backend is compiled in will automatically be used in the background. Our current backends are implementet in fuzz_aflnet.c and fuzz_libafl.c, but should require no modifications.

A custom implementation of the fuzzer on a new firmware will require some familiarity with the usage of virtuals and modifiers in the .toml configuration file for the firmware. lwip_config.toml for STM32F769i-eval in boardrunner/boardrunner_examples should be a good example of how to define virtuals, which are implemented in stateful_fuzzers/lwip_ip.c in this case. These virtuals must be registered in fuzz_init using virtual_register(...)

The general interface consists of the following main functions to focus on which will be useful if you write your own virtuals for hooking the firmware or making a model-based fuzzer:

```c
// Register a callback of the type void callback(void);
void fuzz_register_callback(fuzz_callback_t cb); // called when the fuzzer gets to the post input handling hook
void fuzz_register_exit(fuzz_callback_t cb); // called when the fuzzer exits, happens on a crash or user interruption

// called when the fuzzer is restoring a memory snapshot, is useful for model-level fuzzing so the model can reset some state
// importantly, is called on the first restore_snapshot, so the first call should tell the model what to restore to
void fuzz_register_restore(fuzz_callback_t cb); 

// Data getting/setting with fuzzing backend
size_t fuzz_get_data(char* buf, size_t len); // - Gets the next buffer from the fuzzer, returns 0 if the previous input is still running
void fuzz_set_data(char* buf, size_t len); // - Returns data to the fuzzer, useful for AFLNet which wants data back, for libAFL this does nothing

// These allow getting/setting registers and memory
uint32_t fuzz_get_register(int reg);
void fuzz_set_register(uint32_t value, int reg);
int fuzz_write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
int fuzz_read_memory(unsigned long long addr, uint8_t *mem_buf, int len);

// These are the built in virtuals that are essential to the fuzzing interface
static void virt_assert(unsigned int cpu_index, void *udata); // - This should be set wherever the program cannot reach, reporting reaching it as a bug, restarting program if no non-null PC is given
static void fuzz_snap_point(unsigned int cpu_index, void *udata); // - This should be set after initialization is done, when the input/sequence is done running, this point will be restored to
static void fuzz_sync_point(unsigned int cpu_index, void *udata); // - This should be set after an input is done being processed, which tells the fuzzer to queue up the next input
```

### Examples

Here are some example use cases that are pre-made, to demonstrate fuzzing both from the model-level or from the firmware, which each have their pros and cons. The model layer means you don't have to analyze the firmware to accurately inject and extract inputs/outputs, which is useful for firmwares who have complicated peripheral interactions that are hard to generically insert data into, such as some USART inputting firmwares. The firmware level allows you to directly inject data into the firwmare, which can be useful if you're targetting fuzzing a parser withing the firwmare, letting you skip and stub out parts of the firmware that aren't useful to you with virtuals and modifiers.

#### LWIP fuzzing

LWIP has both model-based and firmware-based fuzzing implementations to use as references, with different pros and cons. The model-based is simpler to implement but is much slower, while the firmware-based is faster, but has a more manual implementation and an imperfect input injection. These examples run in persistent mode since this firmware sometimes uses more RAM than the script automatically detects, so we change the size of one of the sections in bin-writable-ranges as follows:
```
0x20000000	0x7c000
```

##### Model-Based

To use the model-based fuzzing, uncomment the virtual definitions for fuzz_snap_point and fuzz_sync_point in lwip_config.toml, and enable elder mode for the ethernet peripheral. Then, build the provided fuzzing_model.c, and point the elder configuration to the build fuzzing model. Then, simply run the lwip config
```sh
fastdyn run --config boardrunner/boardrunner_examples/examples/STM32F769i-eval/LWIP/lwip_config.toml -p
```

##### Firmware-Based

Our firmware-based fuzzing stubs out all hardware accesses, allowing running it with no hardware connection, and no built model. In the configuration file, uncomment all provided virtuals and modifers. Then, set both the ethernet peripheral and unhandled space to use 'classic' meaning all reads and writes are ignored, reads return 0. Then, simply run the firmware
```sh
fastdyn run --config boardrunner/boardrunner_examples/examples/STM32F769i-eval/LWIP/lwip_config.toml -p
```

#### MQTT fuzzing

##### Firmware-Based

This firmware-based fuzzing also stubs out hardware accesses to allow running with no connection. This firmware has a very simple input injection point, making it an ideal use case for this. The provided .toml file for the MQTT firmware should already have everything set correctly to fuzz. The one minor bug that has to be tweaked is to change fuzz.c on line 352 to the following to avoid a problem occuring from including the PC in the snapshot, which is important for other implementations
```c
for (int i = 0; i < 15; i++) {
```

##### Modbus fuzzing

##### Model-Based

To fuzz Modbus, use the fuzzing_model.c for the elder mode model, and use the provided virtuals for handling synchronization and snapshotting. As with the previous firmwares, run in persistent mode with -p.