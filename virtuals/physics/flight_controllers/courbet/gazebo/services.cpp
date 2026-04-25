/**
 * @file services.cpp
 * @brief Implementation of services for communicating with Gazebo
 */
#include <gz/transport.hh>
#include <gz/msgs/model.pb.h>
#include <gz/msgs/magnetometer.pb.h>
#include <gz/msgs/altimeter.pb.h>
#include <gz/msgs/imu.pb.h>
#include <gz/msgs/clock.pb.h>
#include <gz/msgs/navsat.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/any.pb.h>
#include <gz/msgs/laserscan.pb.h>
#include <gz/msgs/uint32.pb.h>
#include <gz/msgs/entity_factory.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/math/Quaternion.hh>
#include <boost/circular_buffer.hpp>
#include <iostream>
#include <cmath>
#include <cctype>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <functional>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <gz/math/Vector3.hh>

const static bool DEBUG = false;
static bool READY = false;

std::atomic<double> latest_time_s_{0.0};

double read_latest_sim_time()
{
  return latest_time_s_.load(std::memory_order_relaxed);
}

void update_latest_sim_time(double new_time)
{
  latest_time_s_.store(new_time, std::memory_order_relaxed);
}

using json = nlohmann::json;

struct Vec3NoiseModel {
  // Base noise levels
  gz::math::Vector3d sigma_white_base{0,0,0};
  gz::math::Vector3d sigma_rw_base{0,0,0};

  // User knobs
  double white_scale = 1.0;
  double drift_scale = 1.0;

  // Deterministic controls
  uint64_t seed = 0;

  // Time quantization
  // white_dt: how often white noise changes
  // drift_dt: how often bias random walk gets a new increment
  double white_dt = 0.01;
  double drift_dt = 0.01;

  // Bias state
  gz::math::Vector3d bias{0,0,0};

  // Track how far the drift has been integrated
  int64_t last_drift_tick = -1;

  Vec3NoiseModel() = default;

  Vec3NoiseModel(
      gz::math::Vector3d white_base,
      gz::math::Vector3d rw_base,
      double w_scale = 1.0,
      double d_scale = 1.0,
      gz::math::Vector3d initial_bias = {0,0,0},
      uint64_t deterministic_seed = 0,
      double white_dt_sec = 0.01,
      double drift_dt_sec = 0.01)
      : sigma_white_base(white_base),
        sigma_rw_base(rw_base),
        white_scale(w_scale),
        drift_scale(d_scale),
        seed(deterministic_seed),
        white_dt(white_dt_sec),
        drift_dt(drift_dt_sec),
        bias(initial_bias),
        last_drift_tick(-1) {}

  void Reset()
  {
    bias = {0,0,0};
    last_drift_tick = -1;
  }

  void Reset(gz::math::Vector3d initial_bias)
  {
    bias = initial_bias;
    last_drift_tick = -1;
  }

private:
  static uint64_t SplitMix64(uint64_t x)
  {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= (x >> 31);
    return x;
  }

  static double Uniform01(uint64_t key)
  {
    return (SplitMix64(key) >> 11) * (1.0 / 9007199254740992.0);
  }

  static double Normal01(uint64_t key1, uint64_t key2)
  {
    double u1 = Uniform01(key1);
    double u2 = Uniform01(key2);
    u1 = std::max(u1, 1e-12);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
  }

  static int64_t TimeToTick(double t, double dt)
  {
    // Stable floor-based bucket mapping
    return static_cast<int64_t>(std::floor(t / dt));
  }

  double NoiseSample(int axis, int stream_id, int64_t tick) const
  {
    const uint64_t tick_u = static_cast<uint64_t>(tick);

    const uint64_t base =
        seed ^
        (0xD1B54A32D192ED03ULL * (tick_u + 1)) ^
        (0x94D049BB133111EBULL * static_cast<uint64_t>(axis + 1)) ^
        (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(stream_id + 1));

    const uint64_t k1 = base ^ 0xA24BAED4963EE407ULL;
    const uint64_t k2 = base ^ 0x9FB21C651E98DF25ULL;

    return Normal01(k1, k2);
  }

  gz::math::Vector3d WhiteAtTime(double simTimeSec) const
  {
    const int64_t tick = TimeToTick(simTimeSec, white_dt);

    return {
      (sigma_white_base.X() * white_scale) * NoiseSample(0, 0, tick),
      (sigma_white_base.Y() * white_scale) * NoiseSample(1, 0, tick),
      (sigma_white_base.Z() * white_scale) * NoiseSample(2, 0, tick)
    };
  }

  void AdvanceBiasToTime(double simTimeSec)
  {
    const int64_t target_tick = TimeToTick(simTimeSec, drift_dt);
    if (target_tick < 0)
      return;

    if (last_drift_tick < 0) {
      last_drift_tick = -1;
    }

    for (int64_t tick = last_drift_tick + 1; tick <= target_tick; ++tick) {
      const double sdt = std::sqrt(drift_dt);

      bias += gz::math::Vector3d(
        (sigma_rw_base.X() * drift_scale) * sdt * NoiseSample(0, 1, tick),
        (sigma_rw_base.Y() * drift_scale) * sdt * NoiseSample(1, 1, tick),
        (sigma_rw_base.Z() * drift_scale) * sdt * NoiseSample(2, 1, tick)
      );
    }

    last_drift_tick = target_tick;
  }

public:
  gz::math::Vector3d Apply(const gz::math::Vector3d &trueVal, double simTimeSec)
  {
    AdvanceBiasToTime(simTimeSec);
    return trueVal + bias + WhiteAtTime(simTimeSec);
  }
};

Vec3NoiseModel gyro_noise_model(
    {0.001, 0.001, 0.001},      // white base
    {0.0002, 0.0002, 0.0002},   // random walk base
    1.0,                        // white scale
    1.0,                        // drift scale
    {0,0,0},                    // initial bias
    12345,                      // seed
    0.01,                       // white_dt (100 Hz)
    0.01                        // drift_dt (100 Hz)
);

Vec3NoiseModel accel_noise_model(
    {0.02, 0.02, 0.02},
    {0.01, 0.01, 0.01},
    1.0,
    1.0,
    {0,0,0},
    23456,
    0.01,
    0.01
);

Vec3NoiseModel mag_noise_model(
    {0.001, 0.001, 0.001},
    {0.0001, 0.0001, 0.0001},
    1.0,
    1.0,
    {0,0,0},
    34567,
    0.02,   // maybe 50 Hz
    0.02
);

class DeterministicNoiseTestService
{
public:
  DeterministicNoiseTestService(gz::transport::Node &node, const std::string &service_name)
  {
    node.Advertise(service_name, &DeterministicNoiseTestService::OnRequest, this);
  }
private:
  bool OnRequest(const gz::msgs::Empty &, gz::msgs::StringMsg &response)
  {
    // generate 5 noise samples from a base value and then do it again and compare the results
    std::ostringstream ss;
    double simTimeSec = 123.456; // fixed time for testing
    // arrays for storing noisy samples of size [5][3] for gyro, accel, mag
    gz::math::Vector3d gyro_samples[5];
    gz::math::Vector3d accel_samples[5];
    gz::math::Vector3d mag_samples[5];

    for (int i = 0; i < 5; i++) {
      gz::math::Vector3d trueVal(1.0, 2.0, 3.0);
      gyro_samples[i] = gyro_noise_model.Apply(trueVal, simTimeSec + i);
      accel_samples[i] = accel_noise_model.Apply(trueVal, simTimeSec + i);
      mag_samples[i] = mag_noise_model.Apply(trueVal, simTimeSec + i);
      ss << std::fixed << std::setprecision(6);
      ss << "Sample " << i << ", Time:" << simTimeSec + i << ":\n";
      ss << "  Gyro:  " << gyro_samples[i].X() << ", " << gyro_samples[i].Y() << ", " << gyro_samples[i].Z() << "\n";
      ss << "  Accel: " << accel_samples[i].X() << ", " << accel_samples[i].Y() << ", " << accel_samples[i].Z() << "\n";
      ss << "  Mag:   " << mag_samples[i].X() << ", " << mag_samples[i].Y() << ", " << mag_samples[i].Z() << "\n";
    }

    // reset the noise models to ensure they start from the same state
    gyro_noise_model.Reset();
    accel_noise_model.Reset();
    mag_noise_model.Reset();

    for (int i = 0; i < 5; i++) {
      gz::math::Vector3d trueVal(1.0, 2.0, 3.0);
      gz::math::Vector3d gyro_sample_2 = gyro_noise_model.Apply(trueVal, simTimeSec + i);
      gz::math::Vector3d accel_sample_2 = accel_noise_model.Apply(trueVal, simTimeSec + i);
      gz::math::Vector3d mag_sample_2 = mag_noise_model.Apply(trueVal, simTimeSec + i);
      ss << std::fixed << std::setprecision(6);
      ss << "Sample " << i << ", Time:" << simTimeSec + i << " (second call):\n";
      ss << "  Gyro:  " << gyro_sample_2.X() << ", " << gyro_sample_2.Y() << ", " << gyro_sample_2.Z() << "\n";
      ss << "  Accel: " << accel_sample_2.X() << ", " << accel_sample_2.Y() << ", " << accel_sample_2.Z() << "\n";
      ss << "  Mag:   " << mag_sample_2.X() << ", " << mag_sample_2.Y() << ", " << mag_sample_2.Z() << "\n";

      // Compare with the first sample
      bool gyro_match = (gyro_samples[i].X() == gyro_sample_2.X()) &&
                        (gyro_samples[i].Y() == gyro_sample_2.Y()) &&
                        (gyro_samples[i].Z() == gyro_sample_2.Z());
      bool accel_match = (accel_samples[i].X() == accel_sample_2.X()) &&
                         (accel_samples[i].Y() == accel_sample_2.Y()) &&
                         (accel_samples[i].Z() == accel_sample_2.Z());
      bool mag_match = (mag_samples[i].X() == mag_sample_2.X()) &&
                       (mag_samples[i].Y() == mag_sample_2.Y()) &&
                       (mag_samples[i].Z() == mag_sample_2.Z());

      ss << "  Gyro match: " << (gyro_match ? "YES" : "NO") << "\n";
      ss << "  Accel match: " << (accel_match ? "YES" : "NO") << "\n";
      ss << "  Mag match:   " << (mag_match ? "YES" : "NO") << "\n";
    }

    // reset again to show that it produces the same sequence on the next call
    gyro_noise_model.Reset();
    accel_noise_model.Reset();
    mag_noise_model.Reset();

    response.set_data(ss.str());
    return true;
  }
};

// struct Vec3NoiseModel {
//   // BASE (reference) noise levels — tune these once
//   gz::math::Vector3d sigma_white_base{0,0,0}; // std-dev of white noise
//   gz::math::Vector3d sigma_rw_base{0,0,0};    // std-dev of random-walk drift

//   // ======= YOUR TWO KNOBS =======
//   double white_scale = 1.0;  // <-- scales instantaneous noise
//   double drift_scale = 1.0;  // <-- scales long-term drift
//   // ==============================

//   // current bias state (drifting over time)
//   gz::math::Vector3d bias{0,0,0};

//   std::mt19937 rng{std::random_device{}()};
//   std::normal_distribution<double> N{0.0, 1.0};

//   // === NEW: constructors ===

//   // Default constructor (keeps your existing defaults)
//   Vec3NoiseModel() = default;

//   // Convenience constructor
//   Vec3NoiseModel(
//       gz::math::Vector3d white_base,
//       gz::math::Vector3d rw_base,
//       double w_scale = 1.0,
//       double d_scale = 1.0,
//       gz::math::Vector3d initial_bias = {0,0,0})
//   : sigma_white_base(white_base),
//     sigma_rw_base(rw_base),
//     white_scale(w_scale),
//     drift_scale(d_scale),
//     bias(initial_bias)
//   {}

//   // === existing methods (unchanged) ===

//   gz::math::Vector3d SampleWhite() {
//     return {
//       (sigma_white_base.X() * white_scale) * N(rng),
//       (sigma_white_base.Y() * white_scale) * N(rng),
//       (sigma_white_base.Z() * white_scale) * N(rng)
//     };
//   }

//   void StepBias(double dt) {
//     const double sdt = std::sqrt(dt);

//     bias += gz::math::Vector3d(
//       (sigma_rw_base.X() * drift_scale) * sdt * N(rng),
//       (sigma_rw_base.Y() * drift_scale) * sdt * N(rng),
//       (sigma_rw_base.Z() * drift_scale) * sdt * N(rng)
//     );
//   }

//   gz::math::Vector3d Apply(const gz::math::Vector3d &trueVal, double dt) {
//     StepBias(dt);
//     return trueVal + bias + SampleWhite();
//   }
// };

// Vec3NoiseModel gyro_noise_model(
//     {0.001, 0.001, 0.001},   // sigma_white_base
//     {0.0002, 0.0002, 0.0002},// sigma_rw_base
//     0.0,                     // white_scale
//     0.0                      // drift_scale
// );

// Vec3NoiseModel accel_noise_model(
//     {0.02, 0.02, 0.02},
//     {0.01, 0.01, 0.01},
//     0.0,
//     0.0
// );

// Vec3NoiseModel mag_noise_model(
//     {0.001, 0.001, 0.001},
//     {0.0001, 0.0001, 0.0001},
//     0.0,
//     0.0
// );

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
 * @brief Get orientation for synthetic mag reading
 *
 *
 */
class CorrectMagService
{
public:
  struct SensorRPY {
    double roll;
    double pitch;
    double yaw;
    SensorRPY(double r=0.0, double p=0.0, double y=0.0) : roll(r), pitch(p), yaw(y) {}
  };

  CorrectMagService(gz::transport::Node &node,
                    const std::string &pose_topic,
                    const std::string &mag_topic,
                    const std::string &service_name,
                    SensorRPY sensor_rpy = SensorRPY())
    : sensor_rpy_(sensor_rpy)
  {
    if (pose_topic != "NONE")
      node.Subscribe(pose_topic, &CorrectMagService::OnPoseMsg, this);

    if (mag_topic != "NONE")
      node.Subscribe(mag_topic, &CorrectMagService::OnMagMsg, this);

    node.Advertise(service_name, &CorrectMagService::OnServiceRequest, this);
  }

private:
  // === Helpers: build R = Rz(yaw)*Ry(pitch)*Rx(roll) ===
  static gz::math::Matrix3d RzRyRx(double roll, double pitch, double yaw)
  {
    const double cr = std::cos(roll),  sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw),   sy = std::sin(yaw);

    gz::math::Matrix3d R;
    R(0,0) = cy*cp;            R(0,1) = cy*sp*sr - sy*cr;  R(0,2) = cy*sp*cr + sy*sr;
    R(1,0) = sy*cp;            R(1,1) = sy*sp*sr + cy*cr;  R(1,2) = sy*sp*cr - cy*sr;
    R(2,0) = -sp;              R(2,1) = cp*sr;             R(2,2) = cp*cr;
    return R;
  }

  static gz::math::Matrix3d RotFromQuat(const gz::msgs::Quaternion &qmsg)
  {
    // Assumption (matches your python): msg orientation is body->world quaternion (w,x,y,z)
    const double w = qmsg.w();
    const double x = qmsg.x();
    const double y = qmsg.y();
    const double z = qmsg.z();

    const double n = std::sqrt(w*w + x*x + y*y + z*z);
    if (n == 0.0) return gz::math::Matrix3d::Identity;

    const double qw = w/n, qx = x/n, qy = y/n, qz = z/n;

    gz::math::Matrix3d R;
    R(0,0) = 1 - 2*(qy*qy + qz*qz);  R(0,1) = 2*(qx*qy - qw*qz);      R(0,2) = 2*(qx*qz + qw*qy);
    R(1,0) = 2*(qx*qy + qw*qz);      R(1,1) = 1 - 2*(qx*qx + qz*qz);  R(1,2) = 2*(qy*qz - qw*qx);
    R(2,0) = 2*(qx*qz - qw*qy);      R(2,1) = 2*(qy*qz + qw*qx);      R(2,2) = 1 - 2*(qx*qx + qy*qy);
    return R;
  }

  void OnPoseMsg(const gz::msgs::Pose_V &msg)
  {
    if (msg.pose_size() <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    latest_pose_q_ = msg.pose(0).orientation();
    have_pose_ = true;
  }

  void OnMagMsg(const gz::msgs::Magnetometer &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_raw_mag_ = msg;
    have_mag_ = true;
  }

  bool OnServiceRequest(const gz::msgs::Empty &, gz::msgs::Magnetometer &rep)
  {
    gz::msgs::Quaternion q;
    gz::msgs::Magnetometer raw;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!have_pose_ || !have_mag_) {
        // raw (or 0) return when data not ready
        rep = latest_raw_mag_;
        return true;
      }
      q = latest_pose_q_;
      raw = latest_raw_mag_;
    }

    // === Build coordinate transformation matrix from world to sensor ===
    const gz::math::Matrix3d R_wb = RotFromQuat(q);
    const gz::math::Matrix3d C_wb = R_wb.Transposed();
    const gz::math::Matrix3d R_bs = RzRyRx(sensor_rpy_.roll, sensor_rpy_.pitch, sensor_rpy_.yaw);
    const gz::math::Matrix3d C_bs = R_bs.Transposed();
    const gz::math::Matrix3d C_ws = C_bs * C_wb;

    // === Build NED -> ENU transformation matrix ===
    gz::math::Matrix3d C_ww;
    C_ww(0,0)=0; C_ww(0,1)=1; C_ww(0,2)=0;
    C_ww(1,0)=1; C_ww(1,1)=0; C_ww(1,2)=0;
    C_ww(2,0)=0; C_ww(2,1)=0; C_ww(2,2)=-1;

    gz::math::Vector3d m_raw(raw.field_tesla().x(),
                             raw.field_tesla().y(),
                             raw.field_tesla().z());

    // corrected = C_ws * C_ww * (C_ws^T * m_raw)
    const gz::math::Vector3d tmp = C_ws.Transposed() * m_raw;
    const gz::math::Vector3d m_corr = C_ws * (C_ww * tmp);

    double dt = 0.01; // assuming 100 Hz update rate
    gz::math::Vector3d noisy_mag_corr = mag_noise_model.Apply(m_corr, read_latest_sim_time());
    // // std::cerr << "NEW READING:" << std::endl;
    // // std::cerr << "\tCorrected mag: " << m_corr.X() << ", " << m_corr.Y() << ", " << m_corr.Z() << std::endl;
    // // std::cerr << "\tNoisy mag: " << noisy_mag_corr.X() << ", " << noisy_mag_corr.Y() << ", " << noisy_mag_corr.Z() << std::endl;
    gz::math::Vector3d noisy_msg_corr = noisy_mag_corr;

    gz::msgs::Magnetometer latest_msg_corr_ = raw; // timestamp/frame_id 유지
    latest_msg_corr_.mutable_field_tesla()->set_x(noisy_msg_corr.X());
    latest_msg_corr_.mutable_field_tesla()->set_y(noisy_msg_corr.Y());
    latest_msg_corr_.mutable_field_tesla()->set_z(noisy_msg_corr.Z());

    rep = latest_msg_corr_;
    return true;
  }

  SensorRPY sensor_rpy_;

  std::mutex mutex_;
  bool have_pose_{false};
  bool have_mag_{false};

  gz::msgs::Quaternion latest_pose_q_;
  gz::msgs::Magnetometer latest_raw_mag_;
};

/**
 * @brief Get mag reading
 */
class GetMagReadingService
{
public:
  GetMagReadingService(gz::transport::Node &node,
                       const std::string &topic_name,
                       const std::string &service_name)
  {
    if (topic_name == "NONE")
      return;
    node.Subscribe(topic_name, &GetMagReadingService::OnMagMsg, this);
    node.Advertise(service_name, &GetMagReadingService::OnServiceRequest, this);
  }
private:
  void OnMagMsg(const gz::msgs::Magnetometer &msg)
  {
    gz::math::Vector3d true_mag(
      msg.field_tesla().x(),
      msg.field_tesla().y(),
      msg.field_tesla().z()
    );
    double dt = 0.01; // assuming 100 Hz update rate
    gz::math::Vector3d noisy_mag = mag_noise_model.Apply(true_mag, read_latest_sim_time());
    std::lock_guard<std::mutex> lock(mutex_);
    gz::msgs::Magnetometer noisy_msg = msg;
    noisy_msg.mutable_field_tesla()->set_x(noisy_mag.X());
    noisy_msg.mutable_field_tesla()->set_y(noisy_mag.Y());
    noisy_msg.mutable_field_tesla()->set_z(noisy_mag.Z());
    latest_msg_ = noisy_msg;
  }

  bool OnServiceRequest(const gz::msgs::Empty &,
                        gz::msgs::Magnetometer &rep)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    rep = latest_msg_;
    return true;
  }

  gz::msgs::Magnetometer latest_msg_;
  std::mutex mutex_;
};

/**
 * @brief Service handler for /get_laser_scan
 *
 * This class subscribes to the /lidar topic (of type gz::msgs::LaserScan) and provides
 * responses to the /get_laser_scan service. Argument to the service call is the number
 * of samples to return. There are 400 samples in each full scan so the subscribe will continuously
 * update the latest_msg_ with the most recent scan, and the service call will keep track of the
 * index of the next sample to return, looping back to the beginning when it reaches the end. We will use
 * a mutex to protect access to latest_msg_ and the sample index since they are shared between the subscribe callback and the service handler.
 */
class GetLaserScanService
{public:
  GetLaserScanService(gz::transport::Node &node,
                      const std::string &topic_name,
                      const std::string &service_name)
  {
    if (topic_name == "NONE")
      return;
    node.Subscribe(topic_name, &GetLaserScanService::OnLaserScanMsg, this);
    node.Advertise(service_name, &GetLaserScanService::OnServiceRequest, this);
  }
private:
  void OnLaserScanMsg(const gz::msgs::LaserScan &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_msg_ = msg;
    auto ranges = latest_msg_.ranges();
    for (int i = 0; i < 400; ++i) {
      latest_scan_[i] = ranges.Get(i);
    }
    if (!have_scan_) {
      have_scan_ = true;
    }
  }

  bool OnServiceRequest(const gz::msgs::UInt32 &req,
                        gz::msgs::StringMsg &rep)
  {
    if (!have_scan_) {
      rep.set_data("None");
      return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::string scan_data;
    int num_samples = req.data();
    // Format must be "start_index:range0,range1,..." so clients can parse with find(':')
    // and stoi(start_index). Do NOT concatenate index + first range without ':' or
    // "185" + "2.93" becomes "1852.93" and start_index is parsed wrong.
    scan_data += std::to_string(sample_index_);
    scan_data += ':';
    for (int i = 0; i < num_samples; ++i) {
      int index = (sample_index_ + i) % 400; // loop back to the beginning if we reach the end
      if (i > 0) {
        scan_data += ',';
      }
      const double r = latest_scan_[index];
      if (std::isinf(r) || std::isnan(r)) {
        scan_data += "inf";
      } else {
        scan_data += std::to_string(r);
      }
    }
    sample_index_ = (sample_index_ + num_samples) % 400; // update sample index for next call
    rep.set_data(scan_data);
    return true;
  }

  double latest_scan_[400];
  gz::msgs::LaserScan latest_msg_;
  bool have_scan_ = false;
  int sample_index_ = 0;
  std::mutex mutex_;
};


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
    // check if msgtype is magnetometer
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
 * @brief Service handler for /set_noise_scale
 *
 * This service sets the noise scale factors for the IMU noise models.
 * The request message is expected to be a string in the format
 * "<acc/gyro/mag>,white_scale,drift_scale" where both values are doubles.
 *
 * @param[in] req A string message with noise scale parameters
 * @param[out] rep A boolean message indicating success or failure
 * @return true if the noise scales were set successfully, false otherwise
 */
bool OnSetNoiseScaleRequest(const gz::msgs::StringMsg &req,
                            gz::msgs::Boolean &rep)
{
  std::string payload = req.data();
  std::istringstream ss(payload);
  std::string token;
  std::getline(ss, token, ',');
  std::string sensor_type = token;
  std::getline(ss, token, ',');
  double white_scale = std::stod(token);
  std::getline(ss, token, ',');
  double drift_scale = std::stod(token);

  // printf("got payload: %s\n", payload.c_str());

  if (sensor_type == "gyro") {
    gyro_noise_model.white_scale = white_scale;
    gyro_noise_model.drift_scale = drift_scale;
  } else if (sensor_type == "accel") {
    accel_noise_model.white_scale = white_scale;
    accel_noise_model.drift_scale = drift_scale;
  } else if (sensor_type == "mag") {
    mag_noise_model.white_scale = white_scale;
    mag_noise_model.drift_scale = drift_scale;
  } else {
    rep.set_data(false);
    return true;
  }
  rep.set_data(true);
  return true;
}

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
    } else if (model_name == "bicopter") {
      pwm_values_[0] = 1000;
      pwm_values_[1] = 1000;
      pwm_values_[2] = 1500;
      pwm_values_[3] = 1500;
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
        update_latest_sim_time(std::stod(response.substr(start, end - start)));
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

    while (run_until_time_s_ - read_latest_sim_time() > 0.1 )
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
        if (read_latest_sim_time() < run_until_time_s_)
        {
          if (!toggle) {
              toggle = true;
              std::cout << "Advancing simulation to time: " << run_until_time_s_ << " s\n";
              std::cout << "Current latest_time_s_: " << read_latest_sim_time() << " s\n";
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
              update_latest_sim_time(std::stod(response.substr(start, end - start)));
            }

            imu_data_t imu = parse_imu_json(response);
            {
              gz::math::Vector3d true_accel(imu.accel_x, imu.accel_y, imu.accel_z);
              gz::math::Vector3d true_gyro(imu.gyro_x, imu.gyro_y, imu.gyro_z);
              gz::math::Vector3d noisy_accel = accel_noise_model.Apply(true_accel, read_latest_sim_time());
              gz::math::Vector3d noisy_gyro  = gyro_noise_model.Apply(true_gyro, read_latest_sim_time());
              imu.accel_x = noisy_accel.X();
              imu.accel_y = noisy_accel.Y();
              imu.accel_z = noisy_accel.Z();
              imu.gyro_x  = noisy_gyro.X();
              imu.gyro_y  = noisy_gyro.Y();
              imu.gyro_z  = noisy_gyro.Z();
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

  // double latest_time_s_{0.0};
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

/**
 * @brief Status for hardfault
 *
 * This structure just lets us know if we hardfaulted and stores the corresponding instruction address.
 */
class HFService
{
public:
  HFService(gz::transport::Node &node,
             const std::string &service_name)
  {
    node.Advertise("/set_hardfault_pc", &HFService::OnServiceRequest, this);
    node.Advertise("/get_hf_status", &HFService::OnIsHardFault, this);
  }
private:
  bool OnServiceRequest(const gz::msgs::UInt32 &req,
                       gz::msgs::Empty &rep)
  {
    hardfault_address_ = req.data();
    return true;
  }
  bool OnIsHardFault(const gz::msgs::Empty &,
                     gz::msgs::StringMsg &rep)
  {
    // return format is "<bool>,<address>?"
    std::string status = "false,0";
    if (hardfault_address_ != 0) {
      status = "true," + std::to_string(hardfault_address_);
    }
    rep.set_data(status);
    return true;
  }

  // uint64_t hardfault_address_{0};
  std::atomic<uint64_t> hardfault_address_{0};

};

/**
 * @brief Multi-sensor service that aggregates the latest readings from
 *        clock, pose, navsat, imu, and magnetometer topics and returns
 *        them in a single service response.
 *
 * This is useful if you need a snapshot of multiple sensor streams at once
 * without making multiple service calls.
 */
class MultiSensorService
{
public:
  MultiSensorService(gz::transport::Node &node,
                     const std::string &clock_topic,
                     const std::string &pose_topic,
                     const std::string &navsat_topic,
                     const std::string &imu_topic,
                     const std::string &mag_topic,
                     const std::string &service_name)
  {
    node.Subscribe(clock_topic, &MultiSensorService::OnClockMsg, this);
    node.Subscribe(pose_topic, &MultiSensorService::OnPoseMsg, this);
    node.Subscribe(navsat_topic, &MultiSensorService::OnNavSatMsg, this);
    node.Subscribe(imu_topic, &MultiSensorService::OnImuMsg, this);
    node.Subscribe(mag_topic, &MultiSensorService::OnMagMsg, this);

    node.Advertise(service_name, &MultiSensorService::OnServiceRequest, this);
  }

private:
  // === Topic Callbacks ===
  void OnClockMsg(const gz::msgs::Clock &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clock_msg_ = msg;
  }

  void OnPoseMsg(const gz::msgs::Pose_V &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pose_msg_ = msg;
  }

  void OnNavSatMsg(const gz::msgs::NavSat &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    navsat_msg_ = msg;
  }

  void OnImuMsg(const gz::msgs::IMU &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    imu_msg_ = msg;
  }

  void OnMagMsg(const gz::msgs::Magnetometer &msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mag_msg_ = msg;
  }

  // === Service Handler ===
  bool OnServiceRequest(const gz::msgs::Empty &,
                        gz::msgs::Any &rep)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Build a JSON-like string for compact transport in gz::msgs::Any
    // (Alternatively, define a custom protobuf message with these fields.)
    std::ostringstream oss;
    oss << "{"
        << "\"clock\": " << clock_msg_.DebugString() << ","
        << "\"pose\": " << pose_msg_.DebugString() << ","
        << "\"navsat\": " << navsat_msg_.DebugString() << ","
        << "\"imu\": " << imu_msg_.DebugString() << ","
        << "\"mag\": " << mag_msg_.DebugString()
        << "}";

    rep.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    rep.set_string_value(oss.str());

    return true;
  }

  // === Latest Messages ===
  gz::msgs::Clock clock_msg_;
  gz::msgs::Pose_V pose_msg_;
  gz::msgs::NavSat navsat_msg_;
  gz::msgs::IMU imu_msg_;
  gz::msgs::Magnetometer mag_msg_;

  std::mutex mutex_;
};

/**
 * @brief Parse /world/<name>/... from a full Gazebo topic path.
 */
static std::string WorldNameFromNavsatTopic(const std::string &navsat_topic)
{
  const std::string key = "/world/";
  const std::size_t p = navsat_topic.find(key);
  if (p == std::string::npos) {
    return "runway";
  }
  const std::size_t start = p + key.size();
  const std::size_t slash = navsat_topic.find('/', start);
  if (slash == std::string::npos) {
    return "runway";
  }
  return navsat_topic.substr(start, slash - start);
}

/**
 * @brief Parse model scoped name from /model/<name>/pose.
 */
static std::string ModelNameFromPoseTopic(const std::string &pose_topic)
{
  const std::string pre = "/model/";
  const std::string suf = "/pose";
  if (pose_topic.size() <= pre.size() + suf.size()) {
    return "";
  }
  if (pose_topic.compare(0, pre.size(), pre) != 0) {
    return "";
  }
  if (pose_topic.compare(pose_topic.size() - suf.size(), suf.size(), suf) != 0) {
    return "";
  }
  return pose_topic.substr(pre.size(), pose_topic.size() - pre.size() - suf.size());
}

/**
 * @brief Advertises /place_box_relative: spawn a box in the sim world at a
 *        pose relative to the vehicle model (Gazebo EntityFactory relative_to).
 *
 * Request body: JSON on gz::msgs::StringMsg::data(), e.g.
 *   {"x":1,"y":0,"z":0,"sx":0.5,"sy":0.5,"sz":0.5,"roll":0,"pitch":0,"yaw":0}
 * Optional keys: name (unique model name), relative_to (override frame),
 * mass (kg), wait (if true, block for gz-sim reply — slows sim; default false),
 * timeout_ms (only when wait=true).
 *
 * Default behavior sends /world/.../create asynchronously so this service returns
 * immediately and does not block simulation.
 *
 * Response: JSON with dispatched, transport_ok, name; if wait=true also gz_result.
 */
class PlaceRelativeBoxService
{
public:
  PlaceRelativeBoxService(gz::transport::Node &node,
                          const std::string &world_name,
                          const std::string &vehicle_model_name)
    : node_(node),
      world_(world_name),
      vehicle_(vehicle_model_name)
  {
    node_.Advertise("/place_box_relative",
                    &PlaceRelativeBoxService::OnRequest, this);
  }

private:
  static bool IsSafeModelToken(const std::string &s)
  {
    for (char c : s) {
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
        return false;
      }
    }
    return !s.empty();
  }

  bool OnRequest(const gz::msgs::StringMsg &req, gz::msgs::StringMsg &rep)
  {
    try {
      const json j = json::parse(req.data());
      const double x = j.value("x", 0.0);
      const double y = j.value("y", 0.0);
      const double z = j.value("z", 0.0);
      const double sx = j.value("sx", 1.0);
      const double sy = j.value("sy", 1.0);
      const double sz = j.value("sz", 1.0);
      const double mass = j.value("mass", 1.0);
      const double roll = j.value("roll", 0.0);
      const double pitch = j.value("pitch", 0.0);
      const double yaw = j.value("yaw", 0.0);

      std::string rel = j.value("relative_to", vehicle_);
      if (rel.empty()) {
        rel = vehicle_;
      }

      std::string box_name = j.value("name", std::string(""));
      if (!IsSafeModelToken(box_name)) {
        box_name = "placed_box_" + std::to_string(++counter_);
      }

      std::ostringstream sdf;
      sdf << std::setprecision(17);
      sdf << "<?xml version=\"1.0\" ?><sdf version=\"1.6\">"
          << "<model name=\"inline_box\">"
          << "<link name=\"link\">"
          << "<inertial><mass>" << mass << "</mass></inertial>"
          << "<visual name=\"v\"><geometry><box><size>"
          << sx << " " << sy << " " << sz
          << "</size></box></geometry></visual>"
          << "<collision name=\"c\"><geometry><box><size>"
          << sx << " " << sy << " " << sz
          << "</size></box></geometry></collision>"
          << "</link></model></sdf>";

      const gz::math::Quaterniond q(roll, pitch, yaw);

      gz::msgs::EntityFactory factory;
      factory.set_sdf(sdf.str());
      factory.set_name(box_name);
      factory.set_allow_renaming(true);
      factory.set_relative_to(rel);
      factory.mutable_pose()->mutable_position()->set_x(x);
      factory.mutable_pose()->mutable_position()->set_y(y);
      factory.mutable_pose()->mutable_position()->set_z(z);
      factory.mutable_pose()->mutable_orientation()->set_x(q.X());
      factory.mutable_pose()->mutable_orientation()->set_y(q.Y());
      factory.mutable_pose()->mutable_orientation()->set_z(q.Z());
      factory.mutable_pose()->mutable_orientation()->set_w(q.W());

      const std::string svc = "/world/" + world_ + "/create";
      const bool wait_for_reply = j.value("wait", false);

      json out;
      out["name"] = box_name;
      out["async"] = !wait_for_reply;

      if (wait_for_reply) {
        gz::msgs::Boolean gz_rep;
        bool gz_result = false;
        unsigned int timeout_ms =
            static_cast<unsigned int>(j.value("timeout_ms", 60000));
        if (timeout_ms < 1000u) {
          timeout_ms = 1000u;
        }
        const bool executed =
            node_.Request(svc, factory, timeout_ms, gz_rep, gz_result);
        out["transport_ok"] = executed;
        out["gz_result"] = gz_result;
        out["gz_accepted"] = gz_rep.data();
        out["create_timeout_ms"] = timeout_ms;
        if (!executed) {
          out["error"] = "Timed out or unreachable: " + svc;
        } else if (!gz_result) {
          out["error"] = "Gazebo create returned false";
        }
      } else {
        std::function<void(const gz::msgs::Boolean &, bool)> on_create_reply =
            [box_name](const gz::msgs::Boolean &gz_rep, bool gz_result) {
              if (DEBUG) {
                std::cerr << "[place_box_relative] async create \"" << box_name
                          << "\" gz_result=" << gz_result
                          << " accepted=" << gz_rep.data() << "\n";
              } else if (!gz_result) {
                std::cerr << "[place_box_relative] create failed for \""
                          << box_name << "\"\n";
              }
            };
        const bool dispatched = node_.Request(svc, factory, on_create_reply);
        out["dispatched"] = dispatched;
        out["transport_ok"] = dispatched;
        if (!dispatched) {
          out["error"] = "Could not send create request (discovery failed?): " + svc;
        }
      }

      rep.set_data(out.dump());
      return true;
    } catch (const std::exception &e) {
      json err;
      err["transport_ok"] = false;
      err["gz_result"] = false;
      err["error"] = e.what();
      rep.set_data(err.dump());
      return true;
    }
  }

  gz::transport::Node &node_;
  std::string world_;
  std::string vehicle_;
  std::atomic<uint64_t> counter_{0};
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
  std::string pose_topic = "/model/r1_rover/pose";
  std::string altimeter_topic = "NONE";
  std::string imu_topic = "/world/runway/model/r1_rover/link/base_link/sensor/imu_sensor/imu";
  if (model_name == "gs_drone") {
    navsat_topic = "/world/runway/model/gs_drone/link/sensors/sensor/navsat_sensor/navsat";
    mag_topic = "/world/runway/model/gs_drone/link/sensors/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  } else if (model_name == "iris") {
    navsat_topic = "/world/iris_runway/model/iris_with_ardupilot/model/iris_with_standoffs/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/iris_runway/model/iris_with_ardupilot/model/iris_with_standoffs/link/base_link/sensor/magnetometer_sensor/magnetometer";
    pose_topic = "/model/iris_with_ardupilot/pose";
    joint_states_topic = "NONE";
  } else if (model_name == "bicopter") {
    navsat_topic = "/world/runway/model/bicopter_with_ardupilot/model/bicopter/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/runway/model/bicopter_with_ardupilot/model/bicopter/link/base_link/sensor/magnetometer_sensor/magnetometer";
    pose_topic = "/model/bicopter_with_ardupilot/pose";
    joint_states_topic = "NONE";
  } else if (model_name == "vtail_plane") {
    navsat_topic = "/world/runway/model/skywalker_x8/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/runway/model/skywalker_x8/link/base_link/sensor/magnetometer_sensor/magnetometer";
    pose_topic = "/model/skywalker_x8/pose";
    imu_topic = "/world/runway/model/skywalker_x8/link/imu_link/sensor/imu_sensor/imu";
    // pose_topic = "/world/runway/dynamic_pose/info";
    // navsat_topic = "/world/runway/model/mini_talon_vtail/link/base_link/sensor/navsat_sensor/navsat";
    // mag_topic = "/world/runway/model/mini_talon_vtail/link/base_link/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  } else if (model_name == "skywalker_x8_quad") {
    navsat_topic = "/world/runway/model/skywalker_x8_quad/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/runway/model/skywalker_x8_quad/link/base_link/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  } else if (model_name == "blueboat") {
    navsat_topic = "/world/waves/model/blueboat/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/waves/model/blueboat/link/base_link/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  } else if (model_name == "bluerov2") {
    navsat_topic = "/world/bluerov2_underwater/model/bluerov2/link/base_link/sensor/navsat_sensor/navsat";
    mag_topic =    "/world/bluerov2_underwater/model/bluerov2/link/base_link/sensor/magnetometer_sensor/magnetometer";
    joint_states_topic = "NONE";
  }

  if (model_name == "r1_rover") {
    altimeter_topic = "/world/runway/altitude";
  }

  const std::string sim_world_name = WorldNameFromNavsatTopic(navsat_topic);
  std::string vehicle_scope_name = ModelNameFromPoseTopic(pose_topic);
  if (vehicle_scope_name.empty()) {
    vehicle_scope_name = model_name;
  }
  PlaceRelativeBoxService placeBoxService(node, sim_world_name, vehicle_scope_name);

  GenericSensorService<gz::msgs::NavSat> navSatService(
    node,
    navsat_topic,
    "/get_navsat_reading"
  );

  // GenericSensorService<gz::msgs::Magnetometer> magService(
  //   node,
  //   mag_topic,
  //   "/get_mag_reading"
  // );

  GenericSensorService<gz::msgs::Altimeter> altimeterService(
    node,
    altimeter_topic,
    "/get_altimeter_reading"
  );

  GetMagReadingService magService(
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

  // GenericSensorService<gz::msgs::LaserScan> laserScanService(
  //   node,
  //   "/lidar",
  //   "/get_laser_scan"
  // );

  GetLaserScanService laserScanService(
    node,
    "/lidar",
    "/get_laser_scan"
  );

  HFService hfService(
    node,
    "/get_hf_status"
  );

  ServoService servoService(
    node,
    "/set_run_until_time",
    "127.0.0.1",
    model_name,
    9002,
    5200
  );

  // std::string pose_topic = "/model/r1_rover/pose";
  // std::string pose_topic = "/model/gs_drone/pose";
  std::string clock_topic = "/world/runway/clock";

  MultiSensorService multiSensorService(
      node,
      clock_topic,
      pose_topic,
      navsat_topic,
      imu_topic,
      mag_topic,
      "/get_trace_state"
  );

  node.Advertise("/set_noise_scale", &OnSetNoiseScaleRequest);

  // YawService yawService(
  //   node,
  //   "/model/" + model_name + "/pose",
  //   "/get_yaw_reading"
  // );

  CorrectMagService::SensorRPY mag_rpy;
  mag_rpy.roll  = 0.0;
  mag_rpy.pitch = 0.0;
  mag_rpy.yaw   = 0.0;

  CorrectMagService correctMagService(
    node,
    pose_topic,
    mag_topic,
    "/get_corrected_mag_reading",
    mag_rpy
  );

  std::cout << "ArduPilot Services running...\n";

  READY = true;

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
