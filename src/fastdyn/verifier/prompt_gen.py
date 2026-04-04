import os
import sys
import glob
from typing import Dict
import logging
import textwrap
import click
from pathlib import Path

from . import verifier as verify
from .. import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()
user_obserability_prompt = "NOTE AND REQUIREMENT: We need this model to be observable/user interactive."
non_user_obserability_prompt = "NOTE AND REQUIREMENT: We DON'T need this model to be observable/user interactive."
runtime_trace_request = "If you need, ask the user and user can give you complete complete runtime mmio trace for the peripheral model, it is compressed and contains all the transitions."


# ── Critical framework rules that must appear in every generated prompt ──────
# These prevent common LLM mistakes that are impossible to infer from trace data alone.
framework_rules = """
## Critical Framework Rules

### 1. Absolute Addressing
The emulation framework passes **absolute guest physical addresses** to the model's
read/write callbacks — NOT offsets relative to the peripheral base. For example, a
read to a register at offset 0x08 of a peripheral whose base address is 0x40022000
arrives as `addr = 0x40022008`.

**You MUST subtract the peripheral's base address** from `addr` before comparing
against register offsets. For example:
```c
uint64_t my_periph_read(void *opaque, hwaddr addr, unsigned size) {
    // Convert absolute address to register offset
    hwaddr offset = addr - MY_PERIPH_BASE;
    switch (offset) {
        case 0x00: /* first register  */ ...
        case 0x08: /* second register */ ...
    }
}
```
If a device covers multiple disjoint address ranges (e.g., peripheral A at
0x40020000–0x400203FF and peripheral B at 0x40020800–0x40020BFF), your model must
handle both absolute ranges by checking which range the incoming address falls into
and subtracting the appropriate base for that range.

The trace data shows these absolute addresses directly (e.g., `address = 0x40022008`).
Use them to infer each peripheral's base address and register layout.

### 2. Callback Function Signatures
Any function pointer registered with a framework API **MUST exactly match** the
expected signature. In particular:

- **All callback-based APIs** in this framework pass a `void *opaque` (or `void *data`)
  argument to the registered callback. Your callback functions **MUST** accept this
  parameter. Do NOT use empty parameter lists `()` in C — that is NOT the same as
  `(void)` and causes undefined behavior when the framework passes an argument.

```c
// CORRECT — matches the expected void (*)(void *) signature:
static void my_callback(void *opaque) {
    MyState *s = (MyState *)opaque;
    // ...
}

// WRONG — empty parameter list, undefined behavior when caller passes an argument:
static void my_callback() { ... }
```

- **Never cast function pointers** to hide type mismatches. If the compiler warns
  about an incompatible function pointer, fix the function signature instead.
- This applies to ALL framework callbacks: timer callbacks, inter-peripheral request
  handlers, and any other API that accepts a function pointer and an opaque/data pointer.

### 3. Stateful Emulation over Trace Memorization
You must implement a dynamic, stateful model. The trace data is EVIDENCE for
reverse-engineering the register logic — it is NOT a lookup table to copy from.

- DO NOT generate "record-and-replay" code that hardcodes return values from the
  trace (e.g., `if (addr == X && count == N) return Y;`). Such code only works
  for the exact execution that was recorded and breaks on any other run.
- DO maintain a state struct whose fields represent register values.
- DO update state based on firmware writes at runtime (e.g., set/clear bits via
  masking when the firmware writes to a control register).
- DO derive read return values from current state, not from trace constants.
- DO keep the logic minimal — only implement behavior the trace evidence supports.
  Do not invent complex state machines, timers, or hardware features beyond what
  the trace implies.
"""


search_replace_prompting = """
### 3.Output your code changes using SEARCH/REPLACE blocks.
Instead of providing the full source code, provide your code changes using SEARCH/REPLACE blocks to update the model/s file.
Rules for SEARCH/REPLACE blocks:
1. The code in the SEARCH section must exactly match the current file, character for character, including all whitespace and indentation.
2. Include enough lines in the SEARCH block to uniquely identify the location (usually 3-5 lines of context around the change).
3. Do not use abbreviations, placeholders, or comments like `// ... rest of function` in the SEARCH block.
4. Output multiple blocks if you need to make changes in different parts of the file.
5. Wrap each SEARCH/REPLACE block in a standard text markdown block (text ... ) so it can be easily parsed.
Use this exact format:
// FILE: target_filename.c
<<<<<<< SEARCH
[exact old code to find]
=======
[new code to replace it with]
>>>>>>> REPLACE
"""

# ── LLM-facing instructions for verifier diff interpretation ────────────────
# These are added to prompts by prompt_gen.py, NOT by verifier.py.
# Verifier reports raw facts; these constants add interpretation for the LLM.
polling_interpretation_instructions = """
NOTE: Polling patterns indicate that the polled register has bits that hardware
auto-clears or auto-sets.

CRITICAL WARNING: Just because a Polling detection appears in the "emulated model"
list DOES NOT mean the emulation is handling it correctly! If the emulation gets
stuck in an INFINITE POLLING LOOP forever, the verifier will still detect the
polling pattern because it sees the infinite reads.
Therefore, if you see a Polling Loop on a register (e.g. Polling Loop on REG_B), you
MUST check the "Entropy Mismatch Details" for the polled register (e.g. REG_B).
If the polled register has LOW (0.00) entropy in emulation while having >0 entropy
in hardware, it means your model is HANGING in an infinite loop and you MUST
implement self-clearing/auto-transitioning bits in that register!
"""


# Define the QEMU API context
qemu_api_list = """
- `int qemu_plugin_write_memory(unsigned long long addr, uint8_t *mem_buf, int len)`: Writes guest memory.
- `int qemu_plugin_read_memory(unsigned long long addr, uint8_t *mem_buf, int len)`: Reads guest memory.
- `int qemu_plugin_read_register(int reg, uint8_t *buf)`: Reads a register of VM. reg is number of register 0 is R0 in ARM.
- `void qemu_plugin_set_register(uint8_t *mem_buf, int reg)`: Writes a register of VM. reg is number of register 0 is R0 in ARM.
- `void qemu_plugin_raise_irq(int irq, false)`: Raises an interrupt line, MUST: use interrupt + 16, here false means non-secure interrupt, dont change it to true!.
- `void qemu_plugin_timer_alarm(uint64_t timer_fd, uint64_t delay_ns)`: Accepts a timer's handle and an absolute nanosecond timestamp to arm that timer to fire at the specified moment for one shot.
- `int64_t qemu_plugin_get_virtual_timer(void)`: Returns virtual clock (monotonic up counter) of the system.
- `uint64_t qemu_plugin_timer_new_ns(void (*cb)(void *), void *data)`: Accepts a callback function and user data to create a new, unscheduled timer object for a future one-shot event, returning a uint64_t handle that must be armed manually.
- `uint64_t qemu_plugin_timer_new_period_ns(void (*cb)(void *), void *data, uint64_t period)`: Accepts a callback function, user data, and a nanosecond period to create and arm a periodic timer that executes the callback at each interval, returning a uint64_t timer handle.
- `void dev_debug(char *str)`: Any debug messages must be logged using this function.
- `int api_pty_fd_gen(void)`: Takes no input and returns an integer file descriptor for the pseudo-terminal device /tmp/usart1_pty
- `void api_pty_write_req(int fd, uint8_t value)`: Takes a file descriptor fd and a byte value as input to write the byte to the pseudo-terminal, with no output.
- `int api_pty_read_nonblock(int fd, uint8_t *buff);`: Attempts to read a single byte from the pseudo-terminal fd in non-blocking mode, returning a status.
- `I2CBus api_i2c_init_bus(ConfigSection* model_info)`: Initializes an I2C bus from a configuration section and returns the I2CBus struct by value.
- `int api_i2c_start_transfer(I2CBus* bus, uint8_t address, bool is_recv)`: Starts an I2C transaction with a slave device.
- `int api_i2c_start_transfer_10bit(I2CBus* bus, uint16_t address, bool is_recv)`: Starts an I2C transaction with a slave device when the address is 10bit.
- `void api_i2c_end_transfer(I2CBus* bus)`: Ends the current I2C transaction.
- `int api_i2c_send(I2CBus *bus, uint8_t data)`: Sends a byte to the active I2C slave device.
- `uint8_t api_i2c_recv(I2CBus *bus)`: Receives a byte from the active I2C slave device.
- `SPIBus api_spi_init_bus(ConfigSection* model_info)`: Takes a ConfigSection pointer, parses the SPI configuration, loads all specified slave device models, and returns a populated SPIBus structure.
- `uint32_t api_spi_transfer(SPIBus *bus, uint32_t val)`: Performs a full-duplex transfer on the bus by sending val (MOSI) to the currently selected slave and returning the uint32_t value received from it (MISO).
- `void api_spi_set_cs(SPIBus *bus, int cs_id, int level)`: Sets the logic state of a chip select line by taking a cs_id and level (0=active, 1=inactive), and notifies all relevant slaves by calling their set_cs callback.
- `void api_dma_register_stream(int stream_id, dma_request_handler_t handler, void *opaque)`: Called by a DMA model. Takes a `stream_id`, a `handler` callback, and an `opaque` data pointer. Registers the handler to be called when a peripheral triggers a request for that stream.
- `void api_dma_request(int stream_id)`: Called by a peripheral model (e.g., ADC). Takes a `stream_id` to trigger. This function looks up the corresponding handler (registered via `api_dma_register_stream`).
- `int api_fifo_open(const char *path)`: Creates and opens a named pipe (FIFO) at the specified path using O_RDWR to prevent EOF when the external writer disconnects.
- `int api_fifo_write(int fd, const void *data, int len)`: Writes a data buffer of a specified length to the FIFO file descriptor.
- `int api_fifo_read_nonblock(int fd, uint8_t *out_byte)`: Reads a single byte from the FIFO in non-blocking mode. Returns 1 if a byte was read, 0 if the buffer is empty.
- `void api_fifo_close(int fd, const char *path)`: Closes the file descriptor and removes (unlinks) the named pipe file from the filesystem.

// --- I2C Bus struct definitions here ---
typedef struct {
    char* name; //Name of the slave
    int address;    //address for which the slave will be registered
    SlaveSendFunc send; //Call this function when you want to send data and get ack from the slave device.
    SlaveRecvFunc recv; //Call this function when you want to receive data from the slave device.
    SlaveEventFunc event; //Call this function when you want to start transmission
} SlaveDetails;

typedef struct {
    int num_slaves;
    SlaveDetails* slave; //Dynamic array of slaves
} SlaveList;

typedef struct {
    SlaveList Slaves;  //Constant once registered on i2c_init_bus
    SlaveDetails* current_dev;//Current devices show the current devices being used for the transaction
    uint8_t saved_address; //saved_address is the address initiated for the transaction by the master.
} I2CBus;
// --- End of struct definitions ---

// --- SPI Bus struct definitions here ---
#define NUM_CS_LINES 4          //USER can change the max number based on the requirement

typedef struct {
    char* name;                 //Name of the slave
    int cs_id;                  //Chip select id for which the device will be registered
    int cs_enable;              //is the current chip enable
    SlaveTransferFunc transfer; //transfer function used to send and receive the data to/from slave
    SlaveSetcsFunc set_cs;      //function to tell the slave it is selected
} SpiSlaveDetails;

typedef struct {
    int num_slaves;
    SpiSlaveDetails* slave; //Dynamic array of slaves
} SpiSlaveList;

typedef struct {
    SpiSlaveList Slaves;  //Registered on spi_init_bus
} SPIBus;
// --- End of struct definitions ---

// --- Start of DMA Info

#define MAX_DMA_STREAMS 16

// --- End of DMA Info

// --- Network Backend APIs (Linux TAP) ---
// 1. Init TAP interface (NON-BLOCKING). Returns fd.
int api_tap_init(const char *dev_name);
// 2. Send raw frame to host.
int api_tap_send(int fd, const uint8_t *buf, int len);
// 3. Poll host for incoming frame. Returns length or -1.
int api_tap_recv_nonblock(int fd, uint8_t *buf, int max_len);
"""

def initial_prompt_gen_multiple_periphs(analysis_dir, model_name, peripherals, out_dir, model_sources, user_obs=''):
    fastdyn_log.info("Generating Prompt for LLM")
    global qemu_api_list

    final_prompt = generate_prompt_multiple(
        analysis_dir=analysis_dir,
        model_name=model_name,
        peripherals=peripherals,
        qemu_api_list=qemu_api_list,
        user_obs = user_obs,
        model_sources=model_sources
    )

    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "initial_prompt.txt"
    output_path.write_text(final_prompt + "\n", encoding="utf-8")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return str(output_path)



def initial_prompt_gen(analysis_dir, peripheral, out_dir):
    fastdyn_log.info("Generating Prompt for LLM")
    # Construct the path from the base directory and the peripheral name
    peripheral_path = os.path.join(analysis_dir, peripheral)

    final_prompt = generate_prompt(peripheral_path)

    output_path = os.path.join(out_dir, "initial_prompt.txt")
    with open(output_path, "w") as f:
        f.write(final_prompt + "\n")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return output_path



def iteration_prompt_gen(diff_obj, peripheral, out_dir, device_model_path, show_prompt=True, max_model_chars=120000):
    '''
    Based on the difference object, create a correction prompt using SEARCH/REPLACE
    blocks. This is consistent with the multi-peripheral verifier flow.
    '''
    platform_name = diff_obj.platform_name
    peripheral_name = peripheral
    device_model = ''
    model_filename = os.path.basename(device_model_path)

    if show_prompt:
        device_model = Path(device_model_path).read_text(encoding="utf-8", errors="replace")
        if max_model_chars and len(device_model) > max_model_chars:
            device_model = device_model[:max_model_chars] + "\n/* ... truncated ... */"

    # Build the state data with LLM-facing instructions appended
    state_data_with_instructions = diff_obj.diff_state_data
    if state_data_with_instructions:
        state_data_with_instructions += "\n" + polling_interpretation_instructions

    final_prompt = f'''
Take this prompt independent from previous prompt history.

You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name} peripheral.
Your task is to analyze the following summary of MMIO trace data and correct the existing C device model.

## Backward-pass/Correction
Below is the current device model. The logs show mismatches between hardware and emulation; identify the root cause and fix it.

## Model Under Correction
You may produce SEARCH/REPLACE blocks for this file:
- `{peripheral_name}` -> `{model_filename}`
Each SEARCH/REPLACE block MUST begin with a comment identifying its target file:
`// FILE: {model_filename}`

--- START OF CURRENT MODEL ---

## Model: `{peripheral_name}`  |  File: `{model_filename}`
```c
{device_model}
```

--- END OF CURRENT MODEL ---

## Available APIs
You **must** use the following APIs to construct the device model. Pay close attention to the read/write callback signatures.
```c
{qemu_api_list.strip()}
```

### NOTE
If a required API is missing from the registry, stop and do not generate the model. Ask the user to provide the API by specifying its inputs, outputs, and description. Then ask whether to generate the device model with this API or attempt it without using a workaround. If no workaround is possible, indicate that the API is critical for the device model.

If you need more info about the firmware, dont generate the device model.

{runtime_trace_request}

If you believe the issue is not in the model but the device (slave) attached to it, stop and ask the user for the slave model and observe the slave model first and correct the slave model as well if it has issues, don't generate the master model!
The slave model will just have following supporting functions and not any more registration functions which **MUST NOT** be changed
//In case of an I2C slave
- STM32F4_event
- STM32F4_send
- STM32F4_receive

//In case of a SPI slave
- STM32F4_set_cs
- STM32F4_transfer

{framework_rules}

## Host-Side I/O Setup (if needed)
If the model needs host I/O, output the exact host command(s). If none, output exactly:
No host-side setup required.

## Trace Log Schema:
The logs below are formatted as arrays containing the following fields: [Operation, Peripheral, Register, Memory Address, Register Value, Program Counter (PC)]. Pay close attention to the Register Value differences between Hardware and the Emulated Model.

--- START OF ANALYSIS DATA ---

## Platform:
{platform_name}

## Peripheral Name:
{peripheral_name}

## Initialization Sequence (`init.txt`):
This file contains all accesses that occur before the main runtime loop begins.
```
{diff_obj.diff_init_data}
```

## Detected Runtime Loops (`loop_pattern_*.txt`):
These files contain the most common repeating sequences of operations during runtime.
```
{diff_obj.diff_loop_pattern_data}
```

## Stateful Behavior Analysis (`state.txt`):
This file identifies programming patterns like Read-Modify-Write (RMW), which indicate stateful registers.
```
{state_data_with_instructions}
```

## Register Entropy Analysis (`entropy.txt`):
This file measures the randomness of values read from registers. High entropy suggests data registers, while low entropy suggests status registers.
```
{diff_obj.diff_entropy_data}
```
## Runtime Data Accesses
This file contains all the accesses information for the data registers
{diff_obj.diff_runtime_trace}

# ISR Analysis data (`isr_analysis.txt`):
This file contains the information about the irqs
{diff_obj.isr_analysis_data}

--- END OF ANALYSIS DATA ---

Based **only** on the data provided above, output:

## Required Output Format:

### 1. Previous Model Failure Analysis
A concise, one-paragraph summary explaining the most likely reason for the mismatches.

### 2. Register Analysis
A bulleted list of the important registers mentioned in the traces and their inferred functions.

{search_replace_prompting}
'''
    output_path = os.path.join(out_dir, "revised_prompt.txt")
    with open(output_path, "w") as f:
        f.write(final_prompt + "\n")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return output_path

def read_file_content(filepath: str) -> str:
    """Reads the entire content of a file, returning a helpful message if not found."""
    if not os.path.exists(filepath):
        return f"File not found: {os.path.basename(filepath)}"
    try:
        with open(filepath, 'r') as f:
            content = f.read().strip()
            return content if content else "File is empty."
    except Exception as e:
        return f"Error reading file: {e}"

def parse_summary_file(filepath: str) -> Dict[str, str]:
    """Parses the key-value pairs from the summary.txt file."""
    summary_data = {}
    if not os.path.exists(filepath):
        return summary_data
    try:
        with open(filepath, 'r') as f:
            for line in f:
                if ":" in line:
                    key, value = line.split(":", 1)
                    summary_data[key.strip()] = value.strip()
    except Exception:
        # Ignore errors if the summary file is malformed
        pass
    return summary_data

def generate_prompt(peripheral_directory: str) -> str:
    """
    Generates a detailed prompt for an LLM based on analysis files in a directory.
    """
    if not os.path.isdir(peripheral_directory):
        print(f"[ERROR] Analysis directory not found: {peripheral_directory}")
        print(f"[ERROR] Please ensure you have run the analysis script first.")
        sys.exit(1)

    peripheral_name = os.path.basename(peripheral_directory)

    # Find and parse the summary.txt file from the parent directory
    parent_dir = os.path.dirname(peripheral_directory)
    summary_path = os.path.join(parent_dir, "summary.txt")
    summary_info = parse_summary_file(summary_path)
    platform_name = summary_info.get("Platform", "Unknown Platform")
    isr_analysis = os.path.join(peripheral_directory, "isr_analysis.txt")

    # Read all other analysis files
    init_data = read_file_content(os.path.join(peripheral_directory, "init.txt"))
    state_data = read_file_content(os.path.join(peripheral_directory, "state.txt"))
    entropy_data = read_file_content(os.path.join(peripheral_directory, "entropy.txt"))
    loop_files = sorted(glob.glob(os.path.join(peripheral_directory, "loop_pattern_*.txt")))
    isr_analysis_data = read_file_content(isr_analysis) if os.path.exists(isr_analysis) else None

    loop_data_list = []
    if not loop_files:
        loop_data_list.append("No repeating loop patterns were detected for this peripheral.")
    else:
        for loop_file in loop_files:
            loop_data_list.append(f"--- Contents of {os.path.basename(loop_file)} ---\n{read_file_content(loop_file)}")
    loop_data = "\n\n".join(loop_data_list)

    # Assemble the prompt, escaping literal curly braces {{ and }} in the C code example
    prompt = f"""
Take this prompt independent from previous prompt history.

You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name.lower()} peripheral.
Your task is to analyze the following summary of MMIO trace data and generate a complete C device model.

## Available  APIs
You **must** use the following APIs to construct the device model. Pay close attention to the read/write callback signatures.
```c
{qemu_api_list.strip()}
```

### NOTE
If a required API is missing from the registry, stop and do not generate the model. Ask the user to provide the API by specifying its inputs, outputs, and description. Then ask whether to generate the device model with this API or attempt it without using a workaround. If no workaround is possible, indicate that the API is critical for the device model.

{framework_rules}

## Commands:
After generating the model, provide the host command required to create and manage the virtual I/O endpoint (e.g., a pseudo-terminal at a fixed path) that the device model will connect to. This command should be run in a separate terminal. If no external command is required for the peripheral to function, skip this section.

--- START OF ANALYSIS DATA ---

## Platform:
{platform_name}

## Peripheral Name:
{peripheral_name}

## Initialization Sequence (`init.txt`):
This file contains all accesses that occur before the main runtime loop begins.
```
{init_data}
```

## Detected Runtime Loops (`loop_pattern_*.txt`):
These files contain the most common repeating sequences of operations during runtime.
```
{loop_data}
```

## Stateful Behavior Analysis (`state.txt`):
This file identifies programming patterns like Read-Modify-Write (RMW), which indicate stateful registers.
```
{state_data}
```

## Register Entropy Analysis (`entropy.txt`):
This file measures the randomness of values read from registers. High entropy suggests data registers, while low entropy suggests status registers.
```
{entropy_data}
```

# ISR Analysis data (`isr_analysis.txt`):
This file contiains the information about the irqs
{isr_analysis_data}

--- END OF ANALYSIS DATA ---

Based **only** on the data provided above, generate the complete C source code for the device model. Follow the required output format precisely.

## Required Output Format:

### 1. High-Level Summary
A concise, one-paragraph summary of this peripheral's likely purpose and overall behavior, considering the platform context.

### 2. Register Analysis
A bulleted list of the important registers mentioned in the traces and their inferred functions.

### 3. C Device Model Source Code
The C source code for MMIO read and write callback for {peripheral_name} emulation and any initialization you need for the emulation only. The code must be fully self-contained and ready to be compiled. Including <device.h> and <boardrunner/vio.h> will give you access to all APIs i mentioned.

```c
// Device Model for {peripheral_name}

// Inferred Register Functions:
// ... add registers here ...

// This function will emulation all device reads
uint64_t {peripheral_name.lower()}_read(void *opaque, hwaddr addr, unsigned size) {{
    // Example: return device->register; // Return some register value from device
	// ... {peripheral_name.lower()} reads, the retuned value will be emulation of device ...
}}

// This function will emulate all device writes
void {peripheral_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{
        // Example: GPIOG->BSRR = value; // Set PG13 high
        // ... Code that responds to {peripheral_name.lower()} writes to emulated device ...
}}

void {peripheral_name.lower()}_init(ConfigSection* model_info) {{
		// Example: memset(&{peripheral_name.lower()}_state, 0, sizeof({peripheral_name.lower()}_state_t));
}}
```
"""
    return prompt.strip()

def slave_model_gen(peripheral_name, platform_name, out_dir, slave_firmware_path, reference_model_path):
    '''
    based on the slave firmware code and the existing reference code, generate a C model for the slave.
    '''
    fastdyn_log.info("Generating Prompt for LLM")

    with open(slave_firmware_path, 'r') as f:
        slave_firmware = f.read().strip()

    with open(reference_model_path, 'r') as f:
        reference_model = f.read().strip()

    slave_gen_prompt = f"""
    Take this prompt independent from previous prompt history.

    You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name.lower()} peripheral.
    Your task is to analyze the following firmware of the {peripheral_name} slave and a reference c model.

    ## Available  APIs
    The slave model will just have following supporting functions and not any more registration functions which **MUST NOT** be changed
    //In case of an I2C slave
    - STM32F4_event
    - STM32F4_send
    - STM32F4_receive

    //In case of a SPI slave
    - STM32F4_set_cs
    - STM32F4_transfer

    ### NOTE
    If a required API is missing from the registry, stop and do not generate the model. Ask the user to provide the API by specifying its inputs, outputs, and description. Then ask whether to generate the device model with this API or attempt it without using a workaround. If no workaround is possible, indicate that the API is critical for the device model.

    {framework_rules}

    ## Commands:
    After generating the model, provide the host command required to create and manage the virtual I/O endpoint (e.g., a pseudo-terminal at a fixed path) that the device model will connect to. This command should be run in a separate terminal. If no external command is required for the peripheral to function, skip this section.

    ## Slave Firmware
    {slave_firmware}

    ## Slave Model Reference Code
    {reference_model}
    """

    output_path = os.path.join(out_dir, "slave_model_prompt.txt")
    with open(output_path, "w") as f:
        f.write(slave_gen_prompt + "\n")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return output_path

def _resolve_roles(peripherals, model_name, model_sources=None):
    """
    Returns:
        roles   : dict {periph_str: 'SOURCE' | 'CONTEXT'}
        sources : list of SOURCE peripheral names
    """
    if model_sources:
        # Explicit override — trust the user
        model_sources_lower = {s.strip().lower() for s in model_sources}
        roles = {
            p: ("SOURCE" if p.strip().lower() in model_sources_lower else "CONTEXT")
            for p in peripherals
        }
    else:
        # Heuristic: exact match OR model_name contains periph name as substring
        model_lower = model_name.strip().lower()
        roles = {}
        for p in peripherals:
            p_lower = p.strip().lower()
            if p_lower == model_lower or p_lower in model_lower:
                roles[p] = "SOURCE"
            else:
                roles[p] = "CONTEXT"

    sources = [p for p, r in roles.items() if r == "SOURCE"]

    if not sources:
        raise ValueError(
            f"Could not resolve any SOURCE peripheral for model '{model_name}'.\n"
            f"Peripherals given: {list(peripherals)}\n"
            f"None matched by exact name or substring.\n"
            f"Use -ms/--model-source to specify explicitly.\n"
            f"Example: -ms DMA1 -ms DMAMUX1"
        )

    return roles, sources

def generate_prompt_multiple(analysis_dir, model_name, peripherals, qemu_api_list, user_obs, model_sources):
    """
    Generates a detailed prompt for an LLM based on analysis files in a directory.
    `peripherals` can be a tuple/list (Click multiple=True) or a single string.
    """
    if isinstance(peripherals, str):
        peripherals = [peripherals]
    else:
        peripherals = list(peripherals)

    # Resolve roles
    try:
        roles, sources = _resolve_roles(peripherals, model_name, model_sources)
    except ValueError as e:
        raise click.UsageError(str(e))

    # user_obs = 'REQ'
    if user_obs == 'REQ':
        usr_prompt = user_obserability_prompt
    elif user_obs == 'NOT-REQ':
        usr_prompt = non_user_obserability_prompt
    else:
        usr_prompt = ''

    # Parse summary once (it lives in analysis_dir/summary.txt per your layout)
    summary_path = os.path.join(analysis_dir, "summary.txt")
    summary_info = parse_summary_file(summary_path)
    platform_name = summary_info.get("Platform", "Unknown Platform")

    blocks = []
    for periph in peripherals:
        peripheral_directory = os.path.join(analysis_dir, periph)
        peripheral_name = os.path.basename(peripheral_directory)

        init_data    = read_file_content(os.path.join(peripheral_directory, "init.txt"))
        state_data   = read_file_content(os.path.join(peripheral_directory, "state.txt"))
        entropy_data = read_file_content(os.path.join(peripheral_directory, "entropy.txt"))

        isr_path = os.path.join(peripheral_directory, "isr_analysis.txt")
        isr_analysis_data = read_file_content(isr_path) if os.path.exists(isr_path) else "No ISR analysis file was found."

        loop_files = sorted(glob.glob(os.path.join(peripheral_directory, "loop_pattern_*.txt")))
        if not loop_files:
            loop_data = "No repeating loop patterns were detected for this peripheral."
        else:
            loop_chunks = []
            for loop_file in loop_files:
                loop_chunks.append(
                    f"--- Contents of {os.path.basename(loop_file)} ---\n{read_file_content(loop_file)}"
                )
            loop_data = "\n\n".join(loop_chunks)

        role = roles[periph]
        if role == "SOURCE":
            role_note = (
                f"[SOURCE] — This peripheral's trace data is the PRIMARY BEHAVIORAL SOURCE "
                f"for modeling `{model_name}`. Generate `{model_name}` using this data as "
                f"your main reference. The output model name is `{model_name}` rather than "
                f"`{periph}` because this model may be a composite covering additional "
                f"hardware blocks (see address ranges below if present)."
            )
        else:
            role_note = (
                f"[CONTEXT] — Do NOT generate a model for `{periph}`. Use this data only "
                f"to understand how `{model_name}` interacts with `{periph}` at runtime."
            )

        block = textwrap.dedent(f"""\
        ## Peripheral: {periph}  [{role}]
        > {role_note}

        ## Initialization Sequence (`init.txt`)
        This file contains all accesses that occur before the main runtime loop begins.
        ```
        {init_data}
        ```

        ## Detected Runtime Loops (`loop_pattern_*.txt`)
        These files contain the most common repeating sequences of operations during runtime.
        ```
        {loop_data}
        ```

        ## Stateful Behavior Analysis (`state.txt`)
        This file identifies programming patterns like Read-Modify-Write (RMW), which indicate stateful registers.
        ```
        {state_data}
        ```

        ## Register Entropy Analysis (`entropy.txt`)
        This file measures the randomness of values read from registers. High entropy suggests data registers,
        while low entropy suggests status registers.
        ```
        {entropy_data}
        ```

        ## ISR Analysis (`isr_analysis.txt`)
        This file contains information about IRQs.
        ```
        {isr_analysis_data}
        ```
        """)
        blocks.append(block)

    prompt_start = textwrap.dedent(f"""\
    Take this prompt independent from previous prompt history.

    You are an expert reverse engineer specializing in embedded systems and writing C emulation \
    for peripherals in a QEMU-based framework.
    You have read the reference manual for {platform_name} with deep familiarity with \
    {", ".join(peripherals)}.
    {usr_prompt}

    ## Your Task
    Generate a **single, complete C device model** for: `{model_name}`

    The analysis data below covers **multiple peripherals**: {", ".join(peripherals)}.
    One of these is the **target** (`{model_name}`) — the model you are generating.
    The others are **context peripherals** — provided only because `{model_name}` has a \
    runtime dependency on them (e.g., it triggers DMA, shares a bus, raises an IRQ to another block).

    ### How to Use Context Peripheral Data
    - Do **not** generate a device model for context peripherals.
    - Use their trace data only to understand how `{model_name}` interacts with them at runtime \
    (e.g., which stream IDs are used, when a DMA request is triggered, what IRQ line is shared).
    - If `{model_name}` drives or is driven by a context peripheral, express that relationship \
    using the inter-peripheral APIs listed below (e.g., `api_dma_request`, \
    `api_dma_register_stream`).

    ### Dependency Check
    Before generating code, briefly state:
    1. Which peripheral(s) are context (not the target).
    2. What the runtime relationship is between `{model_name}` and each context peripheral.
    3. Which API(s) from the list below implement that relationship.

    If you cannot infer a meaningful relationship from the trace data, and the context peripheral \
    data provides no useful information for modeling `{model_name}`, state that clearly and \
    **ignore** the context peripheral — do not let it block code generation.
    Only stop and ask if a relationship clearly exists but the required API is missing from the list.

    ## Available APIs
    You **must** use only the following APIs. Pay close attention to signatures.
    ```c
    {qemu_api_list.strip()}
    ```

    ### API Note
    If a required API is missing, stop and ask the user to provide it (inputs, outputs, description). \
    Then ask whether to proceed with a workaround or wait for the API. If no workaround exists, \
    state that it is critical.

    {framework_rules}

    ## Host-Side I/O Setup (if needed)
    Output the exact host command(s) needed, inferred strictly from the available APIs \
    (e.g., PTY→serial, TAP→network, FIFO→pipe). List commands in run order. \
    If none needed, write exactly: No host-side setup required.

    --- START OF ANALYSIS DATA ---

    ## Platform
    {platform_name}

    ## TARGET MODEL: {model_name}

    """)

    # f-string + doubled braces to emit literal C braces
    prompt_end = textwrap.dedent(f"""\
    --- END OF ANALYSIS DATA ---

    Based **only** on the data above, generate the complete C source for `{model_name}`.

    ## Required Output Format

    ### 1. Dependency Summary
    - Target model: `{model_name}`
    - Context peripheral(s) and their role (e.g., "DMA — `{model_name}` calls `api_dma_request(stream_id)` after conversion complete")
    - Inter-peripheral APIs used and why

    ### 2. Register Analysis
    Bulleted list of important registers and their inferred functions.

    ### 3. C Device Model Source Code
    Self-contained C source for `{model_name}` only. Including `<device.h>` and `<boardrunner/vio.h>` \
    gives you access to all APIs above.
    ```c
    // Device Model for {model_name}

    uint64_t {model_name.lower()}_read(void *opaque, hwaddr addr, unsigned size) {{
        // ...
    }}

    void {model_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{
        // ...
    }}

    void {model_name.lower()}_init(ConfigSection* model_info) {{
        // ...
    }}
    ```
    """)

    # Update prompt_start to show composition clearly
    composition_note = textwrap.dedent(f"""\
    ## Model Composition
    - **Trace sources** (primary behavioral data): {', '.join(f'`{s}`' for s in sources)}
    - **Context only** (do not model): {', '.join(f'`{p}`' for p in peripherals if roles[p] == 'CONTEXT') or 'None'}
    - **Output model name**: `{model_name}`

    """)

    # Insert composition_note + ranges_block between the blocks and prompt_end
    return (
        prompt_start
        + composition_note
        + "\n\n".join(blocks)
        + "\n\n"
        + prompt_end
    ).strip()


def iteration_prompt_gen_multiple_periph(
    cm_path_hardware,
    cm_path_emulation,
    peripherals,           # all -p values, used for diff generation
    model_names,           # all -mname values
    model_to_path,         # {model_name: file_path}
    out_dir,
    show_prompt=True,
    max_model_chars=120000,
):
    """
    Generates a single unified correction prompt containing:
    - All model source files (labeled by name and filename)
    - All peripheral diffs (labeled MISMATCH or PASSING)
    - Instruction for LLM to output SEARCH/REPLACE blocks per file
    """
    global qemu_api_list

    if isinstance(peripherals, str):
        peripherals = [peripherals]
    else:
        peripherals = list(peripherals)

    summary_path = os.path.join(cm_path_hardware, "summary.txt")
    platform_name = parse_summary_file(summary_path).get("Platform", "Unknown Platform")

    # ── 1. Run verification for all peripherals ──────────────────────────────
    peripheral_results = {}   # {periph: (not_match, diff_obj)}
    any_mismatch = False

    for periph in peripherals:
        not_match, diff_obj = verify.verify_automata(
            automata1=cm_path_hardware,
            automata2=cm_path_emulation,
            peripheral=periph
        )
        peripheral_results[periph] = (not_match, diff_obj)
        if not_match:
            any_mismatch = True

    if not any_mismatch:
        fastdyn_log.info("All peripherals passing — no correction prompt needed.")
        return None

    # ── 2. Build model source blocks ─────────────────────────────────────────
    model_source_blocks = []
    for model_name in model_names:
        path = model_to_path[model_name]
        filename = os.path.basename(path)
        if show_prompt:
            source = Path(path).read_text(encoding="utf-8", errors="replace")
            if max_model_chars and len(source) > max_model_chars:
                source = source[:max_model_chars] + "\n/* ... truncated ... */"
        else:
            source = "/* model source hidden */"

        model_source_blocks.append(
            f"## Model: `{model_name}`  |  File: `{filename}`\n"
            f"```c\n"
            f"{source}\n"
            f"```"
        )

    # ── 3. Build peripheral diff blocks ──────────────────────────────────────
    peripheral_diff_blocks = []
    for periph in peripherals:
        not_match, diff_obj = peripheral_results[periph]
        status = "MISMATCH" if not_match else "PASSING"

        state_data_section = diff_obj.diff_state_data
        if state_data_section and state_data_section.strip() not in ("", "File is empty.", "File not found: state.txt"):
            state_data_section += "\n" + polling_interpretation_instructions

        peripheral_diff_blocks.append(textwrap.dedent(f"""\
        ## Peripheral: `{periph}`  [{status}]
        {"> This peripheral has trace mismatches that require a fix." if not_match else "> This peripheral is currently passing — do not regress it."}

        ### Initialization Sequence
```
        {diff_obj.diff_init_data}
```

        ### Detected Runtime Loops
```
        {diff_obj.diff_loop_pattern_data}
```

        ### Stateful Behavior Analysis
```
        {state_data_section}
```

        ### Register Entropy Analysis
```
        {diff_obj.diff_entropy_data}
```

        ### Runtime Data Accesses
```
        {diff_obj.diff_runtime_trace}
```

        ### ISR Analysis
```
        {diff_obj.isr_analysis_data}
```
        """).strip())

    # ── 4. Build file target reference ───────────────────────────────────────
    file_reference = "\n    ".join(
        f"- `{model_name}` → `{os.path.basename(model_to_path[model_name])}`"
        for model_name in model_names
    )

    mismatch_list = [p for p, (nm, _) in peripheral_results.items() if nm]

    # ── 5. Assemble prompt ───────────────────────────────────────────────────
    prompt_start = textwrap.dedent(f"""\
    Take this prompt independent from previous prompt history.

    You are an expert reverse engineer specializing in embedded systems and \
writing C emulation for peripherals.
    You have read the reference manual for {platform_name} with special \
familiarity with {", ".join(peripherals)}.

    ## Your Task
    The following peripheral(s) have trace mismatches: {", ".join(f"`{p}`" for p in mismatch_list)}.
    Analyze the diffs below and correct whichever model file(s) are responsible.
    A mismatch in one peripheral may be caused by a bug in a *different* model \
(e.g. `ADC1` calling `api_dma_request` with a wrong stream ID will manifest as \
a DMA model failure). Reason across all models before deciding where the fix belongs.

    ## Model Files Under Correction
    You may produce SEARCH/REPLACE blocks for any or all of these files:
    {file_reference}
    Each SEARCH/REPLACE block MUST begin with a comment identifying its target file:
    `// FILE: <filename>`

    ## Available APIs
```c
    {qemu_api_list.strip()}
```

    ### NOTE
    If a required API is missing, stop and ask the user to provide its \
signature and semantics.

    {framework_rules}

    ## Host-Side I/O Setup (if needed)
    If any model needs host I/O, output exact host command(s). \
If none, output exactly: No host-side setup required.

    ## Trace Log Schema
    Arrays of: [Operation, Peripheral, Register, Address, Value, PC].
    Pay close attention to Value differences between Hardware and Emulated.

    --- START OF CURRENT MODELS ---

    {chr(10).join(model_source_blocks)}

    --- END OF CURRENT MODELS ---

    --- START OF ANALYSIS DATA ---

    ## Platform: {platform_name}

    """).strip()

    prompt_end = textwrap.dedent(f"""\
    --- END OF ANALYSIS DATA ---

    Based **only** on the data above, output:

    ### 1. Cross-Model Failure Analysis
    For each [MISMATCH] peripheral, identify:
    - The root cause
    - Which model file contains the bug (may differ from the peripheral's own model)
    - Whether fixing it risks regressing any [PASSING] peripheral

    ### 2. Register Analysis
    Bulleted list of registers involved in mismatches and their inferred roles.

    {search_replace_prompting}
    """).strip()

    final_prompt = (
        prompt_start + "\n\n"
        + "\n\n".join(peripheral_diff_blocks) + "\n\n"
        + prompt_end
    ).strip()

    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "revised_prompt.txt"
    output_path.write_text(final_prompt + "\n", encoding="utf-8")
    fastdyn_log.info(f"Unified correction prompt written to {output_path}")
    return str(output_path)