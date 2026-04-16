import os
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

#### 2a. MMIO read/write callbacks (`_read` / `_write`)
The framework passes the pointer returned by `_init` as the `opaque` argument to
every subsequent `_read` and `_write` call. Your `_init` **MUST return a pointer to
your state struct** so the framework can wire it up correctly:

```c
static MyPeriphState g_state;

void* myperiph_init(ConfigSection* model_info) {
    memset(&g_state, 0, sizeof(g_state));
    // ... initialization ...
    return &g_state;   // REQUIRED — framework stores this and passes it as opaque
}

uint64_t myperiph_read(void *opaque, hwaddr addr, unsigned size) {
    MyPeriphState *s = (MyPeriphState *)opaque;  // valid: framework set this from init
    hwaddr offset = addr - MY_BASE;
    // ...
}

void myperiph_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    MyPeriphState *s = (MyPeriphState *)opaque;
    hwaddr offset = addr - MY_BASE;
    // ...
}
```

#### 2b. Inter-peripheral / timer callbacks
Any function pointer registered with a framework API such as `api_signal_register`,
`api_dma_register_stream`, or `qemu_plugin_timer_new_ns` **MUST exactly match** the
expected signature, and those callbacks DO receive a valid `opaque` pointer (the one
you passed at registration time). Your callback functions **MUST** accept the `opaque`
parameter and cast it to access your state:

```c
// CORRECT — matches the expected void (*)(void *) signature:
static void my_callback(void *opaque) {
    MyPeriphState *s = (MyPeriphState *)opaque;  // valid: you registered &g_state
    // ...
}

// WRONG — empty parameter list, undefined behavior when caller passes an argument:
static void my_callback() { ... }
```

- **Never cast function pointers** to hide type mismatches. If the compiler warns
  about an incompatible function pointer, fix the function signature instead.
- This applies to ALL inter-peripheral / timer callbacks: timer callbacks, signal
  handlers, DMA request handlers, and any other API that accepts a function pointer
  and an opaque/data pointer.

### 3. Stateful Emulation over Trace Memorization
You must implement a dynamic, stateful model. The trace data is EVIDENCE for
reverse-engineering the register logic — it is NOT a lookup table to copy from.

- Never emit "record-and-replay" code that hardcodes return values from the
  trace (e.g., `if (addr == X && count == N) return Y;`). Such code only works
  for the exact execution that was recorded and breaks on any other run.
- DO maintain a state struct whose fields represent register values.
- DO update state based on firmware writes at runtime (e.g., set/clear bits via
  masking when the firmware writes to a control register).
- DO derive read return values from current state, not from trace constants.
- DO keep the logic minimal — only implement behavior the trace evidence supports.
  Do not invent complex state machines, timers, or hardware features beyond what
  the trace implies.

### 4. Initialization and ConfigSection
The `_init` function receives a `ConfigSection* model_info` pointer that contains
the peripheral's configuration from the platform description (base address, IRQ
numbers, bus connections, etc.). Use it when calling bus-init APIs like
`api_i2c_init_bus(model_info)` or `api_spi_init_bus(model_info)`. Do not
hardcode values that can be read from the configuration.
"""

observability_guidance = """\
## Observability & Host I/O
If the peripheral's firmware-level behavior implies interaction with an external \
entity (e.g., receiving characters on a serial line, toggling user-visible outputs, \
reading sensor data from an external bus), the model MUST expose that interaction to \
the host using the appropriate I/O API from the list above (PTY for serial streams, \
FIFO for generic byte pipes, TAP for network frames, I2C/SPI buses for attached \
devices, etc.). Infer which API to use from the register semantics and trace \
patterns — do not ask the user. The goal is to make the emulated peripheral \
observable and interactive from the host side wherever the real hardware would \
have been observable and interactive to a user or external system.
"""

inter_peripheral_guidance = """\
## Inter-Peripheral Communication
If the peripheral interacts with other hardware blocks at runtime (e.g., triggering \
a DMA transfer after a conversion completes, raising a signal line that another \
peripheral listens on, or sharing an IRQ), express that relationship using the \
framework's inter-peripheral APIs (`api_dma_request`, `api_dma_register_stream`, \
`api_signal_set`, `api_signal_register`, etc.) from the API list above.

Do NOT invent direct function calls into another peripheral model's private code. \
All cross-peripheral interaction MUST go through the shared APIs so that models \
remain independently replaceable.
"""

context_peripheral_instructions = """\
### How to Use Context Peripheral Data
- Do **not** generate a device model for context peripherals.
- Use their trace data only to understand how the target model interacts with \
them at runtime (e.g., which stream IDs are used, when a DMA request is \
triggered, what IRQ line is shared).
- If the target model drives or is driven by a context peripheral, express that \
relationship using the inter-peripheral APIs described above.
"""

host_io_section = """\
## Host-Side I/O Setup (if needed)
After generating the model, provide the exact host command(s) required to create \
and manage any virtual I/O endpoint the model connects to (e.g., a pseudo-terminal, \
a named pipe, a TAP interface). List commands in run order. If no external command \
is required for the peripheral to function, output exactly: \
No host-side setup required.
"""

api_missing_note = """\
### Missing API Policy (HARD BLOCKER)
If the model requires an API that is not listed above, you MUST stop and NOT \
generate the device model. Instead:
1. Clearly name the missing API and explain why it is required for this model.
2. Propose a precise signature for the missing API in the following format:
   - **Name**: `api_<domain>_<action>` (e.g., `api_can_send_frame`)
   - **Inputs**: list each parameter with its C type and a one-line description
   - **Output**: return type and meaning
   - **Description**: one paragraph explaining what the API does and when the \
model would call it
3. STOP here. Do not generate the model. Do not attempt workarounds or \
placeholder implementations. The user will add the API to the framework and \
re-run generation.

Do NOT generate partial or placeholder models when a required API is missing — \
this creates cascading failures in the emulation pipeline.
"""

slave_bug_blocker = """\
### Slave Device Bug Detection (HARD BLOCKER)
If your analysis of the trace mismatches indicates that the root cause is NOT in \
this model but in an attached slave device (e.g., an I2C/SPI slave returning \
wrong data, not ACK-ing correctly, or mishandling a protocol sequence):
1. Do NOT generate SEARCH/REPLACE corrections for the master model — applying \
   patches to the wrong model will introduce new bugs and complicate debugging.
2. State clearly which slave device you suspect and why (cite specific trace \
   evidence: register values, entropy mismatches, protocol violations).
3. Ask the user to provide the slave model source code so you can examine and \
   correct it alongside the master model.

The slave model will have the following callback interface that MUST NOT be changed:
- For I2C slaves: `slave_i2c_event`, `slave_i2c_send`, `slave_i2c_recv`
- For SPI slaves: `slave_spi_set_cs`, `slave_spi_transfer`
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
6. **NEVER leave the REPLACE section empty.** The parser requires at least one line of content between `=======` and `>>>>>>> REPLACE`. To delete a block of code entirely, replace it with a single comment line, for example `/* removed */`. Do NOT produce an empty REPLACE section — this will cause a parse error.
Use this exact format:
// FILE: target_filename.c
<<<<<<< SEARCH
[exact old code to find]
=======
[new code to replace it with — must never be empty; use /* removed */ to delete]
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


# Base QEMU/FastDyn APIs (always available, even in --no-vio ablation mode)
qemu_base_api_list = """
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
"""

# Define the full QEMU API context (base + VIO)
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
- `int api_pty_fd_gen(void)`: Takes no input and returns an integer file descriptor for a host-side pseudo-terminal device. The actual PTY path is determined by the framework at runtime.
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
- `void api_signal_register(int signal_id, signal_handler_t handler, void *opaque)`: Called by a sink peripheral model (for example EXTI). Registers a callback for a logical signal line such as EXTI line 13.
- `void api_signal_set(int signal_id, bool level)`: Called by a source peripheral model (for example GPIO). Publishes the logical level for a signal line. The API is level-based; edge detection belongs in the receiving model.
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

// --- Start of Signals API Info

#define MAX_SIGNALS 128

typedef void (*signal_handler_t)(void *opaque, int signal_id, bool level);

// --- End of Signals API Info

// --- Network Backend APIs (Linux TAP) ---
// 1. Init TAP interface (NON-BLOCKING). Returns fd.
int api_tap_init(const char *dev_name);
// 2. Send raw frame to host.
int api_tap_send(int fd, const uint8_t *buf, int len);
// 3. Poll host for incoming frame. Returns length or -1.
int api_tap_recv_nonblock(int fd, uint8_t *buf, int max_len);
"""

def initial_prompt_gen_multiple_periphs(analysis_dir, model_name, peripherals, out_dir, model_sources, no_vio=False, **kwargs):
    fastdyn_log.info("Generating Prompt for LLM")
    if 'user_obs' in kwargs:
        pass  # Deprecated — observability is now always inferred via observability_guidance

    api_list = qemu_base_api_list if no_vio else qemu_api_list
    if no_vio:
        fastdyn_log.info("Ablation mode: --no-vio enabled. VIO APIs stripped from prompt.")

    final_prompt = generate_prompt_multiple(
        analysis_dir=analysis_dir,
        model_name=model_name,
        peripherals=peripherals,
        qemu_api_list=api_list,
        model_sources=model_sources
    )

    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "initial_prompt.txt"
    output_path.write_text(final_prompt + "\n", encoding="utf-8")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return str(output_path)



def initial_prompt_gen(analysis_dir, peripheral, out_dir, no_vio=False):
    fastdyn_log.info("Generating Prompt for LLM")
    if no_vio:
        fastdyn_log.info("Ablation mode: --no-vio enabled. VIO APIs stripped from prompt.")
    # Construct the path from the base directory and the peripheral name
    peripheral_path = os.path.join(analysis_dir, peripheral)

    final_prompt = generate_prompt(peripheral_path, no_vio=no_vio)

    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "initial_prompt.txt"
    output_path.write_text(final_prompt + "\n", encoding="utf-8")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return str(output_path)



def iteration_prompt_gen(diff_obj, peripheral, out_dir, device_model_path, show_prompt=True, max_model_chars=120000):
    '''
    Based on the difference object, create a correction prompt using SEARCH/REPLACE
    blocks. This is consistent with the multi-peripheral verifier flow.
    '''
    platform_name = diff_obj.platform_name
    peripheral_name = peripheral
    device_model = ''
    if not show_prompt:
        fastdyn_log.warning(
            "show_prompt=False: model source will be hidden from LLM. "
            "SEARCH/REPLACE corrections will likely fail."
        )
    model_filename = os.path.basename(device_model_path)

    if show_prompt:
        device_model = Path(device_model_path).read_text(encoding="utf-8", errors="replace")
        if max_model_chars and len(device_model) > max_model_chars:
            device_model = device_model[:max_model_chars] + "\n/* ... truncated ... */"

    # Build the state data with LLM-facing instructions appended
    state_data_with_instructions = diff_obj.diff_state_data
    if state_data_with_instructions and not state_data_with_instructions.strip().startswith("[NO DATA"):
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

{api_missing_note}

{framework_rules}

{observability_guidance}

{inter_peripheral_guidance}

{slave_bug_blocker}

{host_io_section}

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

## Rare Value Transitions (`rare_transitions.txt`):
These are read values from LOW-entropy (status/control) registers that did NOT appear in the dominant loop patterns above. They represent infrequent but important state changes that the model MUST handle correctly. Pay close attention to these values — they reveal how the register behaves in a different operational phase (e.g. data available vs. idle).
```
{diff_obj.rare_transitions_data}
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

## ISR Analysis (`isr_analysis.txt`)
This file contains information about IRQs.
```
{diff_obj.isr_analysis_data}
```

--- END OF ANALYSIS DATA ---

Based **only** on the data provided above, output:

## Required Output Format:

### 1. Previous Model Failure Analysis
A concise, one-paragraph summary explaining the most likely reason for the mismatches.

### 2. Register Analysis
A bulleted list of the important registers mentioned in the traces and their inferred functions.

{search_replace_prompting}
'''
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "revised_prompt.txt"
    output_path.write_text(final_prompt + "\n", encoding="utf-8")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return str(output_path)

def read_file_content(filepath: str) -> str:
    """Reads the entire content of a file, returning a helpful message if not found."""
    if not os.path.exists(filepath):
        return f"[NO DATA — file not found: {os.path.basename(filepath)}]"
    try:
        with open(filepath, 'r') as f:
            content = f.read().strip()
            return content if content else "[NO DATA — file is empty]"
    except Exception as e:
        return f"[NO DATA — read error: {e}]"

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

def generate_prompt(peripheral_directory: str, no_vio: bool = False) -> str:
    """
    Generates a detailed prompt for an LLM based on analysis files in a directory.

    Args:
        peripheral_directory: Path to the analysis directory for the peripheral.
        no_vio: If True, strip VIO APIs from the prompt (ablation mode).
    """
    if not os.path.isdir(peripheral_directory):
        raise FileNotFoundError(
            f"Analysis directory not found: {peripheral_directory}. "
            f"Ensure you have run the analysis script first."
        )

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
    rare_transitions_data = read_file_content(os.path.join(peripheral_directory, "rare_transitions.txt"))
    loop_files = sorted(glob.glob(os.path.join(peripheral_directory, "loop_pattern_*.txt")))
    isr_analysis_data = read_file_content(isr_analysis)

    loop_data_list = []
    if not loop_files:
        loop_data_list.append("No repeating loop patterns were detected for this peripheral.")
    else:
        for loop_file in loop_files:
            loop_data_list.append(f"--- Contents of {os.path.basename(loop_file)} ---\n{read_file_content(loop_file)}")
    loop_data = "\n\n".join(loop_data_list)

    # Select API list based on ablation mode
    active_api_list = qemu_base_api_list if no_vio else qemu_api_list

    # Assemble the prompt, escaping literal curly braces {{ and }} in the C code example
    prompt = f"""
Take this prompt independent from previous prompt history.

You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name.lower()} peripheral.
Your task is to analyze the following summary of MMIO trace data and generate a complete C device model.

## Available APIs
You **must** use the following APIs to construct the device model. Pay close attention to the read/write callback signatures.
```c
{active_api_list.strip()}
```

{api_missing_note}

{framework_rules}

{observability_guidance}

{inter_peripheral_guidance}

{host_io_section}

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

## Rare Value Transitions (`rare_transitions.txt`):
These are read values from LOW-entropy (status/control) registers that did NOT appear in the dominant loop patterns above. They represent infrequent but important state changes that the model MUST handle correctly. Pay close attention to these values — they reveal how the register behaves in a different operational phase (e.g. data available vs. idle).
```
{rare_transitions_data}
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

## ISR Analysis (`isr_analysis.txt`)
This file contains information about IRQs.
```
{isr_analysis_data}
```

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

static {peripheral_name}State g_{peripheral_name.lower()};

// This function will emulate all device reads
uint64_t {peripheral_name.lower()}_read(void *opaque, hwaddr addr, unsigned size) {{
    {peripheral_name}State *s = ({peripheral_name}State *)opaque;
    hwaddr offset = addr - {peripheral_name.upper()}_BASE;
    // ... return register value based on offset ...
}}

// This function will emulate all device writes
void {peripheral_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{
    {peripheral_name}State *s = ({peripheral_name}State *)opaque;
    hwaddr offset = addr - {peripheral_name.upper()}_BASE;
    // ... update register state based on offset and value ...
}}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* {peripheral_name.lower()}_init(ConfigSection* model_info) {{
    memset(&g_{peripheral_name.lower()}, 0, sizeof(g_{peripheral_name.lower()}));
    // ... initialize state from model_info (bus inits, etc.) ...
    return &g_{peripheral_name.lower()};
}}
```
"""
    return prompt.strip()

def generate_prompt_no_encoder(hardware_log: str, peripheral_name: str, platform_name: str,
                               peripheral_ranges: list = None, no_vio: bool = False) -> str:
    """Generate a prompt using the raw I/O trace instead of the Encoder's compact automaton.

    This is the A1 ablation variant. The prompt contains:
    - The raw io.log (filtered to the target peripheral's address range if known)
    - The VIO API definitions (unless no_vio is also set)
    - The same framework rules and output format as the full pipeline

    No SVD annotations, no pattern mining, no entropy analysis, no ISR detection.

    Args:
        hardware_log: Path to the raw hardware_log/io.log file.
        peripheral_name: Name of the peripheral (e.g., "UART0", "SPI0").
        platform_name: Platform identifier (e.g., "Max78000").
        peripheral_ranges: Optional list of (start, end) address tuples to filter trace lines.
        no_vio: If True, also strip VIO APIs (combines A1+A3 ablation).
    """
    # Read the raw trace
    with open(hardware_log, "r", encoding="utf-8") as f:
        raw_trace = f.read()

    # Filter to peripheral address range if provided
    if peripheral_ranges:
        filtered_lines = []
        for line in raw_trace.splitlines():
            # Check if any address in the line falls within the peripheral ranges
            keep = False
            for start, end in peripheral_ranges:
                if f"0x{start:08X}" in line.upper() or f"0x{start:08x}" in line.lower():
                    keep = True
                    break
                # Also try matching the address field directly
                if "address" in line.lower() or "0x" in line:
                    try:
                        # Extract hex address from the line
                        import re
                        addr_match = re.search(r'address\s*=\s*(0x[0-9a-fA-F]+)', line)
                        if addr_match:
                            addr = int(addr_match.group(1), 16)
                            if start <= addr <= end:
                                keep = True
                                break
                    except (ValueError, AttributeError):
                        pass
            if keep or not peripheral_ranges:
                filtered_lines.append(line)
        raw_trace = "\n".join(filtered_lines)

    active_api_list = qemu_base_api_list if no_vio else qemu_api_list

    prompt = f"""
Take this prompt independent from previous prompt history.

You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name.lower()} peripheral.
Your task is to analyze the following raw MMIO I/O trace and generate a complete C device model.

## Available APIs
You **must** use the following APIs to construct the device model. Pay close attention to the read/write callback signatures.
```c
{active_api_list.strip()}
```

{api_missing_note}

{framework_rules}

--- START OF RAW I/O TRACE ---

## Platform:
{platform_name}

## Peripheral Name:
{peripheral_name}

## Raw Hardware I/O Trace:
The following is the raw MMIO trace captured during hardware execution.
Each line represents a single register read or write with timestamp, address, value, and size.
You must infer the register layout, initialization sequence, runtime behavior, and interrupt patterns from this trace.
```
{raw_trace}
```

--- END OF RAW I/O TRACE ---

Based **only** on the trace data provided above, generate the complete C source code for the device model. Follow the required output format precisely.

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

static {peripheral_name}State g_{peripheral_name.lower()};

// This function will emulate all device reads
uint64_t {peripheral_name.lower()}_read(void *opaque, hwaddr addr, unsigned size) {{
    {peripheral_name}State *s = ({peripheral_name}State *)opaque;
    hwaddr offset = addr - {peripheral_name.upper()}_BASE;
    // ... return register value based on offset ...
}}

// This function will emulate all device writes
void {peripheral_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{
    {peripheral_name}State *s = ({peripheral_name}State *)opaque;
    hwaddr offset = addr - {peripheral_name.upper()}_BASE;
    // ... update register state based on offset and value ...
}}

// MUST return pointer to state — framework passes it as opaque to _read/_write
void* {peripheral_name.lower()}_init(ConfigSection* model_info) {{
    memset(&g_{peripheral_name.lower()}, 0, sizeof(g_{peripheral_name.lower()}));
    // ... initialize state from model_info (bus inits, etc.) ...
    return &g_{peripheral_name.lower()};
}}
```
"""
    return prompt.strip()


def _strip_commented_out_blocks(source: str, min_run: int = 10) -> str:
    """Remove large contiguous blocks of //-commented lines from C source.

    Inline comments that appear alongside active code are left intact.
    Only runs of `min_run` or more consecutive lines where every non-blank
    line starts with '//' are removed — these are almost always dead code
    blocks that were commented out, not useful documentation.
    """
    lines = source.splitlines()
    result = []
    i = 0
    while i < len(lines):
        # Scan forward to measure the length of a //-only run starting at i
        j = i
        while j < len(lines):
            stripped = lines[j].strip()
            if stripped == '' or stripped.startswith('//'):
                j += 1
            else:
                break
        run_len = j - i
        if run_len >= min_run:
            # Drop the block; skip to end of run
            i = j
        else:
            result.extend(lines[i:j])
            i = j
            if i < len(lines):
                result.append(lines[i])
                i += 1
    return '\n'.join(result)


def slave_model_gen(peripheral_name, platform_name, out_dir, slave_firmware_path, reference_model_path, slave_type="i2c"):
    '''
    based on the slave firmware code and the existing reference code, generate a C model for the slave.
    '''
    fastdyn_log.info("Generating Prompt for LLM")

    with open(slave_firmware_path, 'r') as f:
        slave_firmware = _strip_commented_out_blocks(f.read().strip())

    with open(reference_model_path, 'r') as f:
        reference_model = f.read().strip()

    if slave_type == "spi":
        slave_callbacks = """\
    These are the slave's registered callbacks — the ONLY entry points the framework calls on the slave.
    Do not rename them or add new registration functions.
    - `slave_spi_set_cs`
    - `slave_spi_transfer`
"""
    else:
        slave_callbacks = """\
    These are the slave's registered callbacks — the ONLY entry points the framework calls on the slave.
    Do not rename them or add new registration functions.
    - `slave_i2c_event`
    - `slave_i2c_send`
    - `slave_i2c_recv`
"""

    slave_gen_prompt = f"""
Take this prompt independent from previous prompt history.

You are an expert reverse engineer specializing in embedded systems and writing C emulation for peripherals. You have read the reference manual for {platform_name} with special familiarity with {peripheral_name.lower()} peripheral.
Your task is to analyze the following firmware of the {peripheral_name} slave and a reference c model.

## Available APIs
You **must** use only the following APIs. Pay close attention to signatures.
```c
{qemu_api_list.strip()}
```

{api_missing_note}

## Slave Callback Interface
{slave_callbacks}

{framework_rules}

{observability_guidance}

{inter_peripheral_guidance}

{host_io_section}

## Slave Firmware
{slave_firmware}

## Slave Model Reference Code
{reference_model}

## Expected Slave Model Structure
Your generated slave model MUST follow this structural pattern:
```c
// 1. State struct with register array and protocol state
typedef struct {{
    uint8_t regs[256];
    bool    expect_reg_addr;   // I2C: next byte is register pointer
    uint8_t reg_ptr;           // current register pointer
    bool    inited;
}} <name>_state_t;

static <name>_state_t g_state;

// 2. Defaults loader (called on init and soft-reset)
static void load_defaults(<name>_state_t *s) {{ ... }}

// 3. Lazy init guard
static void lazy_init(void) {{ ... }}

// 4. The REQUIRED callback symbols (do NOT rename):
//    I2C: slave_i2c_event, slave_i2c_send, slave_i2c_recv
//    SPI: slave_spi_set_cs, slave_spi_transfer

// 5. Optional name-based aliases: <name>_send, <name>_receive, etc.
```

## Required Output Format

### 1. High-Level Summary
A concise, one-paragraph summary of this slave device's purpose and protocol behavior.

### 2. Register Map
A bulleted list of the slave's internal registers and their functions, inferred from the firmware and reference model.

### 3. C Slave Model Source Code
The complete, self-contained C source file. Including `<device.h>` and `<boardrunner/vio.h>` gives you access to all APIs listed above.
The code must compile as a shared library (`gcc -shared -fPIC -O2 -o slave.so <file>.c`).
"""

    Path(out_dir).mkdir(parents=True, exist_ok=True)
    output_path = Path(out_dir) / "initial_prompt.txt"
    output_path.write_text(slave_gen_prompt + "\n", encoding="utf-8")

    fastdyn_log.info(f"Prompt generated and can be accessed in the file {output_path}")
    return str(output_path)

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

def generate_prompt_multiple(analysis_dir, model_name, peripherals, qemu_api_list, model_sources):
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
        isr_analysis_data = read_file_content(isr_path)

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

    ## Your Task
    Generate a **single, complete C device model** for: `{model_name}`

    The analysis data below covers **multiple peripherals**: {", ".join(peripherals)}.
    One of these is the **target** (`{model_name}`) — the model you are generating.
    The others are **context peripherals** — provided only because `{model_name}` has a \
    runtime dependency on them (e.g., it triggers DMA, shares a bus, raises an IRQ to another block).

    ### Dependency Check
    Before generating code, briefly state:
    1. Which peripheral(s) are context (not the target).
    2. What the runtime relationship is between `{model_name}` and each context peripheral.
    3. Which API(s) from the list below implement that relationship.

    If you cannot infer a meaningful relationship from the trace data, and the context peripheral \
    data provides no useful information for modeling `{model_name}`, state that clearly and \
    **ignore** the context peripheral — do not let it block code generation.

    ## Available APIs
    You **must** use only the following APIs. Pay close attention to signatures.
    ```c
    {qemu_api_list.strip()}
    ```

    {api_missing_note}

    {framework_rules}

    {observability_guidance}

    {inter_peripheral_guidance}

    {context_peripheral_instructions}

    {host_io_section}

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
        MyState *s = (MyState *)opaque;  // valid: framework passes return value of _init
        // ...
    }}

    void {model_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{
        MyState *s = (MyState *)opaque;
        // ...
    }}

    // MUST return &g_state — framework stores this and passes it as opaque to _read/_write
    void* {model_name.lower()}_init(ConfigSection* model_info) {{
        // ...
        return &g_{model_name.lower()};
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
    # Iterate all -d entries so every provided model is embedded and patchable,
    # not just the -mname ones. This handles cases like slave models (BME280)
    # or signal-connected peripherals (GPIOD) that have no trace data but are
    # still relevant for the LLM to reason about and potentially correct.
    model_source_blocks = []
    for model_name, path in model_to_path.items():
        filename = os.path.basename(path)
        if show_prompt:
            source = Path(path).read_text(encoding="utf-8", errors="replace")
            if max_model_chars and len(source) > max_model_chars:
                source = source[:max_model_chars] + "\n/* ... truncated ... */"
        else:
            source = "/* model source hidden */"
            fastdyn_log.warning(
                f"show_prompt=False: model source for '{model_name}' will be hidden. "
                "SEARCH/REPLACE corrections will likely fail."
            )

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
        if state_data_section and not state_data_section.strip().startswith("[NO DATA"):
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

        ### Rare Value Transitions
        These are read values from LOW-entropy (status/control) registers that did NOT appear in the dominant loop patterns. They represent infrequent but important state changes that the model MUST handle correctly.
```
        {diff_obj.rare_transitions_data}
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
        This file contains information about IRQs.
```
        {diff_obj.isr_analysis_data}
```
        """).strip())

    # ── 4. Build file target reference ───────────────────────────────────────
    file_reference = "\n    ".join(
        f"- `{model_name}` → `{os.path.basename(path)}`"
        for model_name, path in model_to_path.items()
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

    {api_missing_note}

    {framework_rules}

    {observability_guidance}

    {inter_peripheral_guidance}

    {context_peripheral_instructions}

    {host_io_section}

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
