from pathlib import Path

from fastdyn import runtime_config


def write_config(path: Path, body: str) -> Path:
    path.write_text(body, encoding="utf-8")
    return path


def test_loads_named_run_processes(tmp_path):
    helpers = tmp_path / "helpers"
    helpers.mkdir()
    config = write_config(
        tmp_path / "fastdyn.toml",
        """
[Run]
cwd = "helpers"

[Run.env]
FAST = "DYN"

[Run.processes.mission]
command = ["python3", "mission.py"]
start_delay_sec = 2.5

[Run.processes.disabled]
enabled = false
command = "never"
""",
    )

    processes = runtime_config.load_processes(config, repo_root=tmp_path)

    assert [process.name for process in processes] == ["mission"]
    process = processes[0]
    assert process.command == ["python3", "mission.py"]
    assert process.cwd == helpers
    assert process.env == {"FAST": "DYN"}
    assert process.ready_message is None
    assert process.start_delay_sec == 2.5


def test_expands_run_process_environment_defaults(tmp_path, monkeypatch):
    monkeypatch.delenv("FASTDYN_TEST_PORT", raising=False)
    config = write_config(
        tmp_path / "fastdyn.toml",
        """
[Run.processes.viewer]
command = ["serve", "--port=${FASTDYN_TEST_PORT:-5010}", "$FASTDYN_MISSING"]
ready_message = "open http://127.0.0.1:${FASTDYN_TEST_PORT:-5010}/"
""",
    )

    processes = runtime_config.load_processes(config, repo_root=tmp_path)

    assert len(processes) == 1
    assert processes[0].command == ["serve", "--port=5010", "$FASTDYN_MISSING"]
    assert processes[0].ready_message == "open http://127.0.0.1:5010/"


def test_expands_run_process_environment_overrides(tmp_path, monkeypatch):
    monkeypatch.setenv("FASTDYN_TEST_PORT", "6200")
    config = write_config(
        tmp_path / "fastdyn.toml",
        """
[Run.processes.viewer]
command = ["serve", "--port=${FASTDYN_TEST_PORT:-5010}"]
ready_message = "open http://127.0.0.1:${FASTDYN_TEST_PORT:-5010}/"
""",
    )

    processes = runtime_config.load_processes(config, repo_root=tmp_path)

    assert processes[0].command == ["serve", "--port=6200"]
    assert processes[0].ready_message == "open http://127.0.0.1:6200/"


def test_loads_rumoca_webviewer_process(tmp_path):
    rumoca_dir = tmp_path / "third_party" / "common" / "rumoca"
    rumoca_dir.mkdir(parents=True)
    (rumoca_dir / "Cargo.toml").write_text("[workspace]\n", encoding="utf-8")
    config = write_config(
        tmp_path / "fastdyn.toml",
        """
[Rumoca]
enabled = true
config = "rumoca_sim.toml"
features = ["lockstep"]

[Rumoca.webviewer]
http_port = 9090
ws_port = 9091
scene = "scene.js"
debug = true
""",
    )

    processes = runtime_config.load_processes(config, repo_root=tmp_path)

    assert len(processes) == 1
    process = processes[0]
    assert process.name == "rumoca"
    assert process.cwd == rumoca_dir
    assert process.background is True
    assert process.shell is False
    assert process.ready_message == "Rumoca web viewer: http://127.0.0.1:9090"
    assert process.command == [
        "cargo",
        "run",
        "-p",
        "rumoca",
        "--features",
        "lockstep",
        "--release",
        "--",
        "lockstep",
        "run",
        "-c",
        str(tmp_path / "rumoca_sim.toml"),
        "--scene",
        str(tmp_path / "scene.js"),
        "--http-port",
        "9090",
        "--ws-port",
        "9091",
        "--debug",
    ]


def test_rumoca_webviewer_ports_can_come_from_environment(tmp_path, monkeypatch):
    monkeypatch.setenv("FASTDYN_RUMOCA_HTTP_PORT", "16104")
    monkeypatch.setenv("FASTDYN_RUMOCA_WS_PORT", "16105")
    rumoca_dir = tmp_path / "third_party" / "common" / "rumoca"
    rumoca_dir.mkdir(parents=True)
    (rumoca_dir / "Cargo.toml").write_text("[workspace]\n", encoding="utf-8")
    config = write_config(
        tmp_path / "fastdyn.toml",
        """
[Rumoca]
enabled = true
config = "rumoca_sim.toml"
features = ["lockstep"]

[Rumoca.webviewer]
http_port = 9090
ws_port = 9091
""",
    )

    processes = runtime_config.load_processes(config, repo_root=tmp_path)

    assert processes[0].ready_message == "Rumoca web viewer: http://127.0.0.1:16104"
    assert "--http-port" in processes[0].command
    assert "16104" in processes[0].command
    assert "--ws-port" in processes[0].command
    assert "16105" in processes[0].command


def test_launch_from_config_passes_standard_environment(tmp_path):
    work_dir = tmp_path / "work"
    work_dir.mkdir()
    marker = tmp_path / "marker.txt"
    config = write_config(
        tmp_path / "fastdyn.toml",
        f"""
[Run.processes.write_marker]
background = false
command = ["sh", "-c", "printf '%s\\n%s\\n' \\"$FASTDYN_CONFIG\\" \\"$FASTDYN_WORK_DIR\\" > {marker}"]
""",
    )

    with runtime_config.launch_from_config(config, work_dir):
        pass

    assert marker.read_text(encoding="utf-8").splitlines() == [
        str(config.resolve()),
        str(work_dir.resolve()),
    ]


def test_configure_run_environment_exports_repo_root(tmp_path, monkeypatch):
    work_dir = tmp_path / "work"
    work_dir.mkdir()
    config = write_config(tmp_path / "fastdyn.toml", "")
    monkeypatch.setattr(runtime_config.fmu_build, "find_repo_root", lambda _path: tmp_path)
    monkeypatch.setenv("FASTDYN_REPO_ROOT", "")

    env = runtime_config.configure_run_environment(config, work_dir)

    assert env["FASTDYN_REPO_ROOT"] == str(tmp_path)


def test_cerebri_config_launches_native_fmi3_bridge(tmp_path, monkeypatch):
    repo_root = Path(__file__).resolve().parents[2]
    config = repo_root / "configs" / "cerebri_cubs2_mr_vmu_tropic.toml"
    cubs2_root = tmp_path / "cerebri_cubs2"
    work_dir = tmp_path / "work"
    work_dir.mkdir()
    monkeypatch.setenv("CEREBRI_CUBS2_ROOT", str(cubs2_root))
    monkeypatch.setenv("FASTDYN_REPO_ROOT", str(repo_root))

    runtime_config.configure_run_environment(config, work_dir)
    processes = runtime_config.load_processes(config, repo_root=repo_root)

    assert [process.name for process in processes] == ["cerebri_cubs2_fmi3"]
    process = processes[0]
    assert process.command[:4] == [
        str(repo_root / "utils" / "cerebri_cubs2_fmi3_bridge.py"),
        "--launch",
        "--cubs2-root",
        str(cubs2_root),
    ]
    assert process.terminate_run_on_exit is True
