"""Tests for fastdyn.llm.patch module."""

import pytest
from pathlib import Path
from fastdyn.llm.patch import (
    apply_search_replace_patches,
    write_patched_file,
    write_model_file,
    PatchError,
)
from fastdyn.llm.response_parser import SearchReplaceBlock


class TestApplySearchReplacePatches:
    """Tests for apply_search_replace_patches()."""

    def test_single_patch_success(self, tmp_model_file):
        patches = [
            SearchReplaceBlock(
                target_file="model.c",
                search_text="        case 0x04:\n            return state.regs[1];",
                replace_text="        case 0x04:\n            return state.regs[1] | 0x01;",
                block_index=1,
            )
        ]
        result = apply_search_replace_patches(tmp_model_file, patches)
        assert "return state.regs[1] | 0x01;" in result
        assert "return state.regs[1];\n" not in result  # original gone

    def test_multiple_patches_in_sequence(self, tmp_model_file):
        patches = [
            SearchReplaceBlock(
                target_file="model.c",
                search_text="        case 0x00:\n            return state.regs[0];",
                replace_text="        case 0x00:\n            return state.regs[0] & 0xFF;",
                block_index=1,
            ),
            SearchReplaceBlock(
                target_file="model.c",
                search_text="    bool active;",
                replace_text="    bool active;\n    bool enabled;",
                block_index=2,
            ),
        ]
        result = apply_search_replace_patches(tmp_model_file, patches)
        assert "& 0xFF" in result
        assert "    bool enabled;" in result

    def test_search_text_not_found_raises_error(self, tmp_model_file):
        patches = [
            SearchReplaceBlock(
                target_file="model.c",
                search_text="THIS TEXT DOES NOT EXIST IN THE FILE",
                replace_text="replacement",
                block_index=1,
            )
        ]
        with pytest.raises(PatchError, match="Block #1 failed"):
            apply_search_replace_patches(tmp_model_file, patches)

    def test_error_reports_correct_block_index(self, tmp_model_file):
        patches = [
            SearchReplaceBlock(
                target_file="model.c",
                search_text="    bool active;",
                replace_text="    bool active;\n    bool enabled;",
                block_index=1,
            ),
            SearchReplaceBlock(
                target_file="model.c",
                search_text="NONEXISTENT CODE",
                replace_text="replacement",
                block_index=2,
            ),
        ]
        with pytest.raises(PatchError) as exc_info:
            apply_search_replace_patches(tmp_model_file, patches)
        assert exc_info.value.block_index == 2

    def test_only_first_occurrence_replaced(self, tmp_path):
        # Create a file with duplicate text
        content = "hello world\nhello world\n"
        model_path = tmp_path / "dup.c"
        model_path.write_text(content)

        patches = [
            SearchReplaceBlock(
                target_file="dup.c",
                search_text="hello world",
                replace_text="goodbye world",
                block_index=1,
            )
        ]
        result = apply_search_replace_patches(str(model_path), patches)
        assert result.count("goodbye world") == 1
        assert result.count("hello world") == 1

    def test_model_file_not_found(self):
        patches = [
            SearchReplaceBlock(
                target_file="model.c",
                search_text="anything",
                replace_text="replacement",
                block_index=1,
            )
        ]
        with pytest.raises(PatchError, match="Model file not found"):
            apply_search_replace_patches("/nonexistent/path/model.c", patches)

    def test_empty_patches_list(self, tmp_model_file):
        original = Path(tmp_model_file).read_text()
        result = apply_search_replace_patches(tmp_model_file, [])
        assert result == original


class TestWriteModelFile:
    """Tests for write_model_file()."""

    def test_creates_file(self, tmp_path):
        output = tmp_path / "output" / "model.c"
        write_model_file(str(output), "// test content\n")
        assert output.exists()
        assert output.read_text() == "// test content\n"

    def test_creates_parent_directories(self, tmp_path):
        output = tmp_path / "deep" / "nested" / "dir" / "model.c"
        write_model_file(str(output), "content")
        assert output.exists()


class TestWritePatchedFile:
    """Tests for write_patched_file()."""

    def test_overwrites_existing(self, tmp_model_file):
        write_patched_file(tmp_model_file, "// patched content\n")
        assert Path(tmp_model_file).read_text() == "// patched content\n"
