#include <gz/transport/Node.hh>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/vector3d.pb.h>
#include <gz/msgs/quaternion.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/model.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <gz/msgs/altimeter.pb.h>
#include <gz/msgs/uint32.pb.h>
#include <gz/msgs/navsat.pb.h>
#include <gz/msgs/uint32.pb.h>
#include <cmath>
#include <cstdio>
#include "gazebo.h"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <charconv>

template <typename ResponseT>
bool request_service(const std::string &service_name, ResponseT &response,
                     unsigned int timeout_ms = 5000)
{
    gz::transport::Node node;
    gz::msgs::Empty request;

    bool result;
    bool executed = node.Request(service_name, request, timeout_ms, response, result);

    return executed && result;
}

bool request_run_until_time(double run_until_time_s) {
    gz::transport::Node node;
    gz::msgs::Time request;
    request.set_sec(static_cast<uint32_t>(std::floor(run_until_time_s)));
    request.set_nsec(static_cast<uint32_t>((run_until_time_s - std::floor(run_until_time_s)) * 1e9));

    gz::msgs::Boolean response;
    bool result;
    bool executed = node.Request("/set_run_until_time", request, 5000, response, result);

    return executed && result && response.data();
}

// Helper: parse a JSON array of numbers into a vector of doubles
std::vector<double> parse_array(const std::string &s) {
    std::vector<double> result;
    std::string num;
    bool in_number = false;
    for (char c : s) {
        if ((c == '-') || (c == '+') || (isdigit(c)) || (c == '.') || (c == 'e') || (c == 'E')) {
            num += c;
            in_number = true;
        } else if (in_number) {
            result.push_back(std::stod(num));
            num.clear();
            in_number = false;
        }
    }
    if (!num.empty()) result.push_back(std::stod(num));
    return result;
}

extern "C" {

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
static int set_pose(double yaw_deg);

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
static int get_joint_state(double *motor_0_pos, double *motor_2_pos);

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
static int get_mag_reading(vector3d_t *mag);

/**
 * @brief Gets the latest GPS reading
 *
 * This function retrieves the latest GPS reading from the Gazebo
 * simulation.
 *
 * @param gps_data Pointer to a gps_data_t struct to store the GPS data
 * @return 1 on success, 0 on failure
 */
static int get_navsat_reading(gps_data_t *gps_data);

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
static int set_servo_pwm(int channel, int pwm);

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
static int advance_simulation(double run_until_time);

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
static int get_imu_batch(imu_batch_t *imu_batch);

/**
 * @brief Get an altimeter reading
 *
 * This function retrieves the latest altimeter reading from the Gazebo
 * simulation.
 *
 * @param altitude Pointer to store the altitude reading
 * @return 1 on success, 0 on failure
 */
static int get_altimeter_reading(double *altitude);

/**
 * @brief Gets the laser scan data for a specific number of samples and returns
 * an a populated array of rplidar_sample_t structs.
 *
 * This function takes in a number of samples to retrieve from the laser scan data. It calls the
 * get_laser_scan service to get the latest laser scan data.
 */
static int get_lidar_samples(rplidar_sample_t *samples, size_t num_samples);

/**
 * @brief Sets the program counter for hardfault simulation
 *
 * This function sets the program counter (PC) value to simulate
 * a hardfault occurring at that address in the Gazebo simulation.
 *
 * @param pc The program counter address to set
 * @return 1 on success, 0 on failure
 */
static int set_hardfault_pc(uint32_t pc);

// Helper: create quaternion from yaw (Z axis rotation)
static void quaternion_from_yaw(double yaw, gz::msgs::Quaternion *quat)
{
    double half_yaw = yaw * 0.5;
    quat->set_x(0);
    quat->set_y(0);
    quat->set_z(std::sin(half_yaw));
    quat->set_w(std::cos(half_yaw));
}

// Set pose service call, returns 0 on success, -1 on failure
static int set_pose(double yaw_deg)
{
    gz::transport::Node node;

    gz::msgs::Pose pose_msg;
    pose_msg.set_name("r1_rover");

    // Position
    auto *pos = pose_msg.mutable_position();
    pos->set_x(0.0);
    pos->set_y(0.0);
    pos->set_z(0.1);

    // Orientation quaternion
    auto *quat = pose_msg.mutable_orientation();
    double yaw_rad = yaw_deg * M_PI / 180.0;
    quaternion_from_yaw(yaw_rad, quat);

    // Boolean response
    gz::msgs::Boolean rep;
    bool result;
    bool executed = node.Request("/world/runway/set_pose", pose_msg, 5000, rep, result);

    if (!executed || !result)
        return 0;

    return 1;
}

static int get_joint_state(double *motor_0_pos, double *motor_2_pos) {
    gz::msgs::Model response;
    if (!request_service("/get_joint_state", response)) {
        return 0;
    }
    *motor_0_pos = response.joint(0).axis1().position();
    *motor_2_pos = response.joint(1).axis1().position();
    return 1;
}

static int get_mag_reading(vector3d_t *mag) {
    gz::msgs::Magnetometer response;
    if (!request_service("/get_corrected_mag_reading", response)) {         // replace with /get_corrected_mag_reading not to use wrong gazebo data
        return 0;
    }
    mag->x = response.field_tesla().x();
    mag->y = response.field_tesla().y();
    mag->z = response.field_tesla().z();
    return 1;
}

static int get_navsat_reading(gps_data_t *gps_data) {
    gz::msgs::NavSat response;
    if (!request_service("/get_navsat_reading", response)) {
        return 0;
    }

    // gz::msgs::Float yaw_rads;
    // if (!request_service("/get_yaw_reading", yaw_rads)) {
    //     return 0;
    // }

    // convert radians to degrees
    // TODO: fully remove but for now keep yaw as 0.0f
    float yaw_deg = 0.0f;

    gps_data_t data;
    data.lat = response.latitude_deg();
    data.lon = response.longitude_deg();
    data.alt = response.altitude();
    data.vel_n = response.velocity_north();
    data.vel_e = response.velocity_east();
    data.vel_d = -1 * response.velocity_up();
    data.sec = (uint64_t)response.header().stamp().sec();
    data.nsec = (uint32_t)response.header().stamp().nsec();
    data.yaw_deg = yaw_deg;

    *gps_data = data;

    return 1;
}

static int advance_simulation(double run_until_time) {
    if (!request_run_until_time(run_until_time)) {
        return 0;
    }
    return 1;
}

static int set_servo_pwm(int channel, int pwm) {
    if (channel < 0 || channel >= 16 || pwm < 0 || pwm > 2000) {
        return 0;
    }

    gz::transport::Node node;
    gz::msgs::StringMsg request;
    request.set_data(std::to_string(channel) + "," + std::to_string(pwm));

    gz::msgs::Boolean response;
    bool result;
    bool executed = node.Request("/set_servo", request, 5000, response, result);

    if (!executed || !result || !response.data()) {
        return 0;
    }

    return 1;
}

static int get_imu_batch(imu_batch_t *imu_batch) {
    // printf("Getting IMU batch\n");
    gz::msgs::StringMsg response;
    bool success = request_service("/get_imu_batch", response);
    if (!success) {
        return 0;
    }

    // data is CSV: first line is count, then each line is accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z
    std::istringstream ss(response.data());
    std::string line;
    // Read count
    if (!std::getline(ss, line)) {
        return 0;
    }
    int count = std::stoi(line);
    if (count <= 0 || count > 100) { // arbitrary max limit
        return 0;
    }
    // if (count != 17) {
    //     return 0;
    // }
    // imu_batch->count = count; non existing field
    for (int i = 0; i < count; i++) {
        if (!std::getline(ss, line)) {
            return 0;
        }
        std::istringstream line_ss(line);
        std::string value;
        // accel_x
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [accel_x, ec] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].accel_body.x);
        if (ec != std::errc()) return 0;
        // accel_y
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [accel_y, ec2] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].accel_body.y);
        if (ec2 != std::errc()) return 0;
        // accel_z
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [accel_z, ec3] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].accel_body.z);
        if (ec3 != std::errc()) return 0;
        // gyro_x
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [gyro_x, ec4] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].gyro.x);
        if (ec4 != std::errc()) return 0;
        // gyro_y
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [gyro_y, ec5] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].gyro.y);
        if (ec5 != std::errc()) return 0;
        // gyro_z
        if (!std::getline(line_ss, value, ',')) return 0;
        auto [gyro_z, ec6] = std::from_chars(value.data(), value.data() + value.size(), imu_batch->imu[i].gyro.z);
        if (ec6 != std::errc()) return 0;
    }

    // debug print out the first

    return 1;

}

static int set_hardfault_pc(uint32_t pc) {
    gz::transport::Node node;
    gz::msgs::UInt32 request;
    request.set_data(pc);

    gz::msgs::Empty response;
    bool result;
    bool executed = node.Request("/set_hardfault_pc", request, 5000, response, result);
    return 1;
}

static int get_altimeter_reading(double *altitude) {
    gz::msgs::Altimeter response;
    if (!request_service("/get_altimeter_reading", response)) {
        return 0;
    }
    *altitude = response.vertical_position();
    return 1;
}

// angle_deg: any real angle, normalized to [0, 360)
// distance_m: meters
// quality: 0..63
// start_flag: 1 for first sample of a new revolution, else 0
static rplidar_sample_t make_rplidar_sample(float angle_deg,
                                            float distance_m,
                                            uint8_t quality,
                                            uint8_t start_flag)
{
    rplidar_sample_t s;

    // normalize angle to [0, 360)
    angle_deg = fmodf(angle_deg, 360.0f);
    if (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }

    // clamp quality
    if (quality > 63) {
        quality = 63;
    }

    // clamp distance
    if (distance_m < 0.0f) {
        distance_m = 0.0f;
    }

    // angle in Q6: degrees * 64
    uint16_t angle_q6 = (uint16_t)lroundf(angle_deg * 64.0f);

    // distance in Q2 over millimeters: mm * 4
    float distance_mm = distance_m * 1000.0f;
    uint16_t dist_q2 = (uint16_t)lroundf(distance_mm * 4.0f);

    // byte 0:
    // bit 0 = S
    // bit 1 = !S
    // bits 2..7 = quality
    uint8_t S = start_flag ? 1u : 0u;
    uint8_t notS = S ? 0u : 1u;
    s.sync_quality = (uint8_t)((quality << 2) | (notS << 1) | S);

    // byte 1:
    // bit 0 = check bit, should be 1
    // bits 1..7 = angle_q6 low 7 bits
    s.angle_lsb = (uint8_t)(((angle_q6 & 0x7F) << 1) | 0x01);

    // byte 2:
    // high 8 bits of 15-bit angle_q6
    s.angle_msb = (uint8_t)((angle_q6 >> 7) & 0xFF);

    // bytes 3-4:
    // little-endian distance_q2
    s.dist_lsb = (uint8_t)(dist_q2 & 0xFF);
    s.dist_msb = (uint8_t)((dist_q2 >> 8) & 0xFF);

    return s;
}

static inline float index_to_angle(int index)
{
    const float angle_min = -M_PI;
    const float step = 0.015747318295739349f;
    float raw_angle = (angle_min + index * step);

    return raw_angle;
}

static int get_lidar_samples(rplidar_sample_t *samples, size_t num_samples) {
    gz::msgs::UInt32 request;
    request.set_data(static_cast<int>(num_samples));

    gz::msgs::StringMsg response;
    gz::transport::Node node;
    bool result;
    bool executed = node.Request("/get_laser_scan", request, 5000, response, result);
    if (!executed || !result) {
        return 0;
    }

    std::string resp_str = response.data();
    if (resp_str == "None") {
        return 0;
    }
    // response is a string of comma-separated values: "index:range1,range2,..."
    std::string data = response.data();
    size_t colon_pos = data.find(':');
    if (colon_pos == std::string::npos) {
        return 0;
    }
    int start_index = std::stoi(data.substr(0, colon_pos)); // not used for now but could be useful for debugging
    double angle_increment = 0.015747318295739349; // 2 * pi / 400
    std::string ranges_str = data.substr(colon_pos + 1);
    std::istringstream ss(ranges_str);
    std::string range_str;
    size_t count = 0;
    while (std::getline(ss, range_str, ',') && count < num_samples) {
        float range = 16.0f;
        if (range_str != "inf") {
            range = std::stof(range_str);
        }
        float angle_deg = index_to_angle((start_index + count) % 400) * 180.0f / M_PI; // convert to degrees
        int idx = (start_index + count) % 400;
        uint8_t start_flag = (idx == 0) ? 1 : 0;
        samples[count] = make_rplidar_sample(angle_deg, range, 60, start_flag);
        count++;
    }
    return 1;
}

static int gz_init(void)
{
    // Set initial pose as a demo
    printf("Gazebo physics engine initialized\n");
    return 1;
}

phy_backend_t gazebo_backend = {
    .name = "gazebo",
    .init = gz_init,
    .shutdown = NULL,
    .get_imu_batch = get_imu_batch,
    .get_mag_reading = get_mag_reading,
    .get_navsat_reading = get_navsat_reading,
    .set_servo_pwm = set_servo_pwm,
    .advance_simulation = advance_simulation,
    .get_joint_state = get_joint_state,
    .get_altimeter_reading = get_altimeter_reading,
    .get_lidar_samples = get_lidar_samples
};

} // extern "C"
