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
int set_pose(const std::string &name, double yaw_deg)
{
    gz::transport::Node node;

    gz::msgs::Pose pose_msg;
    pose_msg.set_name(name);

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

} // extern "C"