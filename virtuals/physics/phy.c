#include "phy.h"
#include <stdio.h>

static phy_backend_t *active_backend = NULL;

int phy_register_backend(phy_backend_t *backend)
{
    if (!backend) {
        return 0;
    }

    active_backend = backend;

    // printf("Physics backend registered: %s\n", backend->name);
    return 1;
}

int phy_init(void)
{
    if (!active_backend || !active_backend->init) {
        return 0;
    }

    return active_backend->init();
}

int phy_get_imu_batch(imu_batch_t *imu_batch)
{
    if (!active_backend || !active_backend->get_imu_batch) {
        return 0;
    }

    return active_backend->get_imu_batch(imu_batch);
}

int phy_get_mag_reading(vector3d_t *mag)
{
    if (!active_backend || !active_backend->get_mag_reading) {
        return 0;
    }

    return active_backend->get_mag_reading(mag);
}

int phy_get_navsat_reading(gps_data_t *gps_data)
{
    if (!active_backend || !active_backend->get_navsat_reading) {
        return 0;
    }

    return active_backend->get_navsat_reading(gps_data);
}

int phy_set_servo_pwm(int channel, int pwm)
{
    if (!active_backend || !active_backend->set_servo_pwm) {
        return 0;
    }

    return active_backend->set_servo_pwm(channel, pwm);
}

int phy_advance_simulation(double run_until_time)
{
    if (!active_backend || !active_backend->advance_simulation) {
        return 0;
    }

    return active_backend->advance_simulation(run_until_time);
}

int phy_get_joint_state(double *motor_0_pos, double *motor_2_pos)
{
    if (!active_backend || !active_backend->get_joint_state) {
        return 0;
    }

    return active_backend->get_joint_state(motor_0_pos, motor_2_pos);
}