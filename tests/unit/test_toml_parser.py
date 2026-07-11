import pytest

from fastdyn.config_env import expand_env_defaults
from fastdyn.toml_parser import _load_svd_enabled


def test_config_expands_environment_defaults(monkeypatch):
    monkeypatch.delenv("CEREBRI_CUBS2_ROOT", raising=False)
    loaded = expand_env_defaults(
        {"CPU": {"cpu0": [{"binary": "${CEREBRI_CUBS2_ROOT:-../cerebri_cubs2}/zephyr.elf"}]}}
    )

    assert loaded["CPU"]["cpu0"][0]["binary"] == "../cerebri_cubs2/zephyr.elf"


def test_config_prefers_environment_override(monkeypatch):
    monkeypatch.setenv("CEREBRI_CUBS2_ROOT", "/workspace/cubs2")
    assert (
        expand_env_defaults("${CEREBRI_CUBS2_ROOT:-../cerebri_cubs2}/src")
        == "/workspace/cubs2/src"
    )


def test_runtime_svd_loading_can_be_disabled_and_overridden():
    assert not _load_svd_enabled({"load_svd": False}, None)
    assert _load_svd_enabled({"load_svd": False}, True)


def test_runtime_svd_loading_requires_boolean():
    with pytest.raises(TypeError, match=r"\[Machine\]\.load_svd"):
        _load_svd_enabled({"load_svd": "false"}, None)
