"""Shared pytest fixtures for FastDyn unit tests."""

import pytest
import os
import tempfile


@pytest.fixture
def tmp_model_file(tmp_path):
    """Create a temporary C model file with sample content for patching tests."""
    sample_model = """\
#include <device.h>
#include <boardrunner/vio.h>

typedef struct {
    uint32_t regs[16];
    bool active;
} MyState;

static MyState state;

uint64_t my_read(void *opaque, hwaddr addr, unsigned size) {
    hwaddr offset = addr - 0x40020000;
    switch (offset) {
        case 0x00:
            return state.regs[0];
        case 0x04:
            return state.regs[1];
        default:
            return 0;
    }
}

void my_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    hwaddr offset = addr - 0x40020000;
    switch (offset) {
        case 0x00:
            state.regs[0] = value;
            break;
        case 0x04:
            state.regs[1] = value;
            break;
    }
}

void my_init(ConfigSection* model_info) {
    memset(&state, 0, sizeof(MyState));
}
"""
    model_path = tmp_path / "model.c"
    model_path.write_text(sample_model, encoding="utf-8")
    return str(model_path)


@pytest.fixture
def sample_llm_initial_response():
    """Sample LLM response for an initial prompt (full C code)."""
    return """\
### 1. High-Level Summary
This peripheral handles basic GPIO operations.

### 2. Register Analysis
- MODER (0x00): Mode register
- ODR (0x14): Output data register

### 3. C Device Model Source Code
```c
#include <device.h>
#include <boardrunner/vio.h>

typedef struct {
    uint32_t moder;
    uint32_t odr;
} GPIOState;

static GPIOState gpio_state;

uint64_t gpio_read(void *opaque, hwaddr addr, unsigned size) {
    hwaddr offset = addr - 0x58020000;
    switch (offset) {
        case 0x00: return gpio_state.moder;
        case 0x14: return gpio_state.odr;
        default: return 0;
    }
}

void gpio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    hwaddr offset = addr - 0x58020000;
    switch (offset) {
        case 0x00: gpio_state.moder = value; break;
        case 0x14: gpio_state.odr = value; break;
    }
}

void gpio_init(ConfigSection* model_info) {
    memset(&gpio_state, 0, sizeof(GPIOState));
}
```
"""


@pytest.fixture
def sample_llm_revised_response():
    """Sample LLM response for a revised prompt (SEARCH/REPLACE blocks)."""
    return """\
### 1. Previous Model Failure Analysis
The model was not handling register 0x04 reads correctly.

### 2. Register Analysis
- REG0 (0x00): Control register
- REG1 (0x04): Status register (needs auto-clear bits)

### 3. Code Changes

// FILE: model.c
<<<<<<< SEARCH
        case 0x04:
            return state.regs[1];
=======
        case 0x04:
            if (state.active) {
                uint32_t val = state.regs[1];
                state.regs[1] = 0;
                return val;
            }
            return state.regs[1];
>>>>>>> REPLACE
// FILE: model.c
<<<<<<< SEARCH
        case 0x04:
            state.regs[1] = value;
            break;
=======
        case 0x04:
            state.regs[1] = value;
            state.active = true;
            break;
>>>>>>> REPLACE
"""
