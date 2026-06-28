"""
One of the supported targets by Fastdyn: QEMU.
"""

import os
import re
import shutil
import subprocess
import signal

from ..utils import helper, twintrace
from ..binary import binary_wrange
import logging

FASTDYN_DEFAULT_WORKDIR = "fastdyn_work"

from .. import fastdyn_log as fastdyn_log_conf
from .. import profiling, timing
log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()


def _dedup_preserve_order(items):
    seen = set()
    out = []
    for x in items:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


def _bool01(v: bool) -> str:
    return "1" if bool(v) else "0"


def _prepare_python_endpoint_pty(pty_path):
    if os.environ.get("FASTDYN_KEEP_ENDPOINT_PTY", "").lower() in {"1", "true", "yes"}:
        return

    try:
        if os.path.islink(pty_path):
            os.unlink(pty_path)
            log.info("Removed stale Python endpoint PTY symlink: %s", pty_path)
    except OSError as exc:
        log.warning("Failed to remove stale Python endpoint PTY symlink %s: %s", pty_path, exc)


_MEMORY_SIZE_RE = re.compile(r"^(0x[0-9a-fA-F]+|\d+)\s*([kmgtpeKMGTPE]?)(?:i?[bB])?$")
_MEMORY_SIZE_MULTIPLIERS = {
    "": 1,
    "K": 1024,
    "M": 1024 ** 2,
    "G": 1024 ** 3,
    "T": 1024 ** 4,
    "P": 1024 ** 5,
    "E": 1024 ** 6,
}


def _parse_memory_size(size):
    if isinstance(size, int):
        return size

    text = str(size).strip()
    match = _MEMORY_SIZE_RE.match(text)
    if not match:
        raise ValueError(f"Unsupported QEMU memory size: {size!r}")

    value = int(match.group(1), 0)
    suffix = match.group(2).upper()
    return value * _MEMORY_SIZE_MULTIPLIERS[suffix]


def _memory_backend_is_file(memory):
    backend = getattr(memory, "memory_backend", None)
    backend_name = getattr(backend, "name", str(backend)).upper()
    return backend_name == "FILE"


def _ensure_memory_backend_file(memory):
    if not _memory_backend_is_file(memory):
        return

    memory_file = getattr(memory, "memory_file", None)
    if not memory_file:
        raise ValueError(f"Memory {memory.memory_name!r} uses a file backend but has no memory_file.")

    size_bytes = _parse_memory_size(memory.memory_size)
    memory_dir = os.path.dirname(memory_file)
    if memory_dir:
        os.makedirs(memory_dir, exist_ok=True)

    with open(memory_file, "ab"):
        pass

    if os.path.getsize(memory_file) != size_bytes:
        os.truncate(memory_file, size_bytes)
        fastdyn_log.info(
            f"Prepared QEMU memory backend {memory_file} with size {memory.memory_size}."
        )


def _path_has_separator(path):
    return os.sep in path or (os.altsep is not None and os.altsep in path)


def _ensure_executable(executable, description):
    if os.path.isabs(executable) or _path_has_separator(executable):
        exists = os.path.isfile(executable) and os.access(executable, os.X_OK)
    else:
        exists = shutil.which(executable) is not None

    if exists:
        return

    raise FileNotFoundError(
        f"{description} not found or not executable: {executable}\n"
        "Run `source ./setup.sh --build-qemu` or build the patched QEMU fork "
        "at the path configured by [Machine].qemu_path."
    )
def _abs_path(path, base=None):
    if path is None:
        return None

    path = os.path.expanduser(str(path))
    if os.path.isabs(path):
        return os.path.normpath(path)

    if base is not None:
        return os.path.abspath(os.path.join(base, path))

    return os.path.abspath(path)


def _resolve_path(path, bases):
    if path is None:
        return None

    path = os.path.expanduser(str(path))
    if os.path.isabs(path):
        return os.path.normpath(path)

    for base in bases:
        if base is None:
            continue
        candidate = os.path.abspath(os.path.join(base, path))
        if os.path.exists(candidate):
            return candidate

    first_base = next((base for base in bases if base is not None), None)
    return _abs_path(path, first_base)


def _fastdyn_source_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))


def _agentic_fuzz_cwd_from_script(script_path):
    script_dir = os.path.dirname(script_path)
    if os.path.basename(script_dir) == "src":
        return os.path.dirname(script_dir)
    return script_dir


def _extract_monitor_port(qemu_cmd):
    """
    Parses `-monitor tcp:127.0.0.1:<port>,server,nowait` from the cmd list.
    Returns int port or None.
    """
    try:
        i = qemu_cmd.index("-monitor")
        spec = qemu_cmd[i + 1]  # tcp:127.0.0.1:5555,server,nowait
        if not spec.startswith("tcp:"):
            return None
        # take last ':' segment up to first ','
        host_port = spec.split(",", 1)[0]
        port_str = host_port.rsplit(":", 1)[1]
        return int(port_str)
    except Exception:
        return None

def _extract_plugin_path(qemu_cmd):
    """
    Parses `--plugin <plugin_path>,k=v,...` from the cmd list.
    Returns plugin path or None.
    """
    try:
        i = qemu_cmd.index("--plugin")
        spec = qemu_cmd[i + 1]
        return spec.split(",", 1)[0].strip()
    except Exception:
        return None


def _build_qemu_env(qemu_cmd):
    """
    Build an environment for QEMU execution with runtime library paths.

    This avoids requiring users to manually export LD_LIBRARY_PATH for
    common Fastdyn runtime dependencies.
    """
    env = os.environ.copy()
    existing_paths = [p for p in env.get("LD_LIBRARY_PATH", "").split(":") if p]
    extra_paths = []

    plugin_path = _extract_plugin_path(qemu_cmd)
    if plugin_path:
        plugin_abs = os.path.abspath(plugin_path)
        plugin_dir = os.path.dirname(plugin_abs)
        extra_paths.append(plugin_dir)

        # Common repository layout: build/libfastdyn.so
        # Add known sibling runtime-lib directories automatically.
        if os.path.basename(plugin_dir) == "build":
            repo_root = os.path.dirname(plugin_dir)
            extra_paths.append(os.path.join(repo_root, "virtuals/fuzzer/fastdyn_fuzz_lib/target/release"))
            extra_paths.append(os.path.join(repo_root, "device_models/postmartem/verifier"))
            extra_paths.append(os.path.join(repo_root, "out/deps/cjson/install/lib"))
            extra_paths.append(os.path.join(repo_root, "out/deps/cjson/install/lib64"))

    extra_paths = [p for p in extra_paths if os.path.isdir(p)]
    merged = _dedup_preserve_order(extra_paths + existing_paths)
    if merged:
        env["LD_LIBRARY_PATH"] = ":".join(merged)

    return env

def _reset_file_memory_backends(machine):
    """
    Clear persistent file-backed RAM images before launching QEMU.

    QEMU's memory-backend-file maps the given file as RAM. If that file is
    reused, stale bytes from a previous firmware run can survive into the next
    boot. Truncating keeps the configured path but lets QEMU grow a fresh image.
    """
    if not getattr(machine.qemu_target_opts, "reset_memory_files", False):
        return

    for memory in (machine.memories or {}).values():
        if not _memory_backend_is_file(memory):
            continue

        memory_file = getattr(memory, "memory_file", "")
        if not memory_file:
            continue

        memory_dir = os.path.dirname(memory_file)
        if memory_dir:
            os.makedirs(memory_dir, exist_ok=True)

        try:
            with open(memory_file, "wb"):
                pass
            size_bytes = _parse_memory_size(memory.memory_size)
            os.truncate(memory_file, size_bytes)
        except OSError as exc:
            raise RuntimeError(f"Unable to reset memory backend file {memory_file!r}: {exc}") from exc

        fastdyn_log.info(f"Reset file-backed memory image: {memory_file}")

def build_qemu_cmd(machine, dev_config_path, out_path):
    """
    Builds the full qemu command from machine + qemu_target_opts.
    Supports N CPUs as SMP if they are homogeneous.
    """
    if not machine.cpus:
        raise ValueError("Machine has no CPUs configured.")

    cpus = machine.cpus
    cpu0 = cpus[0]
    opts = machine.qemu_target_opts
    print_command = opts.print_command
    _ensure_executable(opts.qemu_path, "QEMU executable")

    # QEMU CLI is per-instance: we support SMP (homogeneous CPU model/binary/machine).
    for c in cpus[1:]:
        if (c.machine != cpu0.machine) or (c.cpu != cpu0.cpu) or (c.binary != cpu0.binary) or (c.arch != cpu0.arch):
            raise ValueError(
                "Multiple CPUs are configured but are not homogeneous. "
                "Current QEMU runner supports SMP only (same machine/cpu/binary/arch)."
            )

    # ------------------------ Memory: main is mandatory ------------------------
    memories = machine.memories or {}
    main_memory = memories.get("main")
    if main_memory is None:
        raise ValueError("Machine.memories must contain a 'main' memory entry.")

    main_mem_id = main_memory.memory_id  # this is what -machine memory-backend must refer to
    share_flag = "on" if getattr(main_memory, "memory_share", True) else "off"
    _ensure_memory_backend_file(main_memory)

    # ------------------------ Base QEMU command ------------------------
    cmd = [opts.qemu_path]

    qmp_socket = opts.qmp_socket
    # if user left default /tmp/qmp.sock, it will collide across runs; prefer per-run socket
    if not qmp_socket or qmp_socket == "/tmp/qmp.sock":
        qmp_socket = os.path.join(out_path, "qmp.sock")

    cpu_configs = [
        "-machine", f"{cpu0.machine},memory-backend={main_mem_id}",
        "-cpu", cpu0.cpu,
        "-kernel", cpu0.binary,
        "-qmp", f"unix:{qmp_socket},server,nowait",
        "-monitor", f"tcp:127.0.0.1:{opts.monitor_port},server,nowait",
    ]

    icount = str(os.environ.get("FASTDYN_QEMU_ICOUNT", opts.icount or "")).strip()
    if icount.lower() not in ("", "none", "off", "false", "0"):
        cpu_configs.extend(["-icount", icount])

    log_options = str(cpu0.log_options or "").strip()
    if log_options.lower() not in ("", "none", "off", "false", "0"):
        qemu_log_file = cpu0.log_file
        if qemu_log_file and not os.path.isabs(qemu_log_file):
            qemu_log_file = os.path.join(out_path, qemu_log_file)
        cpu_configs.extend(["-d", log_options])
        if qemu_log_file:
            cpu_configs.extend(["-D", qemu_log_file])

    if len(cpus) > 1:
        cpu_configs.extend(["-smp", str(len(cpus))])

    if opts.enable_gdb:
        gdb_port = int(getattr(opts, "gdb_port", 1234))
        log.info("GDB debugging enabled on port %s.", gdb_port)
        cpu_configs.extend(["-gdb", f"tcp::{gdb_port}"])
    if opts.stop_on_start:
        cpu_configs.append("-S")

    if opts.semihosting:
        cpu_configs.extend(["--semihosting", "--semihosting-config", opts.semihosting_config])

    # init_nsvtor is effectively machine-wide for Cortex-M
    init_nsvtor = getattr(cpu0, "init_nsvtor", 0)
    if init_nsvtor not in (None, 0):
        cpu_configs.extend(["-global", f"armv7m.init-nsvtor={init_nsvtor}"])
    else:
        if not helper.is_elf(cpu0.binary):
            raise ValueError("Not an ELF (raw dump/bin). Need init_nsvtor in the configuration.")
        nsvtor_elf = helper.elf_file_parser(cpu0.binary)
        cpu_configs.extend(["-global", f"armv7m.init-nsvtor={nsvtor_elf}"])

    cmd.extend(cpu_configs)

    # ------------------------ Virtuals & Modifiers (aggregate across CPUs) ------------------------
    virtuals_dir = os.path.join(out_path, "virtuals")
    os.makedirs(virtuals_dir, exist_ok=True)

    virtuals_path = os.path.join(virtuals_dir, "virtuals.txt")
    modifiers_path = os.path.join(virtuals_dir, "modifiers.txt")

    all_virtuals = []
    all_modifiers = []

    # This is useful for a user that was using fastdyn for one project and
    # wants a more readable TOML file for the next project.
    if cpu0.exstng_config_path:
        existing_config_path = os.path.join(cpu0.exstng_config_path, "virtuals.txt")
        with open(existing_config_path, "r") as f:
            existing_virtuals = f.readlines()
        existing_config_path = os.path.join(cpu0.exstng_config_path, "modifiers.txt")
        with open(existing_config_path, "r") as f:
            existing_modifiers = f.readlines()
        all_virtuals.extend(existing_virtuals)
        all_modifiers.extend(existing_modifiers)

    for c in cpus:
        all_virtuals.extend(getattr(c, "virtuals", []) or [])
        all_modifiers.extend(getattr(c, "modifiers", []) or [])

    all_virtuals = _dedup_preserve_order(all_virtuals)
    all_modifiers = _dedup_preserve_order(all_modifiers)

    log.info(f"Virtual Instructions available at {virtuals_path}")
    with open(virtuals_path, "w") as f:
        f.write("\n".join(all_virtuals) + ("\n" if all_virtuals else ""))

    log.info(f"Modifier Instructions available at {modifiers_path}")
    with open(modifiers_path, "w") as f:
        f.write("\n".join(all_modifiers) + ("\n" if all_modifiers else ""))

    # ------------------------ Memory objects ------------------------
    memory_config = []

    # main memory object + globals
    memory_config.extend([
        "-object",
        f"memory-backend-file,id={main_memory.memory_id},mem-path={main_memory.memory_file},"
        f"size={main_memory.memory_size},share={share_flag}",
        "-global",
        f"{cpu0.machine}-soc.ram_baseaddr0={main_memory.memory_start}",
    ])

    # additional memories (stable order: by key name)
    extra_keys = sorted([k for k in memories.keys() if k != "main"])
    for idx, key in enumerate(extra_keys, start=1):
        m = memories[key]
        share_flag = "on" if getattr(m, "memory_share", True) else "off"
        _ensure_memory_backend_file(m)
        memory_config.extend([
            "-object",
            f"memory-backend-file,id={m.memory_id},mem-path={m.memory_file},size={m.memory_size},share={share_flag}",
            "-global",
            f"{cpu0.machine}-soc.ram_baseaddr{idx}={m.memory_start}",
            "-global",
            f"{cpu0.machine}-soc.ram_backend{idx}={m.memory_id}",
        ])

    cmd.extend(memory_config)

    #-----------------generate twin trace binary from the hardware log------------------------
    replay_hardware_log = getattr(cpu0, "hardware_trace", None)
    twintrace_opt = getattr(cpu0, "twintrace", None)
    replay_binary = None

    if twintrace_opt == "replay":
        #create a binary for faster parsing of hardware log for the replayer
        replay_binary = os.path.join(out_path, "replay_bin.ttbin")
        fastdyn_log.info(replay_hardware_log)

        # Decide what to put in the .ttbin based on the TOML's replay-time
        # routing. Two replay modes the framework supports (per paper §RQ2,
        # tab:rq1_sixcase_f103, columns "Replay (all trace)" vs
        # "Replay (target model only)"):
        #
        #   * All-trace:    every peripheral routes through twintrace at
        #                   replay time. The .ttbin must contain every
        #                   recorded event. Heuristic: no device has the
        #                   passthrough handler enabled.
        #
        #   * Target-only:  only the target peripheral(s) replay; the rest
        #                   stay on passthrough (live HW). The .ttbin must
        #                   contain only the target's events, otherwise
        #                   replay's index walks past records that QEMU
        #                   never delivers (bookkeeping divergence at idx=0).
        #                   Heuristic: at least one device has the
        #                   passthrough handler enabled.
        #
        # The filter axis is **address range / IRQ vector**, NOT the
        # [model] tag in the io.log. Tags reflect the recording-time
        # routing (often "everything on twintrace" so the same trace can
        # serve both replay columns); ranges/IRQs reflect the replay-time
        # routing the user is configuring now.
        twintrace_devices = [
            dev for dev in machine.devices.values()
            if any(h.model == "twintrace" and h.enabled for h in dev.handlers)
        ]
        if not twintrace_devices:
            raise ValueError(
                "twintrace = 'replay' requires at least one [Device.X] "
                "with a twintrace handler enabled; none found."
            )

        passthrough_enabled = any(
            h.model == "passthrough" and h.enabled
            for dev in machine.devices.values()
            for h in dev.handlers
        )

        target_ranges = None
        target_irqs = None
        if passthrough_enabled:
            target_ranges = []
            target_irqs = []
            for dev in twintrace_devices:
                for r in (dev.supported_ranges or []):
                    target_ranges.append((int(r[0]), int(r[1])))
                for v in (dev.irq_range or []):
                    target_irqs.append(int(v))

        twintrace.convert(replay_hardware_log, replay_binary,
                          target_ranges=target_ranges,
                          target_irqs=target_irqs)
        #quick verification
        twintrace.replay_binary_verifier(replay_binary)
    elif twintrace_opt == "None":
        twintrace_opt= "off"

    # ------------------------ Fuzzer ------------------------
    if (opts.coverage):
        binary_wrange.run(f"{out_path}/bin-writable-ranges", cpu0.binary)

    # ------------------------ Plugin ------------------------
    plugin_lib = cpu0.plugin_library
    for c in cpus[1:]:
        if getattr(c, "plugin_library", None) != plugin_lib:
            raise ValueError("Different plugin_library across CPUs is not supported (QEMU loads plugins per instance).")
    if not os.path.isfile(plugin_lib):
        raise FileNotFoundError(
            f"FastDyn QEMU plugin not found: {plugin_lib}\n"
            "Build it from the FastDyn repository root after patched QEMU is ready, for example:\n"
            "  make qemu_path=../qemu PHY=true FLIGHT_CONTROLLERS=true FMU=true"
        )

    # ------------------------ Agentic Fuzzer ----------------
    fastdyn_root = _fastdyn_source_root()
    work_dir_abs = _abs_path(out_path or FASTDYN_DEFAULT_WORKDIR)
    binary_abs = _resolve_path(cpu0.binary, [os.getcwd(), fastdyn_root])

    agentic_fuzz_plugin = []
    if opts.agentic_fuzz:
        if not opts.coverage:
            raise ValueError("agentic_fuzz=true requires coverage=true")

        default_agentic_fuzz_root = os.path.join(
            fastdyn_root,
            "virtuals",
            "fuzzer",
            "agentic",
        )
        default_agentic_fuzz_script = os.path.join(default_agentic_fuzz_root, "monitor.py")
        agentic_fuzz_script = _resolve_path(
            opts.agentic_fuzz_script or default_agentic_fuzz_script,
            [os.getcwd(), fastdyn_root, default_agentic_fuzz_root],
        )
        agentic_fuzz_cwd = work_dir_abs
        agentic_fuzz_in_dir = _resolve_path(
            opts.agentic_fuzz_in_dir or "corpus",
            [work_dir_abs],
        )
        agentic_fuzz_out_dir = _resolve_path(
            opts.agentic_fuzz_out_dir or "corpus-agentic",
            [work_dir_abs],
        )

        agentic_fuzz_plugin = [
            "agentic_fuzz=1",
            f"agentic_fuzz_python={opts.agentic_fuzz_python or 'python3'}",
            f"agentic_fuzz_script={agentic_fuzz_script}",
            f"agentic_fuzz_cwd={agentic_fuzz_cwd}",
            f"agentic_fuzz_binary={binary_abs}",
            f"agentic_fuzz_work_dir={work_dir_abs}",
            f"agentic_fuzz_in_dir={agentic_fuzz_in_dir}",
            f"agentic_fuzz_out_dir={agentic_fuzz_out_dir}",
            f"agentic_fuzz_coverage={os.path.join(work_dir_abs, 'bbl.txt')}",
            f"agentic_fuzz_regions={os.path.join(work_dir_abs, 'bin-writable-ranges')}",
            f"agentic_fuzz_snapshot={os.path.join(work_dir_abs, 'snapshot.bin')}",
            f"agentic_fuzz_log={os.path.join(work_dir_abs, 'agentic_fuzz-monitor.log')}",
            f"agentic_fuzz_taint={_bool01(opts.agentic_fuzz_taint)}",
        ]
        if opts.agentic_fuzz_model:
            agentic_fuzz_plugin.append(f"agentic_fuzz_model={opts.agentic_fuzz_model}")

    # ------------------------ Introspection ------------------------
    introspection = cpu0.introspect
    if introspection:
        introspection_schema = cpu0.introspect_schema
        schema_path = os.path.join(out_path, "schema.txt")
        with open(schema_path, "w") as f:
            f.write(introspection_schema)
            fastdyn_log.info(f"Introspection Schema Available at: {schema_path}")

        introspect_plugin = [
            f"introspection={introspection}",
            f"introspection_schema={schema_path}"
        ]

    plugin_kv = [
        f"{plugin_lib},dev={dev_config_path}",
        f"virtual={virtuals_path}",
        f"modifier={modifiers_path}",
        f"coverage={_bool01(opts.coverage)}",
        f"twintrace={twintrace_opt}",
        f"twintrace_binary={replay_binary}",
    ]

    if getattr(machine, "fmu_path", None):
        plugin_kv.append(f"fmu={machine.fmu_path}")
    if getattr(machine, "fmu_name", None):
        plugin_kv.append(f"fmu_name={machine.fmu_name}")
    for name, value in sorted(getattr(machine, "fmu_parameters", {}).items()):
        if "," in name or "=" in name:
            raise ValueError(f"FMU parameter name cannot contain ',' or '=': {name!r}")
        plugin_kv.append(f"fmu_param_{name}={value:.17g}")
    for name, value in sorted(getattr(machine, "fmu_value_references", {}).items()):
        if "," in name or "=" in name:
            raise ValueError(f"FMU value reference name cannot contain ',' or '=': {name!r}")
        plugin_kv.append(f"fmu_vr_{name}={int(value)}")

    if opts.probe_run:
        plugin_kv.append(f"probe_run={_bool01(opts.probe_run)}")
    if opts.probe_faults:
        plugin_kv.append(f"probe_faults={opts.probe_faults}")
    if opts.probe_milestones:
        plugin_kv.append(f"probe_milestones={opts.probe_milestones}")
    if opts.probe_ignores:
        plugin_kv.append(f"probe_ignores={opts.probe_ignores}")
    if opts.probe_out_dir:
        plugin_kv.append(f"probe_out_dir={opts.probe_out_dir}")

    timer_irq_period_ns = os.environ.get(
        "FASTDYN_TIMER_IRQ_PERIOD_NS",
        getattr(opts, "timer_irq_period_ns", None),
    )
    if isinstance(timer_irq_period_ns, str):
        timer_irq_period_ns = timer_irq_period_ns.strip()
    if timer_irq_period_ns not in (None, "", "none", "off", "false", "0", False):
        timer_irq_period_ns = int(timer_irq_period_ns)
        if timer_irq_period_ns <= 0:
            raise ValueError("[Machine].timer_irq_period_ns must be positive")
        plugin_kv.append(f"timer_irq_period_ns={timer_irq_period_ns}")

    if (introspection):
        plugin_kv.extend(introspect_plugin)

    plugin_kv.extend(agentic_fuzz_plugin)

    if opts.finline is not None:
        plugin_kv.append(f"finline={opts.finline}")
    cmd.extend(["--plugin", ",".join(plugin_kv)])

    #TODO: Fix it
    cmd.extend(["-nographic"])

    # ------------------------ GDB command ------------------------
    gdb_cmd, launch_gdb, binary = get_gdb_cmd(machine, out_path)

    if print_command:
        fastdyn_log.info(f"Running following qemu command:\n{' '.join(cmd)}")
    return cmd, gdb_cmd, launch_gdb, binary


def get_gdb_cmd(machine, out_path):
    """
    Returns (gdb_cmd, launch_gdb, binary) to match your call sites.
    """
    opts = machine.qemu_target_opts
    cpu0 = machine.cpus[0]

    binary = cpu0.binary
    launch_gdb = bool(opts.enable_gdb)
    gdb_cmd = None

    if launch_gdb:
        gdb_port = int(getattr(opts, "gdb_port", 1234))
        gdb_script_path = os.path.join(out_path, "gdb_init.txt")
        with open(gdb_script_path, "w") as f:
            f.write(f"target remote localhost:{gdb_port}\n")

        if opts.launch_gdb:
            gdb_cmd = ["xterm", "-e", f"gdb-multiarch -x {gdb_script_path} {binary}"]

    return gdb_cmd, launch_gdb, binary


def setup_qemu(machine, work_dir=None):
    """
    Prepares output dir, writes device config, returns QEMU and GDB commands.
    """
    with timing.phase("qemu.setup"):
        _reset_file_memory_backends(machine)
        with timing.phase("qemu.write_device_config"):
            dev_config_path = helper.write_dev_config_json(output_dir=work_dir, data=machine.parsed_device)

        with timing.phase("qemu.build_command"):
            qemu_cmd, gdb_cmd, launch_gdb, binary = build_qemu_cmd(machine, dev_config_path, work_dir)
    return qemu_cmd, gdb_cmd, launch_gdb, binary


def start_execution(qemu_cmd, launch_gdb, gdb_cmd, binary, python_endpoints=None):
    """
    Starts QEMU. If enabled, optionally launches a GDB terminal.
    """

    # best-effort cleanup based on the monitor port in this command
    port = _extract_monitor_port(qemu_cmd)
    if port is not None:
        with timing.phase("qemu.kill_existing", monitor_port=port):
            kill_qemu_process(port)

    with timing.phase("qemu.build_env"):
        qemu_env = _build_qemu_env(qemu_cmd)

    endpoint_procs = []
    for ep_path, dev_name in python_endpoints or []:
        pty_path = f"/tmp/{dev_name}_pty"
        _prepare_python_endpoint_pty(pty_path)
        log.info("Launching Python endpoint: %s on %s", ep_path, pty_path)
        with timing.phase("qemu.endpoint.popen", endpoint=ep_path):
            endpoint_procs.append(subprocess.Popen(["python3", ep_path, pty_path]))

    effective_cmd = profiling.wrap_perf_command(
        qemu_cmd,
        name="qemu",
        work_dir=os.environ.get("FASTDYN_WORK_DIR"),
    )
    with timing.phase("qemu.popen", command=" ".join(effective_cmd[:8])):
        qemu_proc = subprocess.Popen(effective_cmd, env=qemu_env)
    timing.mark("qemu.pid", child_pid=qemu_proc.pid)
    log.info("Letting QEMU run.")

    if launch_gdb:
        if gdb_cmd is not None:
            with timing.phase("qemu.gdb.popen"):
                subprocess.Popen(gdb_cmd)
        else:
            log.info(f"Connect by running: gdb-multiarch {binary}")

    try:
        with timing.phase("qemu.wait"):
            ret = qemu_proc.wait()
            log.info(f"QEMU exited with code {ret}")
    except KeyboardInterrupt:
        try:
            qemu_proc.terminate()
        except Exception:
            pass
        if port is not None:
            kill_qemu_process(port)
    finally:
        for ep_proc in endpoint_procs:
            try:
                ep_proc.terminate()
            except Exception:
                pass


def kill_qemu_process(port):
    """
    Best-effort kill whatever is bound to the QEMU monitor TCP port.
    Still not ideal, but at least it uses the configured port.
    """
    def get_pids_using_port(p):
        try:
            result = subprocess.check_output(["lsof", "-ti", f"tcp:{p}"])
            pids = result.decode().strip().split("\n")
            return [int(pid) for pid in pids if pid.strip()]
        except Exception:
            return []

    for pid in get_pids_using_port(port):
        try:
            os.kill(pid, signal.SIGKILL)
            if os.isatty(0):
                subprocess.run(["stty", "sane"])
        except ProcessLookupError:
            pass
        except Exception as e:
            log.warning(f"Failed to kill PID {pid} on port {port}: {e}")
