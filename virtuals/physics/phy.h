#ifndef PHY_H
#define PHY_H

#include <stdint.h>

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

/* Backend interface */
typedef struct phy_backend {

    const char *name;

    int  (*init)(void);
    void (*shutdown)(void);

    int (*get_imu_batch)(imu_batch_t *imu_batch);

    int (*get_mag_reading)(vector3d_t *mag);

    int (*get_navsat_reading)(gps_data_t *gps_data);

    int (*set_servo_pwm)(int channel, int pwm);

    int (*advance_simulation)(double run_until_time);

    int (*get_joint_state)(double *motor_0_pos, double *motor_2_pos);

} phy_backend_t;


/* Registration */
int phy_register_backend(phy_backend_t *backend);

/* Generic API */
int phy_init(void);
int phy_get_imu_batch(imu_batch_t *batch);
int phy_get_mag_reading(vector3d_t *mag);
int phy_get_navsat_reading(gps_data_t *gps_data);
int phy_set_servo_pwm(int channel, int pwm);
int phy_advance_simulation(double run_until_time);
int phy_get_joint_state(double *motor_0_pos, double *motor_2_pos);


#endif /* PHY_H */