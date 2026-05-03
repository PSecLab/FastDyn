# FastDyn Plugins

## Build

FastDyn depends on two sibling repositories that you must build first:

- `qemu/` — the patched QEMU fork that hosts the FastDyn plugin
- `libhw/` — the hardware-probe abstraction (ST-Link / OpenOCD / J-Link)

Clone both as siblings of this repository (or anywhere — you'll point `make` at them via `qemu_path` / `libhw_path`).

### 1. System dependencies

```bash
sudo apt-get update
sudo apt-get install -y meson ninja-build pkg-config libglib2.0-dev libcjson-dev python3-venv
# Optional: only required if you enable SUNDIALS=true
sudo apt-get install -y libsundials-dev
```

### 2. Build the QEMU fork and libhw

Follow the build instructions in each sibling repo. At a minimum:

```bash
# libhw → produces <libhw_path>/out/libhw.so
cd <path/to>/libhw && make

# qemu (fork) → produces qemu-system-arm + plugin headers
cd <path/to>/qemu && ./configure --target-list=arm-softmmu --enable-plugins && make
```

### 3. Build the FastDyn plugin

From the FastDyn repository root:

```bash
make qemu_path=<path/to>/qemu libhw_path=<path/to>/libhw
```

Defaults if you omit the flags: `qemu_path=../qemu`, `libhw_path=../libhw`.

The build produces `build/libfastdyn.so` and copies `libhw.so` next to it.

### 4. Make `libfastdyn.so` discoverable at runtime

```bash
export LD_LIBRARY_PATH=$PWD/build:$PWD/device_models/postmartem/verifier${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```

### 5. Optional Makefile flags

Most features are off by default. Override at `make` time:

| Flag                   | Default | Enables                              |
| ---------------------- | ------- | ------------------------------------ |
| `LIBHW`                | `true`  | Hardware passthrough via libhw       |
| `LIBFUZZ`              | `false` | LibAFL fuzzing harness               |
| `AFLNET`               | `true`  | AFLNet fuzzing harness               |
| `DEV`                  | `true`  | Built-in device models               |
| `DEBUG_PRINT`          | `true`  | Verbose plugin logging               |
| `LIBGZ`                | `false` | Gazebo / SITL physics integration    |
| `LIBPY`                | `false` | Halucinator-mode Python callbacks    |
| `SUNDIALS`             | `false` | SUNDIALS solver for FMU physics      |
| `PHY`                  | `false` | Physics engine (with `LIBGZ=true`)   |
| `FMU`                  | `false` | FMU (Functional Mockup Unit) support |
| `FLIGHT_CONTROLLERS`   | `false` | ArduPilot / PX4 SITL adapters        |
| `BOARD_RUNNER`         | `true`  | BoardRunner SDK build                |

Example:

```bash
make qemu_path=<path/to>/qemu libhw_path=<path/to>/libhw LIBPY=true SUNDIALS=true
```

## Install the Python CLI

Use a virtualenv so `fastdyn` and its deps don't pollute system Python:

```bash
python3 -m venv fastdyn-env
source fastdyn-env/bin/activate
./setup.sh
```

Verify with:

```bash
fastdyn --help
```

To leave the venv later: `deactivate`.

## Run

We recommend running every `fastdyn` / `boardrunner` command from the FastDyn repository root. The `cmsis-svd-data` submodule is auto-fetched into `third_party/common/cmsis-svd-data/` by `make` and resolved automatically when you pass `-b <board>`.

## Fuzzer

For the fuzzer setup, please refer to `virtuals/fuzzer/fastdyn_fuzz_lib/README.md`.

In the Makefile, at most one backend should be enabled at once.

To setup the custom AFLNet, use https://anonymous.4open.science/r/aflnet/README.md

## Required OS Dependencies

For Sundial build of Fastdyn:

```bash
sudo apt-get update
sudo apt-get install -y libsundials-dev pkg-config
```

---

## LLM Integration (ChatGPT API)

FastDyn can send generated prompts directly to the OpenAI ChatGPT API, process the
response, and write or patch the device model automatically.

### Setup

#### 1. Install Dependencies

The `openai` and `python-dotenv` packages are required. They are included in
`requirements.txt`, so running `./setup.sh` or `pip install -r requirements.txt`
will install them.

#### 2. Configure Your API Key

Create a file at `~/.fastdyn.env` with your OpenAI API key:

```bash
echo 'OPENAI_API_KEY=sk-your-key-here' > ~/.fastdyn.env
chmod 600 ~/.fastdyn.env
```

Alternatively, export the environment variable directly:

```bash
export OPENAI_API_KEY=sk-your-key-here
```

The tool checks the environment variable first, then falls back to `~/.fastdyn.env`.
You can also specify a custom env file with `--env-file /path/to/.env`.

#### 3. Configure Build Paths (for `--compile`)

If you want to use the `--compile` flag to automatically compile models, fill in
the `boardrunner/boardrunner_sdk/build_config.env` file:

```bash
FASTDYN_INCLUDE_DIR=/path/to/FastDyn/include
QEMU_INCLUDE_DIR=/path/to/qemu/include
```

### Usage

```bash
# Basic: send an initial prompt and extract the model
boardrunner llm -d fastdyn_work_adc -o boardrunner/boardrunner_sdk/model/model.c

# Use a specific model
boardrunner llm -d fastdyn_work_adc -o model.c --model gpt-4.1

# With compilation after extraction
boardrunner llm -d fastdyn_work_adc -o boardrunner/boardrunner_sdk/model/model.c --compile

# Revised prompt (patch mode) with retry
boardrunner llm -d fastdyn_work -o boardrunner/boardrunner_sdk/model/model.c --max-retries 2

# Disable the conversation-reset line for multi-turn context
boardrunner llm -d fastdyn_work_adc -o model.c --no-stateless
```

### Command Reference

| Option                           | Default                       | Description                               |
| -------------------------------- | ----------------------------- | ----------------------------------------- |
| `-d` / `--work-dir`              | (required)                    | Work directory with prompt files          |
| `-o` / `--output`                | (required)                    | Model .c file path                        |
| `--model`                        | `gpt-4o`                      | OpenAI model name                         |
| `--env-file`                     | `~/.fastdyn.env`              | Path to .env file with API key            |
| `--temperature`                  | `0.2`                         | Sampling temperature                      |
| `--stateless` / `--no-stateless` | `--stateless`                 | Keep or strip the conversation reset line |
| `--compile` / `--no-compile`     | `--no-compile`                | Compile model after writing               |
| `--sdk-dir`                      | `boardrunner/boardrunner_sdk` | Path to boardrunner SDK                   |
| `--max-retries`                  | `1`                           | Max retry attempts on failure             |

### How It Works

1. **Initial prompt** (`initial_prompt.txt`): The tool sends the prompt to ChatGPT,
   extracts the C code from the fenced code block in the response, and writes it to
   the output file.

2. **Revised prompt** (`revised_prompt.txt`): The tool sends the prompt to ChatGPT,
   parses SEARCH/REPLACE blocks from the response, and applies them as patches to
   the existing model file.

3. **On failure**: If a patch fails or compilation fails, the tool prompts you to
   send a follow-up request to the LLM with the error context for automatic correction.

4. The raw LLM response is always saved to `<work_dir>/llm_response.txt` for auditing.

---

## Unit Tests

Unit tests live in `tests/unit/`. See `tests/unit/README.md` for conventions and
detailed instructions.

```bash
# Run all unit tests
pytest tests/unit/ -v

# Run a specific test file
pytest tests/unit/test_patch.py -v
```
