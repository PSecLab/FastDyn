/**
 * @file services.cpp
 * @brief Implementation of services for communicating with Gazebo
 */
#include <gz/transport.hh>
#include <gz/msgs/model.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <gz/msgs/navsat.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/pose_v.pb.h>
#include <iostream>
#include <mutex>
#include <thread>

/**
 * @brief Generic service template for any sensor message type
 *
 * This template class can be instantiated for any Gazebo message type.
 * It subscribes to a specified topic and provides the latest message
 * upon a service request.
 *
 * @tparam MsgType The type of the Gazebo message
 */
template <typename MsgType>
class GenericSensorService
{
public:
  GenericSensorService(gz::transport::Node &node,
                       const std::string &topic_name,
                       const std::string &service_name)
  {
    node.Subscribe(topic_name, &GenericSensorService::OnSensorMsg, this);
    node.Advertise(service_name, &GenericSensorService::OnServiceRequest, this);
  }

private:
  void OnSensorMsg(const MsgType &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_msg_ = msg;
  }

  bool OnServiceRequest(const gz::msgs::Empty &,
                        MsgType &rep)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    rep = latest_msg_;
    return true;
  }

  MsgType latest_msg_;
  std::mutex mutex_;
};


int main(int argc, char **argv)
{
  gz::transport::Node node;

  GenericSensorService<gz::msgs::NavSat> navSatService(
    node,
    "/world/runway/model/gs_drone/link/sensors/sensor/navsat_sensor/navsat",
    "/get_navsat_reading"
  );

  std::cout << "ArduRover Services running...\n";

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}