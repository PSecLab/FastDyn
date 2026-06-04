from __future__ import annotations


_CATEGORY_TEMPLATES = {
    "generated_source": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Generate project build outputs or add the build directory as another source root.",
    },
    "toolchain_newlib": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Add the GCC newlib source path as another source root.",
    },
    "toolchain_libgcc_or_gcc": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Add the GCC source path as another source root.",
    },
    "cmsis_or_dsp": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Add the CMSIS or DSP source path as another source root.",
    },
    "mavlink_generated_or_vendor": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Generate MAVLink headers or add the vendor path as another source root.",
    },
    "missing_or_mismatched_submodule": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Init or update submodules, or add the module path as another source root.",
    },
    "unknown_unresolved": {
        "count": 0,
        "examples": [],
        "suggested_remedy": "Check the original path and add the appropriate source root.",
    },
}


def _classify_path(path: str) -> str:
    lower_path = path.lower()
    if "generated" in lower_path or "dsdlc" in lower_path:
        return "generated_source"
    if "/newlib/" in lower_path or "newlib-" in lower_path:
        return "toolchain_newlib"
    if "/libgcc/" in lower_path or "gcc-arm-none-eabi" in lower_path or "/gcc/" in lower_path:
        return "toolchain_libgcc_or_gcc"
    if (
        "cmsis" in lower_path
        or "source/transformfunctions" in lower_path
        or "source/basicmathfunctions" in lower_path
        or "source/statisticsfunctions" in lower_path
        or "source/complexmathfunctions" in lower_path
    ):
        return "cmsis_or_dsp"
    if "mavlink" in lower_path:
        return "mavlink_generated_or_vendor"
    if "modules/" in lower_path:
        return "missing_or_mismatched_submodule"
    return "unknown_unresolved"


def summarize_unresolved_entries(entries: list[dict]) -> dict:
    categories = {
        category: {
            "count": template["count"],
            "examples": list(template["examples"]),
            "suggested_remedy": template["suggested_remedy"],
        }
        for category, template in _CATEGORY_TEMPLATES.items()
    }

    unresolved = [
        entry
        for entry in entries
        if not entry.get("exists_locally") and entry.get("original_file")
    ]

    for entry in unresolved:
        path = entry.get("original_file", "") or ""
        category = _classify_path(path)
        categories[category]["count"] += 1
        if path not in categories[category]["examples"] and len(categories[category]["examples"]) < 5:
            categories[category]["examples"].append(path)

    active_categories = {
        category: info
        for category, info in categories.items()
        if info["count"] > 0
    }

    return {
        "total_unresolved": len(unresolved),
        "categories": active_categories,
    }
