from pathlib import Path

import pytest

from fastdyn import fmu_build


def make_build(tmp_path: Path, parameters: dict[str, float]) -> fmu_build.FmuBuild:
    output = tmp_path / "fmu"
    output.mkdir()
    return fmu_build.FmuBuild(
        name="quadrotor",
        model="FastDyn.Copter",
        model_file=tmp_path / "Copter.mo",
        source_root=tmp_path,
        extra_source_roots=(),
        output=output,
        repo_root=tmp_path,
        package=False,
        parameters=parameters,
    )


def write_model_description(path: Path, extra_names: tuple[str, ...] = ()) -> None:
    names = (*fmu_build.REQUIRED_VALUE_REFERENCES, *extra_names)
    variables = "\n".join(
        f'    <Float64 name="{name}" valueReference="{idx}" />'
        for idx, name in enumerate(names, start=1)
    )
    (path / "modelDescription.xml").write_text(
        f"""
<fmiModelDescription>
  <ModelVariables>
{variables}
  </ModelVariables>
</fmiModelDescription>
""",
        encoding="utf-8",
    )


def test_value_references_include_configured_fmu_parameters(tmp_path):
    build = make_build(tmp_path, {"pwm_min": 1100.0, "mass": 2.5644001})
    write_model_description(build.output, ("pwm_min", "mass"))

    refs = fmu_build.value_references(build)

    assert refs["pwm_min"] == len(fmu_build.REQUIRED_VALUE_REFERENCES) + 1
    assert refs["mass"] == len(fmu_build.REQUIRED_VALUE_REFERENCES) + 2


def test_value_references_reject_missing_configured_parameter(tmp_path):
    build = make_build(tmp_path, {"not_in_fmu": 1.0})
    write_model_description(build.output)

    with pytest.raises(fmu_build.FmuConfigError, match="not_in_fmu"):
        fmu_build.value_references(build)
