/**
 * @brief Gazebo Virtual Instructions
 *
 * This file implements the necessary
 * interactions with Gazebo for the simhost-ing
 * ardupilot.
 *
 * @file FastDyn/virtuals/gazebo.c
 * @author Michael Rooney
 */
#include <math.h>

const uint32_t encoder_counts_per_rev = 3200;
const float wheel_radius = 0.069f;

void gz_service(unsigned int cpu_index, void *udata)
{
	static float yaw = 0.0;
    if (set_pose(yaw)) {
        printf("Pose set successfully\n");
		yaw += 30.0;
	} else {
        printf("Failed to set pose\n");
    }
}

void compass_block_read(unsigned int cpu_index, void *udata)
{
    double mag_x = 0.0;
    double mag_y = 0.0;
    double mag_z = 0.0;

    int success = get_mag_reading(&mag_x, &mag_y, &mag_z);
    if (!success)
    {
        printf("Failed to get magnetometer reading\n");
        return;
    }

    printf("Magnetometer Reading: X=%f, Y=%f, Z=%f\n", mag_x, mag_y, mag_z);

    // Copy state to frontend
    // ...
}

void send_mavlink_gps_data(unsigned int cpu_index, void *udata)
{
    gps_data_t gps_data;
    int success = get_navsat_reading(&gps_data);
    if (!success) {
        printf("Failed to get GPS data\n");
        return;
    }

    printf("GPS Data: Lat=%f, Lon=%f, Alt=%f, VelN=%f, VelE=%f, VelD=%f, Sec=%lu, Nsec=%u\n",
           gps_data.lat, gps_data.lon, gps_data.alt,
           gps_data.vel_n, gps_data.vel_e, gps_data.vel_d,
           gps_data.sec, gps_data.nsec);
}

// TODO: Implement a timer based approach to call this advance_simulation function
// at a fixed simulation time interval.

// For now just make a virtual that calls it once.
void gz_advance_simulation(unsigned int cpu_index, void *udata)
{
    sitl_state_data_t state_data;
    int success = advance_simulation(1, &state_data);
    if (!success) {
        printf("Failed to advance simulation\n");
        return;
    }

    printf("Simulation advanced. Timestamp: %f\n", state_data.timestamp_s);
    // Process state_data as needed
}