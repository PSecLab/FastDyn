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
#include <boost/circular_buffer.hpp>
#include <iostream>
#include <mutex>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

const static bool DEBUG = false;
static bool READY = false;

using json = nlohmann::json;

typedef struct
{
  double accel_x;
  double accel_y;
  double accel_z;
  double gyro_x;
  double gyro_y;
  double gyro_z;
} imu_data_t;

/**
 * @brief Parse IMU data from JSON string
 *
 * @param input JSON string containing IMU data
 * @return Parsed imu_data_t structure
 */
imu_data_t parse_imu_json(const std::string &input)
{
    imu_data_t imu{};

    // Parse JSON
    json j = json::parse(input);

    auto &gyro  = j["imu"]["gyro"];
    auto &accel = j["imu"]["accel_body"];

    imu.gyro_x  = gyro[0].get<double>();
    imu.gyro_y  = gyro[1].get<double>();
    imu.gyro_z  = gyro[2].get<double>();

    imu.accel_x = accel[0].get<double>();
    imu.accel_y = accel[1].get<double>();
    imu.accel_z = accel[2].get<double>();

    // add noise to gyros (+)
    // noise from 0 to 0.001 rad/s
    imu.gyro_x += static_cast<float>( (static_cast<double>(rand()) / RAND_MAX) * 0.001 );
    imu.gyro_y += static_cast<float>( (static_cast<double>(rand()) / RAND_MAX) * 0.001 );
    imu.gyro_z += static_cast<float>( (static_cast<double>(rand()) / RAND_MAX) * 0.001 );

    // imu.gyro_x  = std::to_string(gyro[0].get<double>());
    // imu.gyro_y  = std::to_string(gyro[1].get<double>());
    // imu.gyro_z  = std::to_string(gyro[2].get<double>());

    // imu.accel_x = std::to_string(accel[0].get<double>());
    // imu.accel_y = std::to_string(accel[1].get<double>());
    // imu.accel_z = std::to_string(accel[2].get<double>());

    return imu;
}

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
    if (topic_name == "NONE")
      return;
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
 * This class exposes the /set_run_until_time service, which when called,
 * advances the Gazebo simulation until the specified time. The actual advancing
 * is done in a separate thread to avoid blocking the service call.
 *
 * @param[in] req An gz::msgs::Time request message
 * @param[out] rep Boolean response message
 * @return true if the service call was successful, false otherwise
 *
 * @brief Service handler for /get_latest_sim_state
 *
 * This service retrieves the latest simulation state we have received
 * from Gazebo.
 *
 * @param[in] req An empty request message
 * @param[out] rep A string message containing the latest simulation state in JSON format
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
               const std::string &model_name,
               int target_port,
               int local_port)
    : target_ip_(target_ip), target_port_(target_port)
  {
    InitSocket(local_port);

    // Advertise the service
    node.Advertise(service_name, &ServoService::OnServiceRequest, this);
    node.Advertise("/set_servo", &ServoService::OnSetPwmRequest, this);
    node.Advertise("/get_latest_sim_state", &ServoService::OnGetLatestSimStateRequest, this);
    node.Advertise("/get_imu_batch", &ServoService::OnGetIMUBatch, this);

    // Start the simulation advance thread
    std::thread(&ServoService::advanceSimThread, this).detach();

    // Initialize PWM values to 1500
    pwm_values_.resize(16, 1500);
    // Per vehicle changes to expected format
    if (model_name == "vtail_plane") {
      pwm_values_[2] = 1000;
      for (size_t i = 4; i < 16; i++)
      {
        pwm_values_[i] = 0;
      }
    }
    magic_ = 18458;
    frame_rate_ = 5;
    frame_count_ = 1;

    // Send initial packet to establish communication
    // sendInitialPacket();

  }

  ~ServoService()
  {
    if (sock_ >= 0)
      close(sock_);
  }

  void sendInitialPacket()
  {
    SendServos();
    std::string response = ReceiveResponse();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_response_ = response;
    }
    auto pos = response.find("\"timestamp\":");
    if (pos != std::string::npos)
    {
      size_t start = response.find_first_of("0123456789.-", pos);
      size_t end = response.find_first_of(",", start);
      if (start != std::string::npos && end != std::string::npos)
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_time_s_ = std::stod(response.substr(start, end - start));
      }
    }
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
      // if (pwm != 0 && pwm != 1500) {
      //     std::cout << "Sending PWM: " << pwm << std::endl;
      // }
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
      if (DEBUG)
      {
          std::cout << "Received " << n << " bytes from Gazebo: ";
      }

      buffer[n] = '\0';  // Null-terminate

      if (DEBUG)
      {
          std::cout << std::string(reinterpret_cast<char*>(buffer), n) << std::endl;
      }
      return std::string(reinterpret_cast<char*>(buffer), n);
  }


  bool OnServiceRequest(const gz::msgs::Time &request,
                        gz::msgs::Boolean &response)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // get timestamp from request
      double req_time_s = request.sec() + request.nsec() * 1e-9;
      run_until_time_s_ = req_time_s;
    }

    // std::cout << "Advancing simulation to time: " << run_until_time_s_ << " s\n";

    while (run_until_time_s_ - latest_time_s_ > 0.1)
    {
      // wait for the sim to catch up within 0.1 s
      // std::cout << "Waiting... latest_time_s_: " << latest_time_s_ << " s\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // return true for now
    response.set_data(true);
    return true;
  }

  void advanceSimThread()
  {
    while (!READY) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Starting simulation advance thread...\n";
    // sleep 5 seconds to allow Gazebo to start
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (last_response_.empty()) {
        sendInitialPacket();
    }

    bool toggle = false;
    while (true)
    {
      {
        // std::lock_guard<std::mutex> lock(mutex_);
        if (latest_time_s_ < run_until_time_s_)
        {
          if (!toggle) {
              toggle = true;
              std::cout << "Advancing simulation to time: " << run_until_time_s_ << " s\n";
              std::cout << "Current latest_time_s_: " << latest_time_s_ << " s\n";
          }
          // std::cout << "Advancing simulation to time: " << run_until_time_s_ << " s\n";
          SendServos();
          std::string response = ReceiveResponse();
          if (response.empty())
            continue;
          {
            std::lock_guard<std::mutex> lock(mutex_);
            last_response_ = response;
          }
          auto pos = response.find("\"timestamp\":");
          if (pos != std::string::npos)
          {
            size_t start = response.find_first_of("0123456789.-", pos);
            size_t end = response.find_first_of(",", start);
            if (start != std::string::npos && end != std::string::npos)
            {
              std::lock_guard<std::mutex> lock(mutex_);
              latest_time_s_ = std::stod(response.substr(start, end - start));
            }

            imu_data_t imu = parse_imu_json(response);
            {
              std::lock_guard<std::mutex> lock(mutex_);
              imu_buffer_.push_back(imu);
            }
          }
          else
          {
            std::cout << "No timestamp found in response\n";
          }
        }
        else
        {
          // Sleep briefly to avoid busy wait
          if (toggle) {
              toggle = false;
              std::cout << "Simulation caught up to requested time: " << run_until_time_s_ << " s\n";
          }
          // else {
          //     std::cout << "Latest time: " << latest_time_s_ << " s, Run until time: " << run_until_time_s_ << " s\n";
          // }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    }
  }

  // gz service   -s /world/runway/wind_info \
  //     --reqtype gz.msgs.Empty  \
  //     --reptype gz.msgs.Wind  \
  //     --timeout 3000 --req ''

  /**
   * gz service   -s /get_latest_sim_state \\
   *    --reqtype gz.msgs.Empty  \\
   *    --reptype gz.msgs.StringMsg  \\
   *    --timeout 3000 --req ''
   */
  bool OnGetLatestSimStateRequest(const gz::msgs::Empty &,
                                  gz::msgs::StringMsg &rep)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    rep.set_data(last_response_);
    return true;
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

  // Service callback: get imu batch data
  // Data should be returned CSV string format:
  // timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z\n...
  // Where the first line contains the number of samples (17)
  bool OnGetIMUBatch(const gz::msgs::Empty &,
                     gz::msgs::StringMsg &rep)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // std::string csv_data;
    // csv_data += std::to_string(imu_buffer_.size()) + "\n";
    // for (const auto &imu : imu_buffer_)
    // {
    //   csv_data += imu.accel_x + "," + imu.accel_y + "," + imu.accel_z + ",";
    //   csv_data += imu.gyro_x + "," + imu.gyro_y + "," + imu.gyro_z + "\n";
    // }
    // rep.set_data(csv_data);

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(9);

    oss << imu_buffer_.size() << "\n";
    for (const auto &imu : imu_buffer_) {
      oss << imu.accel_x << ","
          << imu.accel_y << ","
          << imu.accel_z << ","
          << imu.gyro_x  << ","
          << imu.gyro_y  << ","
          << imu.gyro_z  << "\n";
    }

    rep.set_data(oss.str());

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

  double latest_time_s_{0.0};
  double run_until_time_s_{0.0};
  std::string last_response_;
  // std::vector<uint16_t> pwm_values_;

  // boost circular buffer for imu data
  boost::circular_buffer<imu_data_t> imu_buffer_{17};
};

/**
 * @brief Service to get yaw from model pose
 *
 * This service subscribes to the model pose topic and provides
 * the yaw angle upon request.
 */
template <typename MsgType = gz::msgs::Pose_V>
class YawService
{
public:
  YawService(gz::transport::Node &node,
              const std::string &topic_name,
              const std::string &service_name)
  {
    node.Subscribe(topic_name, &YawService::OnPoseMsg, this);
    node.Advertise(service_name, &YawService::OnServiceRequest, this);
  }

private:
  void OnPoseMsg(const MsgType &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    received_ = true;
    latest_pose_ = msg;
  }

  float QuaternionToYaw(const gz::msgs::Quaternion &q)
  {
    // Yaw (Z-axis rotation)
    double siny_cosp = 2.0 * (q.w() * q.z() + q.x() * q.y());
    double cosy_cosp = 1.0 - 2.0 * (q.y() * q.y() + q.z() * q.z());
    double yaw = std::atan2(siny_cosp, cosy_cosp);
    // subtract 90 degrees to convert from Gazebo NED to ArduPilot ENU
    yaw -= M_PI / 2.0;
    if (yaw < -M_PI)
      yaw += 2.0 * M_PI;
    return static_cast<float>(yaw);
  }

  bool OnServiceRequest(const gz::msgs::Empty &,
                        gz::msgs::Float &rep)
  {
    if (!received_)
      rep.set_data(0.0f);
      return true;
    std::lock_guard<std::mutex> lock(mutex_);
    float yaw = QuaternionToYaw(latest_pose_.pose(0).orientation());
    rep.set_data(yaw);
    return true;
  }

  MsgType latest_pose_;
  std::mutex mutex_;
  bool received_{false};
};


int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <model_name>\n";
    std::cerr << "Example: " << argv[0] << " [r1_rover, gs_drone, vtail_plane]\n";
    return 1;
  }

  gz::transport::Node node;

  std::string model_name = argv[1];

  std::string navsat_topic = "/world/runway/model/r1_rover/link/base_link/sensor/navsat_sensor/navsat";
  std::string mag_topic = "/world/runway/model/r1_rover/link/base_link/sensor/magnetometer_sensor/magnetometer";
  std::string joint_states_topic = "/joint_states";
  if (model_name == "gs_drone") {
    navsat_topic = "/world/runway/model/gs_drone/link/sensors/sensor/navsat_sensor/navsat";
    mag_topic = "/world/runway/model/gs_drone/link/sensors/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  } else if (model_name == "vtail_plane") {
    // navsat_topic = "/world/runway/model/skywalker_x8/link/imu_link/sensor/navsat_sensor/navsat";
    // mag_topic = "/world/runway/model/skywalker_x8/link/imu_link/sensor/magnetometer_sensor/magnetometer";
    navsat_topic = "/world/runway/model/mini_talon_vtail/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic = "/world/runway/model/mini_talon_vtail/link/base_link/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
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
    joint_states_topic,
    "/get_joint_state"
  );

  ServoService servoService(
    node,
    "/set_run_until_time",
    "127.0.0.1",
    model_name,
    9002,
    5200
  );

  // YawService yawService(
  //   node,
  //   "/model/" + model_name + "/pose",
  //   "/get_yaw_reading"
  // );

  std::cout << "ArduPilot Services running...\n";

  READY = true;

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
