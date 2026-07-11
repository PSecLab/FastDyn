#!/usr/bin/env python3
"""Launch upstream CUBS2 FMI3 SIL with FastDyn's native transport adapter."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BRIDGE_CRATE = ROOT / "utils" / "cerebri_cubs2_fmi3_bridge"
BRIDGE_BINARY = BRIDGE_CRATE / "target" / "release" / "cerebri-cubs2-fmi3-bridge"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launch", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--cubs2-root", required=True)
    parser.add_argument("--cubs2-build-dir", required=True)
    parser.add_argument("--artifacts", required=True)
    parser.add_argument("--locator", default="udp/192.0.2.2:7447")
    parser.add_argument("--t-end", type=float, default=40.0)
    parser.add_argument("--startup-timeout-s", type=float, default=60.0)
    parser.add_argument("--sim-speed", type=float, default=1000.0)
    return parser.parse_args()


def bridge_needs_build() -> bool:
    if not BRIDGE_BINARY.is_file():
        return True
    binary_mtime = BRIDGE_BINARY.stat().st_mtime_ns
    inputs = [
        BRIDGE_CRATE / "Cargo.toml",
        BRIDGE_CRATE / "Cargo.lock",
        *(BRIDGE_CRATE / "src").glob("*.rs"),
    ]
    return any(path.stat().st_mtime_ns > binary_mtime for path in inputs)


def build_native_bridge(cubs2_root: Path) -> None:
    if not bridge_needs_build():
        return
    print("Building optimized FastDyn/CUBS2 native lockstep bridge", flush=True)
    build_env = os.environ.copy()
    native_flag = "-C target-cpu=native"
    rustflags = build_env.get("RUSTFLAGS", "").strip()
    if native_flag not in rustflags:
        build_env["RUSTFLAGS"] = f"{rustflags} {native_flag}".strip()
    subprocess.run(
        [
            "nix",
            "develop",
            os.fspath(cubs2_root),
            "--command",
            "cargo",
            "build",
            "--release",
            "--locked",
            "--manifest-path",
            os.fspath(BRIDGE_CRATE / "Cargo.toml"),
        ],
        env=build_env,
        check=True,
    )


def main() -> int:
    args = parse_args()
    cubs2_root = Path(args.cubs2_root).expanduser().resolve()
    cubs2_build_dir = Path(args.cubs2_build_dir).expanduser().resolve()
    artifacts = Path(args.artifacts).expanduser().resolve()
    source_headers = (
        cubs2_build_dir
        / "_deps"
        / "synapse_fbs_c-src"
        / "include"
    )
    upstream_runner = cubs2_root / "tests" / "zephyr" / "run_native_sim_zenoh_sil.py"
    if not upstream_runner.is_file():
        raise FileNotFoundError(f"latest CUBS2 SIL runner not found: {upstream_runner}")
    if not source_headers.is_dir():
        raise FileNotFoundError(
            "CUBS2 hardware build has no generated synapse_fbs headers; "
            f"build the FastDyn firmware in {cubs2_build_dir}: {source_headers}"
        )
    build_native_bridge(cubs2_root)

    # Upstream locates synapse_fbs from SIM.parent.parent. Construct that build
    # shape around the native bridge without modifying the CUBS2 checkout.
    controller_build = artifacts / ".fastdyn-controller-build"
    simulated_executable = controller_build / "zephyr" / "zephyr.exe"
    header_link = controller_build / "_deps" / "synapse_fbs_c-src" / "include"
    simulated_executable.parent.mkdir(parents=True, exist_ok=True)
    header_link.parent.mkdir(parents=True, exist_ok=True)
    simulated_executable.unlink(missing_ok=True)
    header_link.unlink(missing_ok=True)
    simulated_executable.symlink_to(BRIDGE_BINARY)
    header_link.symlink_to(source_headers, target_is_directory=True)

    os.environ["CUBS2_FASTDYN_LOCATOR"] = args.locator
    command = [
        "nix",
        "run",
        f"{cubs2_root}#native-sim-sil-run",
        "--",
        "--sim",
        os.fspath(simulated_executable),
        "--plant-backend",
        "fmi3",
        "--artifacts",
        os.fspath(artifacts),
        "--locator",
        args.locator,
        "--t-end",
        f"{args.t_end:g}",
        "--startup-timeout-s",
        f"{args.startup_timeout_s:g}",
        "--sim-speed",
        f"{args.sim_speed:g}",
    ]
    print("Launching upstream CUBS2 FMI3 lockstep runner", flush=True)
    os.chdir(cubs2_root)
    os.execvp(command[0], command)
    return 127  # pragma: no cover


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"FastDyn/CUBS2 FMI3 launch failed: {exc}", file=os.sys.stderr, flush=True)
        raise SystemExit(1)
