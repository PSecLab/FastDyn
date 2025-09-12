#include <gz/transport/Node.hh>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/vector3d.pb.h>
#include <gz/msgs/quaternion.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/model.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <gz/msgs/navsat.pb.h>
#include <cmath>
#include <cstdio>
#include "gazebo_wrapper.h"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cctype>
#include <cstdlib>

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

// Very basic JSON parser for your specific SITL JSON format
bool parse_sitl_state(const std::string &json_str, sitl_state_data_t &state) {
    state.timestamp_s = 0.0;
    for (int i=0;i<6;i++) state.rng[i] = 0.0f;
    state.airspeed = 0.0f;
    state.no_time_sync = false;
    state.attitude = {0,0,0};
    state.wind_vane_apparent = {0,0};

    // Find timestamp
    auto pos_ts = json_str.find("\"timestamp\":");
    if (pos_ts != std::string::npos) {
        size_t start = json_str.find_first_of("0123456789.-", pos_ts);
        size_t end = json_str.find_first_of(",}", start);
        state.timestamp_s = std::stod(json_str.substr(start, end-start));
    }

    // Helper lambda to extract array by key
    auto extract_array = [&](const std::string &key) -> std::vector<double> {
        auto pos = json_str.find(key);
        if (pos == std::string::npos) return {};
        size_t start = json_str.find('[', pos);
        size_t end = json_str.find(']', start);
        if (start == std::string::npos || end == std::string::npos) return {};
        return parse_array(json_str.substr(start+1, end-start-1));
    };

    // IMU
    auto gyro = extract_array("\"gyro\"");
    if (gyro.size() == 3) {
        state.imu.gyro.x = static_cast<float>(gyro[0]);
        state.imu.gyro.y = static_cast<float>(gyro[1]);
        state.imu.gyro.z = static_cast<float>(gyro[2]);
    }
    auto accel = extract_array("\"accel_body\"");
    if (accel.size() == 3) {
        state.imu.accel_body.x = static_cast<float>(accel[0]);
        state.imu.accel_body.y = static_cast<float>(accel[1]);
        state.imu.accel_body.z = static_cast<float>(accel[2]);
    }

    // Position
    auto pos = extract_array("\"position\"");
    if (pos.size() == 3) {
        state.position.x = pos[0];
        state.position.y = pos[1];
        state.position.z = pos[2];
    }

    // Quaternion
    auto quat = extract_array("\"quaternion\"");
    if (quat.size() == 4) {
        state.quaternion.w = quat[0];
        state.quaternion.x = quat[1];
        state.quaternion.y = quat[2];
        state.quaternion.z = quat[3];
    }

    // Velocity
    auto vel = extract_array("\"velocity\"");
    if (vel.size() == 3) {
        state.velocity.x = static_cast<float>(vel[0]);
        state.velocity.y = static_cast<float>(vel[1]);
        state.velocity.z = static_cast<float>(vel[2]);
    }

    return true;
}

extern "C" {

// Helper: create quaternion from yaw (Z axis rotation)
void quaternion_from_yaw(double yaw, gz::msgs::Quaternion *quat)
{
    double half_yaw = yaw * 0.5;
    quat->set_x(0);
    quat->set_y(0);
    quat->set_z(std::sin(half_yaw));
    quat->set_w(std::cos(half_yaw));
}

// Set pose service call, returns 0 on success, -1 on failure
int set_pose(double yaw_deg)
{
    gz::transport::Node node;

    gz::msgs::Pose pose_msg;
    pose_msg.set_name("gs_drone");

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
        return -1;

    return 0;
}

// int get_joint_state(double *motor_0_pos, double *motor_2_pos) {
//     gz::msgs::Model response;
//     if (!request_service("/get_joint_state", response)) {
//         return 0;
//     }
//     *motor_0_pos = response.joint(0).axis1().position();
//     *motor_2_pos = response.joint(1).axis1().position();
//     return 1;
// }

int get_mag_reading(double *mag_x, double *mag_y, double *mag_z) {
    gz::msgs::Magnetometer response;
    if (!request_service("/get_mag_reading", response)) {
        return 0;
    }
    *mag_x = response.field_tesla().x();
    *mag_y = response.field_tesla().y();
    *mag_z = response.field_tesla().z();
    return 1;
}

int get_navsat_reading(gps_data_t *gps_data) {
    gz::msgs::NavSat response;
    if (!request_service("/get_navsat_reading", response)) {
        return 0;
    }

    // printf("NavSat types: %s\n", response.GetTypeName().c_str());

    gps_data_t data;
    data.lat = response.latitude_deg();
    data.lon = response.longitude_deg();
    data.alt = response.altitude();
    data.vel_n = response.velocity_north();
    data.vel_e = response.velocity_east();
    data.vel_d = response.velocity_up();
    data.sec = (uint64_t)response.header().stamp().sec();
    data.nsec = (uint32_t)response.header().stamp().nsec();

    *gps_data = data;

    return 1;
}

int advance_simulation(uint32_t steps, sitl_state_data_t *state_data) {
    printf("Entering advance_simulation with steps: %u\n", steps);

    gz::msgs::StringMsg response;
    if (!request_service("/step_simulation", response)) {
        return 0;
    }

    printf("Received response: %s\n", response.data().c_str());

    sitl_state_data_t data;
    if (!parse_sitl_state(response.data(), data)) {
        return 0;
    }

    *state_data = data;
    return 1;
}

} // extern "C"