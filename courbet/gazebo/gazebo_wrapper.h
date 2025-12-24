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
    float yaw_deg;
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

// IMU batch
// Contains 17 IMU readings
typedef struct {
    imu_t imu[17];
} imu_batch_t;

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
 * @brief Sets the rover's position as a demo
 *
 * This function sets the rover's position in the Gazebo simulation
 * to a fixed point with a specified yaw angle. Basically just making
 * it turn in place.
 *
 * @param yaw_deg Yaw angle in degrees
 * @return 1 on success, 0 on failure
 */
int set_pose(double yaw_deg);

/**
 * @brief Gets the current joint states of the rover's motors
 *
 * This function retrieves the current positions of the rover's motors
 * from the Gazebo simulation.
 *
 * @param motor_0_pos Pointer to store the position of motor 0
 * @param motor_2_pos Pointer to store the position of motor 2
 * @return 1 on success, 0 on failure
 */
int get_joint_state(double *motor_0_pos, double *motor_2_pos);

/**
 * @brief Gets the latest magnetometer reading
 *
 * This function retrieves the latest magnetometer reading from the
 * Gazebo simulation.
 *
 * @param mag_x Pointer to store the X component of the magnetic field
 * @param mag_y Pointer to store the Y component of the magnetic field
 * @param mag_z Pointer to store the Z component of the magnetic field
 * @return 1 on success, 0 on failure
 */
int get_mag_reading(double *mag_x, double *mag_y, double *mag_z);

/**
 * @brief Gets the latest GPS reading
 *
 * This function retrieves the latest GPS reading from the Gazebo
 * simulation.
 *
 * @param gps_data Pointer to a gps_data_t struct to store the GPS data
 * @return 1 on success, 0 on failure
 */
int get_navsat_reading(gps_data_t *gps_data);

/**
 * @brief Sets the PWM value for a specific servo channel
 *
 * This function sets the PWM value for a specified servo channel
 * in the Gazebo simulation.
 *
 * @param channel The servo channel (0-15)
 * @param pwm The PWM value to set (0-2000)
 * @return 1 on success, 0 on failure
 */
int set_servo_pwm(int channel, int pwm);

/**
 * @brief Advances the simulation by a specified number of steps
 *        and retrieves the current SITL state data.
 *
 * This function calls the /step_simulation service in Gazebo to
 * advance the simulation by the given number of steps. It then
 * retrieves the current SITL state data including IMU, position,
 * attitude, velocity, rangefinder readings, wind vane, and airspeed.
 *
 * @param run_until_time The simulation time to run until
 * @return 1 on success, 0 on failure
 */
int advance_simulation(double run_until_time);

/**
 * @brief Retrieves the latest SITL state data without advancing the simulation.
 *
 * This function provides access to the most recent SITL state data
 * that was obtained from the Gazebo simulation. It does not advance
 * the simulation but simply returns the last known state.
 *
 * @param state_data Pointer to a sitl_state_data_t struct to store the state data
 * @return 1 on success, 0 on failure
 */
int get_latest_sitl_state(sitl_state_data_t *state_data);

/**
 * @brief Gets a batch of IMU readings
 *
 * This function retrieves a batch of IMU readings from the Gazebo
 * simulation. After profiling, it was determined the IMU driver function is hit ~60
 * times per second, so we use a circular buffer to store the last 17 readings to reach
 * approximately 1000 Hz.
 *
 * @param imu_batch Pointer to an imu_batch_t struct to store the IMU data
 * @return 1 on success, 0 on failure
 */
int get_imu_batch(imu_batch_t *imu_batch);

#ifdef __cplusplus
}
#endif

#endif // GAZEBO_WRAPPER_H
