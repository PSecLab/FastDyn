# Automatic Rehosting of ArduPilot

ArduPilot can be automatically rehosted using FastDyn.

## How it works

Step 1: Extract binary artifacts from the ArduPilot firmware.
```bash
fastdyn static-analyze -c configs/rover462.toml
```
