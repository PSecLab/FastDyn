#include "phy.h"
#include <stdio.h>
#include <string.h>
#include "physics_engines/gazebo/gazebo.h"

static phy_backend_t *active_backend = NULL;

static phy_backend_entry_t backends[] = {
    { "gazebo", &gazebo_backend},
    // Future engines can be added here, e.g.:
    // { "casadi", &casadi_backend},
};

int phy_select_backend(const char *name)
{
    if (!name) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        if (strcmp(name, backends[i].name) == 0) {
            active_backend = backends[i].backend;
            return 1;
        }
    }

    fprintf(stderr, "phy_select_backend: undefined backend '%s'\n", name);
    return 0;
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