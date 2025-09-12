#ifndef GAZEBO_WRAPPER_H
#define GAZEBO_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct gps_data_t {
    double lat;
    double lon;
    double alt;
    double vel_n;
    double vel_e;
    double vel_d;
    uint64_t sec;
    uint32_t nsec;
} gps_data_t;

// 3D float vector
typedef struct {
    float x;
    float y;
    float z;
} vector3f_t;

// 3D double vector
typedef struct {
    double x;
    double y;
    double z;
} vector3d_t;

// Quaternion
typedef struct {
    double w;
    double x;
    double y;
    double z;
} quaternion_t;

// IMU
typedef struct {
    vector3f_t gyro;
    vector3f_t accel_body;
} imu_t;

// Wind vane apparent
typedef struct {
    float direction;
    float speed;
} wind_vane_apparent_t;

// SITL state data
typedef struct {
    double timestamp_s;
    imu_t imu;
    vector3d_t position;
    vector3f_t attitude;
    quaternion_t quaternion;
    vector3f_t velocity;
    float rng[6];                     // array of 6 floats
    wind_vane_apparent_t wind_vane_apparent;
    float airspeed;
    bool no_time_sync;
} sitl_state_data_t;

/**
 * @brief Debug function to demonstrate control of vehicle in Gazebo
 *
 * @param name Name of the model to set the pose for
 * @param yaw_deg Yaw angle in degrees
 * @return int 0 on success, -1 on failure
 */
int set_pose(double yaw_deg);

// int get_joint_state(double *motor_0_pos, double *motor_2_pos);

int get_mag_reading(double *mag_x, double *mag_y, double *mag_z);

int get_navsat_reading(gps_data_t *gps_data);

int advance_simulation(uint32_t steps, sitl_state_data_t *state_data);

#ifdef __cplusplus
}
#endif

#endif // GAZEBO_WRAPPER_H