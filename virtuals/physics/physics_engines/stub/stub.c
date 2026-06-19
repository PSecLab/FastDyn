#include "stub.h"

#include <string.h>

static int stub_init(void)
{
    return 1;
}

static int stub_get_imu_batch(imu_batch_t *imu_batch)
{
    if (!imu_batch) {
        return 0;
    }

    memset(imu_batch, 0, sizeof(*imu_batch));
    return 1;
}

static int stub_get_mag_reading(vector3d_t *mag)
{
    if (!mag) {
        return 0;
    }

    memset(mag, 0, sizeof(*mag));
    return 1;
}

static int stub_get_navsat_reading(gps_data_t *gps_data)
{
    if (!gps_data) {
        return 0;
    }

    memset(gps_data, 0, sizeof(*gps_data));
    return 1;
}

static int stub_set_servo_pwm(int channel, int pwm)
{
    (void)channel;
    (void)pwm;
    return 1;
}

static int stub_advance_simulation(double run_until_time)
{
    (void)run_until_time;
    return 1;
}

static int stub_get_joint_state(double *motor_0_pos, double *motor_2_pos)
{
    if (!motor_0_pos || !motor_2_pos) {
        return 0;
    }

    *motor_0_pos = 0.0;
    *motor_2_pos = 0.0;
    return 1;
}

static int stub_get_altimeter_reading(double *altitude)
{
    if (!altitude) {
        return 0;
    }

    *altitude = 0.0;
    return 1;
}

static int stub_get_lidar_samples(rplidar_sample_t *samples, size_t num_samples)
{
    if (!samples && num_samples != 0) {
        return 0;
    }

    if (num_samples != 0) {
        memset(samples, 0, num_samples * sizeof(*samples));
    }

    return 0;
}

phy_backend_t stub_backend = {
    .name = "stub",
    .init = stub_init,
    .shutdown = NULL,
    .get_imu_batch = stub_get_imu_batch,
    .get_mag_reading = stub_get_mag_reading,
    .get_navsat_reading = stub_get_navsat_reading,
    .set_servo_pwm = stub_set_servo_pwm,
    .advance_simulation = stub_advance_simulation,
    .get_joint_state = stub_get_joint_state,
    .get_altimeter_reading = stub_get_altimeter_reading,
    .get_lidar_samples = stub_get_lidar_samples,
};
