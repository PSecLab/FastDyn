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
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <memory>

/**
 * @brief Get a large set of different readings from multiple
 * sensors in Gazebo. and return them as a JSON string.
 *
 * @tparam Tuple of msg types to request
 * {
 *    heading: ....
 *    imu: {accel:..., gyro:..., mag:...}
 *    gps: {lat:..., lon:..., alt:...}
 *    baro: {pressure:..., altitude:...}
 *    rng: [rng0, rng1, ...]
 *    airspeed: ...
 *    pose: {position:..., orientation:...}
 *    velocity: {linear:..., angular:...}
 * }
 */

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

/**
 * @brief Advances the simulation by one step and gets the model state
 *        from Gazebo.
 *
 * This class exposes the /step_simulation service, which when called,
 * advances the Gazebo simulation by one step and retrieves the current
 * state of the specified model.
 *
 * @param[in] req An empty request message
 * @param[out] rep The response message containing the model state
 * @return true if the service call was successful, false otherwise
 *
 * This class also exposes a /set_servo service to set individual
 * servo PWM values. The plugin expects a string message in the format
 * "channel,pwm" where channel is the servo channel (0-15) and pwm
 * is the PWM value (e.g., 1000-2000).
 *
 * @param[in] req A string message with "channel,pwm"
 * @param[out] rep A boolean message indicating success or failure
 * @return true if the PWM was set successfully, false otherwise
 */

std::vector<uint16_t> pwm_values_;

class ServoService
{
public:
  ServoService(gz::transport::Node &node,
               const std::string &service_name,
               const std::string &target_ip,
               int target_port,
               int local_port)
    : target_ip_(target_ip), target_port_(target_port)
  {
    InitSocket(local_port);

    // Advertise the service
    node.Advertise(service_name, &ServoService::OnServiceRequest, this);
    node.Advertise("/set_servo", &ServoService::OnSetPwmRequest, this);

    // Initialize PWM values to 1500
    pwm_values_.resize(16, 1500);
    magic_ = 18458;
    frame_rate_ = 5;
    frame_count_ = 1;
  }

  ~ServoService()
  {
    if (sock_ >= 0)
      close(sock_);
  }

private:
  void InitSocket(int local_port)
  {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0)
      throw std::runtime_error("Failed to create UDP socket");

    // Bind to local port
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);

    if (bind(sock_, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
      perror("bind");
      throw std::runtime_error("Failed to bind to local port");
    }

    // Set up remote address
    memset(&remote_addr_, 0, sizeof(remote_addr_));
    remote_addr_.sin_family = AF_INET;
    remote_addr_.sin_port = htons(target_port_);
    if (inet_pton(AF_INET, target_ip_.c_str(), &remote_addr_.sin_addr) <= 0)
      throw std::runtime_error("Invalid target IP");

    // Optional recv timeout
    timeval tv{1, 0};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  std::vector<uint16_t> BuildServoPacket()
  {
    // Packet layout: magic (H), frame_rate (H), frame_count (I), 16*uint16 PWM
    std::vector<uint16_t> packet(19); // 2+2+4+16 bytes will be packed later
    packet[0] = magic_;
    packet[1] = frame_rate_;
    packet[2] = frame_count_; // We'll cast I later in packing
    for (size_t i = 0; i < 16; ++i)
      packet[3 + i] = pwm_values_[i];
    return packet;
  }

  void SendServos()
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Build packet
    uint8_t buffer[2 + 2 + 4 + 16*2]; // H+H+I+16H
    size_t offset = 0;

    // magic (uint16_t)
    uint16_t val16;
    val16 = magic_;
    memcpy(buffer + offset, &val16, sizeof(val16));
    offset += sizeof(val16);

    // frame_rate (uint16_t)
    val16 = frame_rate_;
    memcpy(buffer + offset, &val16, sizeof(val16));
    offset += sizeof(val16);

    // frame_count (uint32_t)
    uint32_t val32 = frame_count_;
    memcpy(buffer + offset, &val32, sizeof(val32));
    offset += sizeof(val32);

    // 16 PWM values (uint16_t)
    for (auto pwm : pwm_values_)
    {
      val16 = pwm;
      memcpy(buffer + offset, &val16, sizeof(val16));
      offset += sizeof(val16);
    }

    // Send packet
    if (sendto(sock_, buffer, sizeof(buffer), 0,
               (struct sockaddr *)&remote_addr_, sizeof(remote_addr_)) < 0)
    {
      perror("sendto");
    }
    frame_count_++;
  }

  std::string ReceiveResponse()
  {
      std::lock_guard<std::mutex> lock(mutex_);
      uint8_t buffer[2048];
      socklen_t addrlen = sizeof(remote_addr_);

      ssize_t n = recvfrom(sock_, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr *)&remote_addr_, &addrlen);

      if (n < 0)
      {
          perror("recvfrom");
          return "";  // No data received or timeout
      }
      std::cout << "Received " << n << " bytes from Gazebo: ";

      buffer[n] = '\0';  // Null-terminate
      std::cout << std::string(reinterpret_cast<char*>(buffer), n) << std::endl;
      return std::string(reinterpret_cast<char*>(buffer), n);
  }

  // Service callback: empty request, no response needed
  bool OnServiceRequest(const gz::msgs::Empty &,
                        gz::msgs::StringMsg &response)
  {
    SendServos();
    response.set_data(ReceiveResponse());
    return !response.data().empty();
  }

  // Service callback: set individual servo PWM values given (channel, pwm)
  bool OnSetPwmRequest(const gz::msgs::StringMsg &request,
                       gz::msgs::Boolean &response)
  {
    // Expecting request.data() to be "channel,pwm"
    auto comma_pos = request.data().find(',');
    if (comma_pos == std::string::npos)
    {
      response.set_data(false);
      return true;
    }

    // Parse channel and PWM values
    int channel = std::stoi(request.data().substr(0, comma_pos));
    int pwm = std::stoi(request.data().substr(comma_pos + 1));
    if (channel < 0 || channel >= 16 || pwm < 0 || pwm > 2000)
    {
      std::cerr << "Invalid channel or PWM value" << std::endl;
      std::cerr << "Received: " << request.data() << std::endl;
      response.set_data(false);
      return true;
    }

    // Set PWM value
    pwm_values_[channel] = pwm;
    response.set_data(true);
    return true;
  }

  int sock_{-1};
  sockaddr_in remote_addr_{};
  std::mutex mutex_;
  std::string target_ip_;
  int target_port_;

  uint16_t magic_;
  uint16_t frame_rate_;
  uint32_t frame_count_;
  // std::vector<uint16_t> pwm_values_;
};


int main(int argc, char **argv)
{
  gz::transport::Node node;

  std::string model_name = "r1_rover";

  std::string navsat_topic = "/world/runway/model/r1_rover/link/base_link/sensor/navsat_sensor/navsat";
  std::string mag_topic = "/world/runway/model/r1_rover/link/base_link/sensor/magnetometer_sensor/magnetometer";
  if (model_name == "gs_drone") {
    navsat_topic = "/world/runway/model/gs_drone/link/sensors/sensor/navsat_sensor/navsat";
    mag_topic = "/world/runway/model/gs_drone/link/sensors/sensor/magnetometer_sensor/magnetometer";
  }
  GenericSensorService<gz::msgs::NavSat> navSatService(
    node,
    navsat_topic,
    "/get_navsat_reading"
  );

  GenericSensorService<gz::msgs::Magnetometer> magService(
    node,
    mag_topic,
    "/get_mag_reading"
  );

  // skip this if not rover
  GenericSensorService<gz::msgs::Model> jointStateService(
    node,
    "/joint_states",
    "/get_joint_state"
  );

  ServoService servoService(
    node,
    "/step_simulation",
    "127.0.0.1",
    9002,
    5200
  );

  std::cout << "ArduPilot Services running...\n";

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}