"""Tests for fastdyn.llm.response_parser module."""

import pytest
from fastdyn.llm.response_parser import (
    extract_c_code,
    parse_search_replace_blocks,
    ParsingError,
    SearchReplaceBlock,
)


class TestExtractCCode:
    """Tests for extract_c_code()."""

    def test_single_c_block(self):
        response = """\
Some explanation text.

```c
#include <device.h>
uint64_t my_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}
```

More text.
"""
        result = extract_c_code(response)
        assert "#include <device.h>" in result
        assert "my_read" in result
        assert "```" not in result

    def test_multiple_c_blocks_concatenated(self):
        response = """\
First block:
```c
#include <device.h>
typedef struct { uint32_t regs[4]; } State;
```

Second block:
```c
uint64_t my_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}
```
"""
        result = extract_c_code(response)
        assert "typedef struct" in result
        assert "my_read" in result

    def test_no_code_block_raises_error(self):
        response = "This response has no code blocks at all."
        with pytest.raises(ParsingError, match="No fenced C code block"):
            extract_c_code(response)

    def test_generic_code_block_with_c_code(self):
        response = """\
Some text:
```
#include <device.h>
uint64_t my_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}
```
"""
        result = extract_c_code(response)
        assert "my_read" in result

    def test_non_c_generic_block_rejected(self):
        response = """\
```
print("hello world")
x = 42
```
"""
        with pytest.raises(ParsingError, match="No fenced C code block"):
            extract_c_code(response)

    def test_uppercase_c_tag(self):
        response = """\
```C
uint32_t val = 0x1234;
void my_init(ConfigSection* info) { }
```
"""
        result = extract_c_code(response)
        assert "uint32_t" in result

    def test_whitespace_handling(self):
        response = """\
```c
    uint64_t aligned_read(void *opaque, hwaddr addr, unsigned size) {
        return 0;
    }
```
"""
        result = extract_c_code(response)
        # Leading whitespace should be preserved
        assert "    uint64_t" in result


class TestParseSearchReplaceBlocks:
    """Tests for parse_search_replace_blocks()."""

    def test_single_block(self):
        response = """\
Some analysis text.

// FILE: model.c
<<<<<<< SEARCH
    case 0x04:
        return state.regs[1];
=======
    case 0x04:
        return state.regs[1] | 0x01;
>>>>>>> REPLACE
"""
        blocks = parse_search_replace_blocks(response)
        assert len(blocks) == 1
        assert blocks[0].target_file == "model.c"
        assert "return state.regs[1];" in blocks[0].search_text
        assert "| 0x01" in blocks[0].replace_text
        assert blocks[0].block_index == 1

    def test_multiple_blocks(self, sample_llm_revised_response):
        blocks = parse_search_replace_blocks(sample_llm_revised_response)
        assert len(blocks) == 2
        assert blocks[0].block_index == 1
        assert blocks[1].block_index == 2

    def test_no_blocks_raises_error(self):
        response = "This response has no SEARCH/REPLACE blocks."
        with pytest.raises(ParsingError, match="No SEARCH/REPLACE blocks"):
            parse_search_replace_blocks(response)

    def test_malformed_block_missing_separator(self):
        response = """\
// FILE: model.c
<<<<<<< SEARCH
    old code here
>>>>>>> REPLACE
"""
        with pytest.raises(ParsingError, match="missing ======="):
            parse_search_replace_blocks(response)

    def test_malformed_block_missing_replace_marker(self):
        response = """\
// FILE: model.c
<<<<<<< SEARCH
    old code
=======
    new code
"""
        with pytest.raises(ParsingError, match="missing >>>>>>> REPLACE"):
            parse_search_replace_blocks(response)

    def test_block_without_file_header(self):
        response = """\
<<<<<<< SEARCH
    old code
=======
    new code
>>>>>>> REPLACE
"""
        blocks = parse_search_replace_blocks(response)
        assert len(blocks) == 1
        # No FILE header, so target_file should be empty
        assert blocks[0].target_file == ""

    def test_preserves_whitespace_in_search_replace(self):
        response = """\
// FILE: model.c
<<<<<<< SEARCH
    if (state.active) {
        uint32_t val = state.regs[0];
    }
=======
    if (state.active) {
        uint32_t val = state.regs[0];
        state.regs[0] = 0;
    }
>>>>>>> REPLACE
"""
        blocks = parse_search_replace_blocks(response)
        assert "    if (state.active) {" in blocks[0].search_text
        assert "        state.regs[0] = 0;" in blocks[0].replace_text
