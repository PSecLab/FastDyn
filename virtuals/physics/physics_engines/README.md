# Physics Engine Backends

FastDyn physics backends provide sensor data to rehosted flight-controller
firmware and accept actuator commands from it. The active backend for the
maintained ArduPilot vehicle configs is the FMI v3 backend in `fmu/`, using
Rumoca-generated Modelica plants. The Gazebo backend remains available for
legacy Courbet experiments.

## Backend Contract

A backend implements these functions through `phy_backend_t`:

```c
int init(void);
void shutdown(void);
int get_imu_batch(vector3f_t *accel, vector3f_t *gyro, int max_samples);
int get_mag_reading(vector3f_t *mag);
int get_imu_batch_batch(imu_batch_t *batch);
int get_mag_reading_d(vector3d_t *mag);
int get_navsat_reading(gps_data_t *gps_data);
int set_servo_pwm(int channel, int pwm);
int advance_simulation(double run_until_time);
int get_altimeter_reading(double *altitude);
```

Populate the backend struct with pointers to the implementation:

```c
phy_backend_t fmu_backend = {
    .name = "fmu",
    .init = fmu_init,
    .shutdown = fmu_shutdown,
    .get_imu_batch = fmu_get_imu_batch,
    .get_mag_reading = fmu_get_mag_reading,
    .get_navsat_reading = fmu_get_navsat_reading,
    .set_servo_pwm = fmu_set_servo_pwm,
    .advance_simulation = fmu_advance_simulation,
    .get_altimeter_reading = fmu_get_altimeter_reading,
};
```

Register the backend in `virtuals/physics/phy.c`.

## FMI v3 Backend

The FMU backend reads the selected `[FMU]` entry from the FastDyn TOML config.
The maintained FastDyn vehicle wrappers are:

- `FastDyn.Copter`
- `FastDyn.Rover`
- `FastDyn.Plane`

They inherit reusable base plants from
`third_party/common/modelica_models/RigidBody/Examples` and add the
ArduPilot-facing sensor and actuator variables. FastDyn/Rumoca wraps those
vehicle models as FMI v3 artifacts at run time. The Modelica class names
deliberately do not include `FMU`; the generated artifact format is a build
target, not part of the vehicle model identity.

The backend advances synchronously from QEMU timer ticks. The firmware publishes
PWM commands, the backend advances the plant to the requested simulation time,
and ArduPilot reads IMU, magnetometer, GPS, barometer, and joint-state outputs
from that same simulated state.

## Gazebo Backend

`gazebo/` is retained for older Courbet/Gazebo workflows. Use it only when
reproducing those experiments or comparing against Gazebo. New CI and OptiFuzz
coverage uses the FMI v3 backend.
