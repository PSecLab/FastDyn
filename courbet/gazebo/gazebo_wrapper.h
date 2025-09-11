#ifndef GAZEBO_WRAPPER_H
#define GAZEBO_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double lat;
    double lon;
    double alt;
    double vel_n;
    double vel_e;
    double vel_d;
    uint64_t sec;
    uint32_t nsec;
} gps_data_t;

/**
 * @brief Debug function to demonstrate control of vehicle in Gazebo
 *
 * @param name Name of the model to set the pose for
 * @param yaw_deg Yaw angle in degrees
 * @return int 0 on success, -1 on failure
 */
int set_pose(const std::string &name, double yaw_deg);

// int get_joint_state(double *motor_0_pos, double *motor_2_pos);

int get_mag_reading(double *mag_x, double *mag_y, double *mag_z);

int get_navsat_reading(gps_data_t *gps_data);

#ifdef __cplusplus
}
#endif

#endif // GAZEBO_WRAPPER_H