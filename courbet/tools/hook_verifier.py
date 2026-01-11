"""
Courbet Hook Verifier

Verifies that the layout requirements specified in the Doxygen comments
of the virtuals file match the actual compiled binary layout.

This gives the reverse engineer confidence that our template-based hooks
will work as expected.

Usage in `.gdbinit`:

```
file <path_to_binary>
set pagination off
source courbet/tools/hook_verifier.py
courbet_verify <path_to_virtuals_file>
```

Then each time you run a different version of ArduPilot, you
will be able to verify that your hooks are still valid.
"""

import gdb
import yaml
import re
from tools.utils import *

# TODO: Add support for function parameters and return types. This will
# make our verification more robust and provide more confidence to the
# user

def get_offset_size(class_name: str, field_name: str):
    """
    Return (offset, size) for a C++ class member using GDB's Python API.
    """
    t = gdb.lookup_type(class_name)

    # Strip typedefs if needed
    t = t.strip_typedefs()

    for f in t.fields():
        if f.name == field_name:
            offset = f.bitpos // 8
            size = f.type.sizeof
            return offset, size

    raise KeyError(f"[{RED_TEXT}FAIL{END_COLOR}]     Field '{field_name}' not found in class '{class_name}'")


def injest_doxygen_requirements(virtuals_path: str, virtuals_txt_file: str) -> (bool, dict):
    """Injest Doxygen requirements for verification of slice-based execution"""
    requirements = {}
    try:
        with open(virtuals_path, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        gdb.write(
            f"[{RED_TEXT}FAIL{END_COLOR}]     Could not open virtuals file at path: {virtuals_path}\n",
            gdb.STDERR,
        )
        return (False, requirements)

    inside_requirements = False
    index = 0
    for line in lines:
        line = line.strip()
        if line.startswith("* @requirements"):
            # Extract the requirement name if present "* @requirements.<name>"
            name = line[len("* @requirements."):].strip()
            print(f"Found requirements section: {name}")
            relevant = False
            with open(virtuals_txt_file, 'r') as txt_file:
                for txt_line in txt_file:
                    if name in txt_line:
                        relevant = True
                        break
            if not relevant:
                print(f"Skipping irrelevant requirements section: {name}")
                continue
            inside_requirements = True
            requirements[index] = []
        elif line.startswith("* @end_requirements"):
            inside_requirements = False
            index += 1
        elif inside_requirements and line.startswith("*"):
            requirements[index].append(line[2:])

    parsed_requirements = {}
    for idx, lines in requirements.items():
        yaml_str = "\n".join(lines).strip()
        # print(yaml_str)
        if not yaml_str:
            parsed_requirements[idx] = {}
            continue

        try:
            parsed_requirements[idx] = yaml.safe_load(yaml_str)
        except yaml.YAMLError as e:
            gdb.write(
                f"[{RED_TEXT}FAIL{END_COLOR}]     YAML parse error in slice {idx}:\n{e}\n",
                gdb.STDERR,
            )
            parsed_requirements[idx] = None
            return (False, parsed_requirements)

    return (True, parsed_requirements)


def verify_layout(requirements):
    for struct, sym_off_size in requirements.items():
        execute_str = f"ptype /o class {struct}"
        out = gdb.execute(execute_str, to_string=True)
        for symbol, off_size in sym_off_size.items():
            expected_offset = None
            expected_size = None
            if isinstance(off_size, dict):
                expected_offset = off_size.get("offset")
                expected_size = off_size.get("size")
            else:
                expected_offset = off_size

            try:
                actual_offset, actual_size = get_offset_size(struct, symbol)
            except KeyError as e:
                gdb.write(f"[{RED_TEXT}FAIL{END_COLOR}]     During verification: {e}\n", gdb.STDERR)
                continue

            if expected_offset is not None and expected_size is not None:
                if actual_offset != expected_offset:
                    gdb.write(
                        f"[{RED_TEXT}FAIL{END_COLOR}]     MISMATCH in {struct}.{symbol}: "
                        f"expected offset 0x{expected_offset:x}, "
                        f"got 0x{actual_offset:x}\n",
                        gdb.STDERR,
                    )
                    continue

                if actual_size != expected_size:
                    gdb.write(
                        f"[{RED_TEXT}FAIL{END_COLOR}]     MISMATCH in {struct}.{symbol}: "
                        f"expected size 0x{expected_size:x}, "
                        f"got 0x{actual_size:x}\n",
                        gdb.STDERR,
                    )
                    continue

                gdb.write(
                    f"[{GREEN_TEXT}VERIFIED{END_COLOR}] MATCH in {struct}.{symbol}: "
                    f"offset 0x{actual_offset:x}, size 0x{actual_size:x}\n",
                )



def entry(virtuals_path: str, virtuals_txt_file: str):
    gdb.write(
        "\n"
        "/**\n"
        f" * {BLUE_TEXT}@brief{END_COLOR} Courbet Hook Verifier\n"
        " *\n"
        " * Verifies that the layout requirements specified in the Doxygen comments\n"
        f" * of the {LIGHT_BLUE_TEXT}{virtuals_path}{END_COLOR} file match the actual compiled\n"
        " * binary layout.\n"
        " *\n"
        " *\n"
        " * To use this on your own hooks, modify your doxygen comments to include\n"
        " * the @requirements section as shown in the examples in\n"
        f" * the {LIGHT_BLUE_TEXT}virtual/ardurover_virtuals.c{END_COLOR} file.\n"
        " */\n\n"
    )

    success, requirements = injest_doxygen_requirements(virtuals_path=virtuals_path, virtuals_txt_file=virtuals_txt_file)

    if not success:
        gdb.write("\n")
        return

    if not requirements:
        gdb.write(
            f"[{YELLOW_TEXT}WARNING{END_COLOR}] No requirements found in virtuals file: {LIGHT_BLUE_TEXT}{virtuals_path}{END_COLOR}\n"
        )

    for _, req in requirements.items():
        verify_layout(req)

    gdb.write("\n")

class CourbetVerify(gdb.Command):
    """courbet_verify ARG1 ARG2 ..."""

    def __init__(self):
        super().__init__("courbet_verify", gdb.COMMAND_USER)
        gdb.execute(f"set prompt ({BLUE_TEXT}courbet{END_COLOR}) ")

    def invoke(self, arg, _):
        argv = gdb.string_to_argv(arg)

        if len(argv) != 2:
            gdb.write(
                f"[{RED_TEXT}FAIL{END_COLOR}]     Usage: courbet_verify <virtuals_file_path> <virtuals_txt_file_path>\n",
                gdb.STDERR,
            )
            return

        entry(argv[0], argv[1])

CourbetVerify()