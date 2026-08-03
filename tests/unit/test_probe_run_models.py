import sys
import types
from types import SimpleNamespace

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib


cmsis_svd = types.ModuleType("cmsis_svd")
cmsis_svd_parser = types.ModuleType("cmsis_svd.parser")
cmsis_svd_parser.SVDParser = object
sys.modules.setdefault("cmsis_svd", cmsis_svd)
sys.modules.setdefault("cmsis_svd.parser", cmsis_svd_parser)

tomli = types.ModuleType("tomli")
tomli.load = tomllib.load
sys.modules.setdefault("tomli", tomli)

dwarf_provider = types.ModuleType("fastdyn.binary.symmap.providers.dwarf")
dwarf_provider.DwarfProvider = object
sys.modules.setdefault("fastdyn.binary.symmap.providers.dwarf", dwarf_provider)

from fastdyn import main


def _machine_with_connection(tmp_path, connection):
    return SimpleNamespace(
        modeling_dir=str(tmp_path / "boardrunner" / "model"),
        devices={
            "spi1": SimpleNamespace(
                handlers=[
                    SimpleNamespace(
                        model="elder",
                        enabled=True,
                        scroll="boardrunner/build/spi1.so",
                    )
                ],
                connections=[connection],
                slaves=[],
            )
        },
    )


def test_compile_missing_boardrunner_models_checks_slave_device_scroll(tmp_path, monkeypatch):
    existing_handler = tmp_path / "boardrunner" / "build" / "spi1.so"
    existing_handler.parent.mkdir(parents=True)
    existing_handler.touch()

    missing_slave = tmp_path / "boardrunner" / "build" / "ms5611_spi.so"
    machine = _machine_with_connection(
        tmp_path,
        {
            "type": "slave",
            "enabled": True,
            "name": "ms5611_spi1.c",
            "device_scroll": "boardrunner/build/ms5611_spi.so",
        },
    )

    calls = []

    def fake_compile(sdk_dir):
        calls.append(sdk_dir)
        missing_slave.touch()
        return True, ""

    monkeypatch.setattr(main, "_abs_repo_path", lambda path: tmp_path / path)
    monkeypatch.setattr(main, "_compile_model", fake_compile)

    assert main._compile_missing_boardrunner_models(machine) is True
    assert len(calls) == 1


def test_compile_missing_boardrunner_models_fails_if_slave_scroll_stays_missing(tmp_path, monkeypatch):
    existing_handler = tmp_path / "boardrunner" / "build" / "spi1.so"
    existing_handler.parent.mkdir(parents=True)
    existing_handler.touch()

    machine = _machine_with_connection(
        tmp_path,
        {
            "type": "slave",
            "enabled": True,
            "name": "ms5611_spi1.c",
            "device_scroll": "boardrunner/build/ms5611_spi.so",
        },
    )

    monkeypatch.setattr(main, "_abs_repo_path", lambda path: tmp_path / path)
    monkeypatch.setattr(main, "_compile_model", lambda _sdk_dir: (True, ""))

    assert main._compile_missing_boardrunner_models(machine) is False


def test_compile_missing_boardrunner_models_ignores_disabled_slave_connection(tmp_path, monkeypatch):
    existing_handler = tmp_path / "boardrunner" / "build" / "spi1.so"
    existing_handler.parent.mkdir(parents=True)
    existing_handler.touch()

    machine = _machine_with_connection(
        tmp_path,
        {
            "type": "slave",
            "enabled": False,
            "name": "disabled_slave",
            "device_scroll": "boardrunner/build/missing_disabled.so",
        },
    )

    calls = []
    monkeypatch.setattr(main, "_abs_repo_path", lambda path: tmp_path / path)
    monkeypatch.setattr(main, "_compile_model", lambda sdk_dir: calls.append(sdk_dir) or (True, ""))

    assert main._compile_missing_boardrunner_models(machine) is True
    assert calls == []
