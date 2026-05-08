This local example is the legacy OpenModelica/FMI 2.0 altimeter harness.

Before using that harness, run:

```bash
source ./build_model.sh
```

For FMI 3.0 generation, use the pinned Rumoca and `modelica_models` submodules
documented in the repository root `README.md`. The current C harness in this
directory includes FMI 2.0 headers and calls `fmi2*` entry points, so it should
not be pointed at an FMI 3.0 FMU until the harness is ported.
