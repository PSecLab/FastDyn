"""
One of the supported targets by Fastdyn: QEMU.
"""

import os
import shutil
import subprocess
import signal

from ..utils import helper
import logging

log = logging.getLogger(__name__)

FASTDYN_DEFAULT_WORKDIR = "fastdyn_work"


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

    # ------------------------ Base QEMU command ------------------------
    cmd = [opts.qemu_path]

    # put log file under out_path (avoid scattering)
    qemu_log_file = cpu0.log_file
    if qemu_log_file and not os.path.isabs(qemu_log_file):
        qemu_log_file = os.path.join(out_path, qemu_log_file)

    qmp_socket = opts.qmp_socket
    # if user left default /tmp/qmp.sock, it will collide across runs; prefer per-run socket
    if not qmp_socket or qmp_socket == "/tmp/qmp.sock":
        qmp_socket = os.path.join(out_path, "qmp.sock")

    cpu_configs = [
        "-machine", f"{cpu0.machine},memory-backend={main_mem_id}",
        "-cpu", cpu0.cpu,
        "-kernel", cpu0.binary,
        "-qmp", f"unix:{qmp_socket},server,nowait",
        "-d", cpu0.log_options,
        "-D", qemu_log_file,
        "-monitor", f"tcp:127.0.0.1:{opts.monitor_port},server,nowait",
    ]

    if len(cpus) > 1:
        cpu_configs.extend(["-smp", str(len(cpus))])

    if opts.enable_gdb:
        log.info("GDB debugging enabled on port 1234 (-s).")
        cpu_configs.append("-s")
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
        memory_config.extend([
            "-object",
            f"memory-backend-file,id={m.memory_id},mem-path={m.memory_file},size={m.memory_size},share={share_flag}",
            "-global",
            f"{cpu0.machine}-soc.ram_baseaddr{idx}={m.memory_start}",
            "-global",
            f"{cpu0.machine}-soc.ram_backend{idx}={m.memory_id}",
        ])

    cmd.extend(memory_config)

    # ------------------------ Plugin ------------------------
    plugin_lib = cpu0.plugin_library
    for c in cpus[1:]:
        if getattr(c, "plugin_library", None) != plugin_lib:
            raise ValueError("Different plugin_library across CPUs is not supported (QEMU loads plugins per instance).")

    plugin_kv = [
        f"{plugin_lib},dev={dev_config_path}",
        f"virtual={virtuals_path}",
        f"modifier={modifiers_path}",
        f"coverage={_bool01(opts.coverage)}",
    ]
    if opts.finline is not None:
        plugin_kv.append(f"finline={opts.finline}")

    cmd.extend(["--plugin", ",".join(plugin_kv)])

    # ------------------------ GDB command ------------------------
    gdb_cmd, launch_gdb, binary = get_gdb_cmd(machine, out_path)

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
        gdb_script_path = os.path.join(out_path, "gdb_init.txt")
        with open(gdb_script_path, "w") as f:
            f.write("target remote localhost:1234\n")

        if opts.launch_gdb:
            gdb_cmd = ["xterm", "-e", f"gdb-multiarch -x {gdb_script_path} {binary}"]

    return gdb_cmd, launch_gdb, binary


def setup_qemu(machine, work_dir=None):
    """
    Prepares output dir, writes device config, returns QEMU and GDB commands.
    """
    if work_dir is not None:
        if not os.path.isdir(work_dir):
            log.warning(f"The output directory: {work_dir} passed by the user does not exist.")
    else:
        work_dir = FASTDYN_DEFAULT_WORKDIR

    if os.path.exists(work_dir):
        log.info(f"The output directory already exists at Path {os.path.abspath(work_dir)}. Deleting it!")
        shutil.rmtree(work_dir)

    log.info(f"Creating output directory at path: {os.path.abspath(work_dir)}")
    os.makedirs(work_dir, exist_ok=True)

    dev_config_path = helper.write_dev_config_json(output_dir=work_dir, data=machine.parsed_device)

    qemu_cmd, gdb_cmd, launch_gdb, binary = build_qemu_cmd(machine, dev_config_path, work_dir)
    return qemu_cmd, gdb_cmd, launch_gdb, binary


def start_execution(qemu_cmd, launch_gdb, gdb_cmd, binary):
    """
    Starts QEMU. If enabled, optionally launches a GDB terminal.
    """
    log.info("Running the following QEMU command:")
    log.info(" ".join(qemu_cmd))

    # best-effort cleanup based on the monitor port in this command
    port = _extract_monitor_port(qemu_cmd)
    if port is not None:
        kill_qemu_process(port)

    qemu_proc = subprocess.Popen(qemu_cmd)
    log.info("Letting QEMU run.")

    if launch_gdb:
        if gdb_cmd is not None:
            subprocess.Popen(gdb_cmd)
        else:
            log.info(f"Connect by running: gdb-multiarch {binary}")

    try:
        qemu_proc.wait()
    except KeyboardInterrupt:
        try:
            qemu_proc.terminate()
        except Exception:
            pass
        if port is not None:
            kill_qemu_process(port)


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
            subprocess.run(["stty", "sane"])
        except ProcessLookupError:
            pass
        except Exception as e:
            log.warning(f"Failed to kill PID {pid} on port {port}: {e}")
