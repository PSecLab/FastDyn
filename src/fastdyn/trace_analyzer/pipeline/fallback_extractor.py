import subprocess
import os
import re
import shutil
from pathlib import Path
from typing import Any

from fastdyn.trace_analyzer.models import SourceContext, MacroContext
from fastdyn.binary.binary_utils.macros import (
    load_compile_database,
    prepare_preprocess_command,
    parse_preprocessor_defines,
)
from fastdyn.trace_analyzer.pipeline.source_context import extract_source_context

def demangle_cpp_symbol(mangled_name: str) -> str:
    if not shutil.which("c++filt"):
        return mangled_name
    try:
        proc = subprocess.run(["c++filt", mangled_name], capture_output=True, text=True, check=True)
        return proc.stdout.strip()
    except subprocess.CalledProcessError:
        return mangled_name
    except OSError:
        return mangled_name

def extract_identity(demangled: str) -> tuple[str, str]:
    if "::" in demangled:
        parts = demangled.split("::")
        class_name = parts[-2].strip()
        method_part = parts[-1].strip()
        method_name = method_part.split("(")[0].strip()
        # Remove any templates or weird characters from class_name
        class_name = class_name.split("<")[0].strip()
        return class_name, method_name
    return demangled, demangled

def find_source_line_ctags(file_path: Path, method_name: str) -> int | None:
    if not shutil.which("ctags"):
        return None
    try:
        proc = subprocess.run(["ctags", "-x", str(file_path)], capture_output=True, text=True, check=True)
        for line in proc.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3:
                tag_name = parts[0]
                if tag_name == method_name:
                    try:
                        return int(parts[2])
                    except ValueError:
                        pass
    except (subprocess.CalledProcessError, OSError):
        pass
    return None

def extract_macros(compile_cmd: dict[str, Any], source_code: str) -> MacroContext:
    try:
        from fastdyn.binary.binary_utils.macros import compile_command_args
        args = compile_command_args(compile_cmd)
        source_path = compile_cmd.get("file", "")
        
        cmd_args = prepare_preprocess_command(args, source_path)
        if not cmd_args:
            return MacroContext(warnings=["Failed to prepare preprocess command"])
        
        proc = subprocess.run(
            cmd_args,
            capture_output=True,
            text=True,
            cwd=compile_cmd.get("directory", str(Path.cwd()))
        )
        if proc.returncode != 0:
            return MacroContext(warnings=[f"gcc -dM -E failed: {proc.stderr[:200]}"])
        
        raw_macros = parse_preprocessor_defines(proc.stdout)
        
        # Simple heuristic to include macros that appear in the snippet or are ALWAYS_INCLUDE
        ALWAYS_INCLUDE = {
            "TRUE", "FALSE", "NULL", "STM32_NO_INIT", "HAL_USE_RTC",
            "RCC_APB1ENR_RTCAPBEN", "RCC_APB1ENR_PWREN", "STM32_HSECLK",
            "STM32_HSE_ENABLED", "STM32_PLLM_VALUE", "STM32_PLLN_VALUE", "STM32_SYSCLK"
        }
        
        def_map = {m.get("name"): m for m in raw_macros if m.get("name")}
        
        source_code_set = set(re.findall(r'[A-Za-z_][A-Za-z0-9_]*', source_code))
        source_code_set.update(ALWAYS_INCLUDE)
        
        selected = {}
        queue = list(source_code_set)
        visited = set()
        
        while queue:
            ident = queue.pop(0)
            if ident in visited:
                continue
            visited.add(ident)
            
            if ident in def_map:
                m = def_map[ident]
                k = m.get("name")
                v = m.get("value", "")
                args = m.get("args")
                
                if args is not None:
                    selected[k] = f"#define {k}({args}) {v}"
                else:
                    selected[k] = f"#define {k} {v}"
                    
                if v:
                    new_idents = set(re.findall(r'\b[a-zA-Z_]\w*\b', v))
                    queue.extend(new_idents - visited)
                
        mc = MacroContext(
            provider="rebuild_approx",
            warnings=["These macros were extracted dynamically via the compilation database and are an approximation."],
            context_artifact="compile_commands.json",
        )
        mc.selected_macros = selected
        mc.selected_macro_names = list(selected.keys())
        return mc
    except Exception as e:
        return MacroContext(warnings=[f"Exception during macro fallback: {e}"])

def run_compile_db_fallback(
    func_name: str,
    fw_build_roots: list[str],
) -> tuple[SourceContext | None, MacroContext | None]:
    if not fw_build_roots:
        return None, None

    demangled = demangle_cpp_symbol(func_name)
    class_name, method_name = extract_identity(demangled)

    db_paths = []
    for root in fw_build_roots:
        p = Path(root)
        # Search for compile_commands.json in build subdirectories (e.g., build/CubeBlack)
        db_paths.extend(p.rglob("compile_commands.json"))

    if not db_paths:
        return None, None

    compile_db = load_compile_database(db_paths)

    target_entry = None
    target_path = None
    anchor_line = None

    # First pass: try ctags on all matching files
    for entry in compile_db:
        fpath = entry.get("file", "")
        fname = Path(fpath).name
        if fname.startswith(class_name) and fname.endswith((".cpp", ".c", ".cc", ".cxx")):
            p = Path(fpath)
            if not p.is_absolute():
                p = Path(entry.get("directory", "")) / p
            if p.exists():
                line = find_source_line_ctags(p, method_name)
                if line is not None:
                    target_entry = entry
                    target_path = p
                    anchor_line = line
                    break

    # Second pass: fallback regex if ctags missed it
    if not target_path:
        for entry in compile_db:
            fpath = entry.get("file", "")
            fname = Path(fpath).name
            if fname.startswith(class_name) and fname.endswith((".cpp", ".c", ".cc", ".cxx")):
                p = Path(fpath)
                if not p.is_absolute():
                    p = Path(entry.get("directory", "")) / p
                if p.exists():
                    try:
                        content = p.read_text(errors="replace").splitlines()
                        pattern = re.compile(rf"\b{method_name}\s*\(")
                        for i, line in enumerate(content):
                            if pattern.search(line):
                                target_entry = entry
                                target_path = p
                                anchor_line = i + 1
                                break
                    except OSError:
                        pass
                if target_path:
                    break
            
    if not target_entry or not target_path or not target_path.exists():
        return None, None

    if anchor_line is None:
        anchor_line = 1

    source_slice = extract_source_context(target_path, anchor_line=anchor_line, max_lines=260)
    
    header_path = None
    header_text = None
    if target_path and target_path.suffix in ['.cpp', '.c', '.cc']:
        for ext in ['.h', '.hpp']:
            possible_header = target_path.with_suffix(ext)
            if possible_header.exists():
                header_path = possible_header
                try:
                    with open(possible_header, 'r', encoding='utf-8', errors='replace') as f:
                        lines = f.readlines()
                    stripped_lines = []
                    for h_line in lines:
                        h_line_s = h_line.strip()
                        if h_line_s.startswith('#include'):
                            continue
                        if not h_line_s:
                            continue
                        stripped_lines.append(h_line.rstrip())
                    header_text = '\n'.join(stripped_lines)
                except OSError:
                    pass
                break

    sc = SourceContext(
        function=method_name,
        source_path=target_path,
        source_root_relative=str(target_path),
        line=anchor_line,
        start_line=source_slice.start_line,
        end_line=source_slice.end_line,
        text=source_slice.text,
        extraction=source_slice.extraction,
        provider="compile_db_fallback",
        warnings=source_slice.warnings,
        header_path=header_path,
        header_text=header_text,
    )

    mc = extract_macros(target_entry, sc.text)
    mc.source_root_relative = sc.source_root_relative
    
    return sc, mc
