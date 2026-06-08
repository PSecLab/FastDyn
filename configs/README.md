# Automatic Rehosting of ArduPilot

ArduPilot can be automatically rehosted using FastDyn.

## How it works

Step 1: Extract binary and static analysis artifacts from the ArduPilot firmware.
```bash
fastdyn static-analyze -c configs/rover462.toml
```

Step 2: Run the firmware dynamically to probe its execution and generate execution traces.
```bash
fastdyn probe-run -c configs/rover462.toml -o fastdyn_recent_run
```

Step 3: Combine the execution trace data and static artifacts to generate a comprehensive LLM prompt for device modeling.
```bash
fastdyn trace-analyze -c configs/rover462.toml -o fastdyn_work --latest-run-dir fastdyn_recent_run
```
