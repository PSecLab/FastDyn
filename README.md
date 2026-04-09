# FastDyn Plugins
## How to run?
Before running the setup, it is highly recommended to use a virtual environment to isolate dependencies.

### Install the venv package:
```bash
sudo apt update
sudo apt install python3-venv -y
```

Create and activate the virtual environment:

### Create the environment (named 'venv')
```bash
python3 -m venv fastdyn-env
```
### Activate it
```bash
source fastdyn-env/bin/activate
```

(Note: You can deactivate the environment later by simply running deactivate)


### Install `Fastdyn` as a package using:
```bash
./setup.sh
```
You can pass the arguments using -c for configuration.toml, -m for symbol map file and -o for output dir. Further use:

```bash
fastdyn --help
```
to get more information about our great tool.

1. Make sacrifice for debugging gods so your debugging and rehosting goes smoothly.

2. Build Fastdyn-qemu and libhw.

3. Run the following command and pass the `qemu` path along with it like:

   ```bash
   make qemu_path="/home/fastdyn-qemu"
   ```
   if you don't pass the `qemu_path` argument, then, it will use the `../qemu` as the path for the QEMU.
   The Makefile will set up the build and run `ninja`.
   also,
   #TODO: Update this later to be more efficient
   ```bash
   export LD_LIBRARY_PATH=/home/FastDyn/build:Fastdyn/FastDyn/device_models/postmartem/verifier
   ```

3. By default the libraries like `libhw` and `libgz` disabled. To enable them, please go to `Makefile` and change the respective flags.

### Extras Update the readme later
We expect the `cmsis-svd-data` to be placed for the generator and verifier wherever you are the running the command!
We recommend running the command from the main directory of fastdyn. (Do we need to update this?)



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
fastdyn llm -d fastdyn_work_adc -o boardrunner/boardrunner_sdk/model/model.c

# Use a specific model
fastdyn llm -d fastdyn_work_adc -o model.c --model gpt-4.1

# With compilation after extraction
fastdyn llm -d fastdyn_work_adc -o boardrunner/boardrunner_sdk/model/model.c --compile

# Revised prompt (patch mode) with retry
fastdyn llm -d fastdyn_work -o boardrunner/boardrunner_sdk/model/model.c --max-retries 2

# Disable the conversation-reset line for multi-turn context
fastdyn llm -d fastdyn_work_adc -o model.c --no-stateless
```

### Command Reference

| Option | Default | Description |
|--------|---------|-------------|
| `-d` / `--work-dir` | (required) | Work directory with prompt files |
| `-o` / `--output` | (required) | Model .c file path |
| `--model` | `gpt-4o` | OpenAI model name |
| `--env-file` | `~/.fastdyn.env` | Path to .env file with API key |
| `--temperature` | `0.2` | Sampling temperature |
| `--stateless` / `--no-stateless` | `--stateless` | Keep or strip the conversation reset line |
| `--compile` / `--no-compile` | `--no-compile` | Compile model after writing |
| `--sdk-dir` | `boardrunner/boardrunner_sdk` | Path to boardrunner SDK |
| `--max-retries` | `1` | Max retry attempts on failure |

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