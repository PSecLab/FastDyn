#include <gz/transport/Node.hh>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/vector3d.pb.h>
#include <gz/msgs/quaternion.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/model.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <gz/msgs/uint32.pb.h>
#include <gz/msgs/navsat.pb.h>
#include <gz/msgs/uint32.pb.h>
#include <cmath>
#include <cstdio>
#include "gazebo_wrapper.h"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <charconv>


// static std::mutex sitl_state_mutex;
// static sitl_state_data_t latest_sitl_state;
// static double latest_time_s = 0.0;

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

int get_joint_state(double *motor_0_pos, double *motor_2_pos) {
    gz::msgs::Model response;
    if (!request_service("/get_joint_state", response)) {
        return 0;
    }
    *motor_0_pos = response.joint(0).axis1().position();
    *motor_2_pos = response.joint(1).axis1().position();
    return 1;
}

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
    data.vel_d = response.velocity_up();
    data.sec = (uint64_t)response.header().stamp().sec();
    data.nsec = (uint32_t)response.header().stamp().nsec();
    data.yaw_deg = yaw_deg;

    *gps_data = data;

    return 1;
}

int advance_simulation(double run_until_time) {
    // printf("Advancing simulation to time: %.6f s\n", run_until_time);
    if (!request_run_until_time(run_until_time)) {
        return 0;
    }
    return 1;
}

int get_latest_sitl_state(sitl_state_data_t *state_data) {
    gz::msgs::StringMsg response;
    bool success = request_service("/get_latest_sim_state", response);
    if (!success) {
        return 0;
    }
    if (!parse_sitl_state(response.data(), *state_data)) {
        return 0;
    }
    return 1;
}

int set_servo_pwm(int channel, int pwm) {
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

int get_imu_batch(imu_batch_t *imu_batch) {
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

    return 1;

}

int set_hardfault_pc(uint32_t pc) {
    gz::transport::Node node;
    gz::msgs::UInt32 request;
    request.set_data(pc);

    gz::msgs::Empty response;
    bool result;
    bool executed = node.Request("/set_hardfault_pc", request, 5000, response, result);
    return 1;
}

} // extern "C"