import os
import sys
import argparse
import glob
from typing import Dict
import logging

from .. import fastdyn_log as fastdyn_log_conf

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

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

    # Read all other analysis files
    init_data = read_file_content(os.path.join(peripheral_directory, "init.txt"))
    state_data = read_file_content(os.path.join(peripheral_directory, "state.txt"))
    entropy_data = read_file_content(os.path.join(peripheral_directory, "entropy.txt"))
    loop_files = sorted(glob.glob(os.path.join(peripheral_directory, "loop_pattern_*.txt")))
    loop_data_list = []
    if not loop_files:
        loop_data_list.append("No repeating loop patterns were detected for this peripheral.")
    else:
        for loop_file in loop_files:
            loop_data_list.append(f"--- Contents of {os.path.basename(loop_file)} ---\n{read_file_content(loop_file)}")
    loop_data = "\n\n".join(loop_data_list)

    # Define the QEMU API context
    qemu_api_list = """
- `int qemu_plugin_write_memory(unsigned long long addr, uint8_t *mem_buf, int len)`: Writes guest memory.
- `int qemu_plugin_read_memory(unsigned long long addr, uint8_t *mem_buf, int len)`: Reads guest memory.
- `int qemu_plugin_read_register(int reg, uint8_t *buf)`: Reads a register of VM. reg is number of register 0 is R0 in ARM.
- `void qemu_plugin_set_register(uint8_t *mem_buf, int reg)`: Writes a register of VM. reg is number of register 0 is R0 in ARM.
- `void qemu_plugin_raise_irq(int irq)`: Raises an interrupt line.
- `void qemu_plugin_raise_irq(int irq)`: raises an interrupt line.
- `void qemu_plugin_timer_alarm(uint64_t timer_fd, uint64_t delay_ns)`: Accepts a timer's handle and an absolute nanosecond timestamp to arm that timer to fire at the specified moment for one shot.
- `int64_t qemu_plugin_get_virtual_timer(void)`: Returns virtual clock (monotonic up counter) of the system.
- `uint64_t qemu_plugin_timer_new_ns(void (*cb)(void *), void *data)`: Accepts a callback function and user data to create a new, unscheduled timer object for a future one-shot event, returning a uint64_t handle that must be armed manually.
- `uint64_t qemu_plugin_timer_new_period_ns(void (*cb)(void *), void *data, uint64_t period)`: Accepts a callback function, user data, and a nanosecond period to create and arm a periodic timer that executes the callback at each interval, returning a uint64_t timer handle.
- `uint64_t my_unimp_read(void *opaque, hwaddr address, unsigned size)`: Signature for a callback that receives VM MMIO read. address is the address of the MMIO access and size is size of access.
- `void my_unimp_write(void *opaque, hwaddr address, uint64_t value, unsigned size)`: Signature for a callback that receives VM MMIO writes.
- `void dev_debug(char *str)`: Any debug messages must be logged using this function.
- `int api_pty_fd_gen(void)`: Takes no input and returns an integer file descriptor for the pseudo-terminal device /tmp/usart1_pty
- `void api_pty_write_req(int fd, uint8_t value)`: Takes a file descriptor fd and a byte value as input to write the byte to the pseudo-terminal, with no output.
- `int api_pty_read_nonblock(int fd, uint8_t *buff);`: Attempts to read a single byte from the pseudo-terminal fd in non-blocking mode, returning a status.
"""

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

--- END OF ANALYSIS DATA ---

Based **only** on the data provided above, generate the complete C source code for the device model. Follow the required output format precisely.

## Required Output Format:

### 1. High-Level Summary
A concise, one-paragraph summary of this peripheral's likely purpose and overall behavior, considering the platform context.

### 2. Register Analysis
A bulleted list of the important registers mentioned in the traces and their inferred functions.

### 3. C Device Model Source Code
The C source code for MMIO read and write callback for {peripheral_name} emulation and any initialization you need for the emulation only. The code must be fully self-contained and ready to be compiled. Including <device.h> and <devmodels_apis.h> will give you access to all APIs i mentioned.

```c
// Device Model for {peripheral_name}

// Inferred Register Functions:
// ... add registers here ...

// This function will emulation all device reads
uint64_t {peripheral_name.lower()}_read(void *opaque, hwaddr addr, unsigned size) {{{{
    // Example: return device->register; // Return some register value from device
	// ... {peripheral_name.lower()} reads, the retuned value will be emulation of device ...
}}}}

// This function will emulate all device writes
void {peripheral_name.lower()}_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {{{{
        // Example: GPIOG->BSRR = value; // Set PG13 high
        // ... Code that responds to {peripheral_name.lower()} writes to emulated device ...
}}}}

void {peripheral_name.lower()}_init(void *opaque) {{{{
		// Example: memset(&{peripheral_name.lower()}_state, 0, sizeof({peripheral_name.lower()}_state_t));
}}}}
```
"""
    return prompt.strip()
