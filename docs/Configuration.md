# FastDyn Plugin Configuration (`fastdyn_config.toml`)

## 1. Introduction: What is This File?

Welcome to the **FastDyn** project!  
If you're new to **firmware rehosting**—the process of running software from a specific embedded device (like a microcontroller) on a different machine (like your PC)—this document is your guide.

This file, `fastdyn_config.toml`, is the central *blueprint* for a complete **QEMU simulation environment**. Running embedded firmware requires us to precisely mimic its original hardware environment. Instead of relying on a fragile, mile-long command line, we define everything here in a structured, human-readable format.

This approach makes our simulation **reproducible, easy to modify, and simple to share**.

### The Workflow

This file doesn't directly configure QEMU. Instead, a launcher script (e.g., `parse_config.py`) reads this file and translates it into the necessary commands and temporary configuration files that QEMU and our custom plugin understand.

---

## 2. High-Level Structure

The configuration is organized into three main logical sections:

- **[Memory]**: Defines the *address space* of our simulated chip—where the RAM is, its size, and how it's created on your computer.
- **[CPU]**: Configures the virtual processor and provides powerful tools to modify the firmware's behavior as it runs.
- **[Device]**: The most detailed section. This is where we simulate the hardware peripherals (like UART, GPIO, Timers) that the firmware expects to find.

---

## 3. Section Details

### [Memory] Section

In embedded systems, the CPU interacts with peripherals by writing to or reading from specific memory addresses. This is called **Memory-Mapped I/O (MMIO)**.  
This section sets up the foundational memory map for our simulation.

```toml
[Memory]
# The size of the main system RAM. (e.g., "512M", "256K")
main_ram_size = "512M"

# The directory on the host where memory-backing files are stored.
# /dev/shm is a high-performance shared memory directory on Linux.
shared_mem_path = "/dev/shm"

# The filename for the main RAM's backing file on your PC.
main_ram_file = "my_m4_ram3"

# The size of a secondary, shared RAM region (e.g., on-chip SRAM).
shared_ram_size = "512K"

# The filename for the shared RAM's backing file.
shared_ram_file = "my_m4_ram"
```

---

### [CPU] Section

This section configures the virtual processor and allows us to **instrument the firmware**, which is essential for rehosting when we don't have real hardware.

```toml
[CPU]
# The QEMU machine model to use.
machine = "cortexm"

# The specific CPU model.
cpu = "cortex-m7"

# Path to the firmware binary we want to run.
binary = "/path/to/your/RTOSDemo.axf"

# Path to the CMSIS-SVD file (chip datasheet in XML form).
platform_svd_path = "/path/to/your/stm32f4.svd"

# (Optional) Path to a helper Python script for custom analysis.
context_minimizer_path = "/path/to/context_minimizer.py"

# --- Virtual Instructions ---
[[CPU.virtuals]]
    at = "main+1"
    instruction = "raise_irq"
    args = ["TIM_0_IRQ"]

# --- Instruction Modifiers ---
[[CPU.modifiers]]
    at = "main+8"
    patch = "r15 <- r14"
```

---

### [Device] Section

This is the core of **hardware simulation**.  
A **peripheral** is a piece of hardware on the chip (e.g., UART, GPIO, Timer).  
A **Device Model** is software that pretends to be that hardware.

#### [Device.Models] — The Model Registry

This defines the *toolbox* of models available to simulate hardware.

```toml
[Device.Models]
    [Device.Models.elder]
        backend = "stlink"
        scroll_file = "elder_scroll.ini"

    [Device.Models.classic]

    [Device.Models.passthrough]
        backend = "stlink"

    [Device.Models.unhandled]
```

#### Peripheral Definitions

Here, you define actual peripherals (from the SVD file) and assign models to handle them.

**Example: UART**

```toml
[Device.uart]
    ranges = ["0x40011000-0x40011FFF"]
    description = "Main USART peripheral"

    scroll_config = """
[uart]
scroll = "/path/to/your/uart_gen.so"
"""

    [[Device.uart.handlers]]
        model   = "qemu"
        type    = "stm32f2xx-usart"
        enabled = true

    [[Device.uart.handlers]]
        model   = "elder"
        enabled = true
```

---

## 4. How to Add a New Peripheral

To add a new device (e.g., a timer):

1. **Define the Peripheral Table**: Add a new `[Device.timer0]` block.
2. **Set its Range**: Look up the address range in the SVD file or datasheet.
3. **Add Handlers**: Decide which model will simulate it.  
   Start with `classic` or `unhandled` if unsure.
4. *(Optional)* **Add Scroll Config**: If it uses the `elder` model, include its INI settings.

**Example: Adding a Timer**

```toml
[Device.timer0]
    ranges = ["0x40000000-0x400003FF"]
    description = "General-purpose Timer 0"

    [[Device.timer0.handlers]]
        model = "classic"
        enabled = true
```
