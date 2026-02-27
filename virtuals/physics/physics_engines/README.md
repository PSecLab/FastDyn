# Creating a Physics Engine Backend for FastDyn

We have a contract with the flight controller to provide the following functions:

```c
int phy_init(void);
int phy_get_imu_batch(vector3f_t *accel, vector3f_t *gyro, int max_samples);
int phy_get_mag_reading(vector3f_t *mag);
```

When you have done this, you can `extern` your backend in your engine's header file and assign it to the `phy_backend` variable. For example, in `gazebo.h`:

```c
extern phy_backend_t gazebo_backend;
```