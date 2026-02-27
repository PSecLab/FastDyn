# Creating a Physics Engine Backend for FastDyn

We have a contract with the flight controller to provide the following functions:

```c
int init(void);
int get_imu_batch(vector3f_t *accel, vector3f_t *gyro, int max_samples);
int get_mag_reading(vector3f_t *mag);
int get_imu_batch_batch(imu_batch_t *batch);
int get_mag_reading_d(vector3d_t *mag);
int get_navsat_reading(gps_data_t *gps_data);
int set_servo_pwm(int channel, int pwm);
int advance_simulation(double run_until_time);
int get_joint_state(double *motor_0_pos, double *motor_2_pos);
```

and populate your backend struct with pointers to your implementations of these functions. For example, in `gazebo.c`:

```c
phy_backend_t gazebo_backend = {
    .name = "gazebo",
    .init = gz_init,
    .shutdown = NULL,
    .get_imu_batch = get_imu_batch,
    .get_mag_reading = get_mag_reading,
    .get_navsat_reading = get_navsat_reading,
    .set_servo_pwm = set_servo_pwm,
    .advance_simulation = advance_simulation,
    .get_joint_state = get_joint_state
};
```

When you have done this, you can `extern` your backend in your engine's header file and assign it to the `phy_backend` variable. For example, in `gazebo.h`:

```c
extern phy_backend_t gazebo_backend;
```