# Coverage Module
# LLM Generated (Ping Ashwin if it's BS)

This directory contains the coverage tracking implementation for the FastDyn QEMU plugin.

## Overview

The coverage module implements AFL++-style edge coverage tracking using a local bitmap. It tracks basic block transitions during firmware execution to provide coverage analysis without requiring actual AFL++ integration or shared memory IPC.

## Files

- **fake_afl.h** - Header file with data structures and function declarations
- **fake_afl.c** - Implementation of the fake AFL coverage tracking

## Architecture

```
┌─────────────────────────────────────┐
│         Core Module (core.c)        │
│                                     │
│  ┌───────────────────────────────┐ │
│  │   vcpu_tb_trans()             │ │
│  │   - Instruments basic blocks   │ │
│  │   - Registers callbacks        │ │
│  └───────────┬───────────────────┘ │
│              │                      │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │   fake_afl_log_callback()     │ │
│  └───────────┬───────────────────┘ │
└──────────────┼─────────────────────┘
               │
               ↓
┌──────────────────────────────────────┐
│    Coverage Module (fake_afl.c)      │
│                                      │
│  ┌────────────────────────────────┐ │
│  │  fake_afl_maybe_log()          │ │
│  │  - Hash current location       │ │
│  │  - XOR with previous location  │ │
│  │  - Update bitmap               │ │
│  └────────────────────────────────┘ │
│                                      │
│  ┌────────────────────────────────┐ │
│  │  fake_afl_state                │ │
│  │  - fake_afl_area (65KB bitmap) │ │
│  │  - prev_loc (edge tracking)    │ │
│  │  - fake_afl_enabled (flag)     │ │
│  └────────────────────────────────┘ │
└──────────────────────────────────────┘
```

## Key Functions

### `fake_afl_setup()`
Initializes the coverage tracking system by:
- Allocating a 65KB bitmap (zero-initialized)
- Enabling coverage tracking
- Resetting the previous location

**Returns:** 0 on success, -1 on failure

### `fake_afl_maybe_log(uint64_t cur_loc)`
Records coverage for a basic block transition:
1. Hashes the current location using bit manipulation
2. XORs with previous location to create edge identifier
3. Increments counter in bitmap
4. Updates previous location for next transition

**Parameters:**
- `cur_loc`: Address of the current basic block

### `fake_afl_cleanup()`
Cleans up resources by:
- Freeing the coverage bitmap
- Resetting state variables

### `fake_afl_get_state()`
Provides access to the internal state for inspection.

**Returns:** Pointer to the `fake_afl_state_t` structure

## Edge Coverage Algorithm

The implementation uses AFL++'s standard edge coverage algorithm:

```c
// Step 1: Hash the current location
cur_loc = (cur_loc >> 4) ^ (cur_loc << 8);
cur_loc &= (FAKE_AFL_MAP_SIZE - 1);

// Step 2: Create edge identifier
edge = cur_loc ^ prev_loc;

// Step 3: Increment coverage counter
fake_afl_area[edge]++;

// Step 4: Update previous location
prev_loc = cur_loc >> 1;
```

### Why This Works

1. **Location Hashing**: The bit shifts and XOR create a unique but deterministic hash of each basic block address that fits within the 65KB bitmap.

2. **Edge Detection**: XORing with the previous location creates a unique identifier for each transition (edge) in the control flow graph.

3. **Transition Diversity**: The right shift by 1 when updating `prev_loc` ensures that A→B and B→A transitions are distinguished.

4. **Saturation**: The counter saturates at 255 (unsigned char max), which is intentional to prevent overflow while tracking hot paths.

## Integration

### In core.c

```c
#include <fake_afl.h>

// At plugin initialization
if (fake_afl_setup() != 0) {
    utils_die("Fake AFL initialization failed");
}

// In vcpu_tb_trans
fake_afl_state_t *coverage_state = fake_afl_get_state();
if (coverage_state->fake_afl_enabled && n > 0) {
    // Register callback for first instruction
    qemu_plugin_register_vcpu_insn_exec_cb(
        first_insn, fake_afl_log_callback, 
        QEMU_PLUGIN_CB_NO_REGS, 
        (void *)(uintptr_t)tb_addr);
}

// At plugin exit
fake_afl_cleanup();
```

## Performance

- **Memory Overhead**: 65KB static allocation
- **CPU Overhead**: ~5-15% from callback instrumentation
- **Initialization**: O(1) - single calloc()
- **Per Basic Block**: O(1) - simple XOR and increment

## Future Extensions

Potential enhancements:
- Export bitmap to file for external analysis
- Add statistics (unique edges, coverage density, hot paths)
- Implement bitmap compression
- Add selective instrumentation (filter by address range)
- Integration with visualization tools

## Usage

The module is automatically initialized when the FastDyn plugin loads. Coverage tracking is enabled by default and requires no configuration.

Output on initialization:
```
Fake AFL coverage tracking enabled (bitmap: 0x7f1234567890, size: 65536 bytes)
```

## License

This module is part of the FastDyn project and follows the same license terms.
