#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>

#include "prowl_mpu6050_driver/conversions.hpp"
#include "prowl_mpu6050_driver/gyro_bias.hpp"
#include "prowl_mpu6050_driver/mpu6050.hpp"
#include "prowl_mpu6050_driver/registers.hpp"

namespace prowl_mpu6050_driver
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr auto kRetryEvery = std::chrono::seconds(1);
constexpr auto kSettle = std::chrono::milliseconds(60);  // discard samples while the filter settles
constexpr auto kNoDataTimeout = std::chrono::seconds(1);
constexpr int kCalibrationAttempts = 3;  // then accept a moving window, with a warning

AccelRange accelRangeParam(int64_t g)
{
  switch (g) {
    case 2: return AccelRange::G2;
    case 4: return AccelRange::G4;
    case 8: return AccelRange::G8;
    case 16: return AccelRange::G16;
  }
  throw std::runtime_error("accel_range must be 2, 4, 8 or 16 (g)");
}

GyroRange gyroRangeParam(int64_t dps)
{
  switch (dps) {
    case 250: return GyroRange::Dps250;
    case 500: return GyroRange::Dps500;
    case 1000: return GyroRange::Dps1000;
    case 2000: return GyroRange::Dps2000;
  }
  throw std::runtime_error("gyro_range must be 250, 500, 1000 or 2000 (deg/s)");
}

Dlpf dlpfParam(int64_t hz)
{
  for (uint8_t i = 0; i <= static_cast<uint8_t>(Dlpf::Hz5); ++i) {
    const auto f = static_cast<Dlpf>(i);
    if (accelBandwidthHz(f) == hz) {
      return f;
    }
  }
  throw std::runtime_error("dlpf must be 260, 184, 94, 44, 21, 10 or 5 (Hz)");
}

}  // namespace

// Polls the MPU6050 on a background thread and publishes /imu/data_raw. At
// startup it averages calibration_time seconds of gyro readings, with the robot
// still, as the gyro bias; nothing is published until then. The stillness check
// averages over vibration_period first, which cancels the LiDAR's vibration. If the chip stops
// answering or stops producing data (it wakes up asleep after a power blip),
// it is set up again every second, keeping the bias.
class Mpu6050Node : public rclcpp::Node
{
public:
  Mpu6050Node()
  : Node("mpu6050_driver")
  {
    bus_ = static_cast<int>(declare_parameter<int64_t>("bus", kDefaultBus));
    const auto address = declare_parameter<int64_t>("address", kDefaultAddress);
    frame_id_ = declare_parameter("frame_id", std::string("imu_link"));
    config_.sample_rate_hz = declare_parameter("rate", 100.0);
    config_.accel_range = accelRangeParam(declare_parameter<int64_t>("accel_range", 4));
    config_.gyro_range = gyroRangeParam(declare_parameter<int64_t>("gyro_range", 500));
    config_.dlpf = dlpfParam(declare_parameter<int64_t>("dlpf", 44));
    const double calibration_time = declare_parameter("calibration_time", 2.0);
    max_calibration_stddev_ = declare_parameter("calibration_max_stddev", 0.3) * kDegToRad;
    const double vibration_period = declare_parameter("vibration_period", 0.1);
    const double stats_period = declare_parameter("stats_period", 5.0);

    if (address != 0x68 && address != 0x69) {
      throw std::runtime_error("address must be 0x68 (104) or 0x69 (105)");
    }
    if (config_.sample_rate_hz <= 0.0) {
      throw std::runtime_error("rate must be positive");
    }
    if (vibration_period < 0.0) {
      throw std::runtime_error("vibration_period must not be negative");
    }
    address_ = static_cast<uint8_t>(address);
    const double rate = sampleRateHz(config_.dlpf, sampleRateDivider(config_.dlpf, config_.sample_rate_hz));
    calibrating_ = calibration_time > 0.0;
    calibration_ = GyroBiasEstimator(static_cast<size_t>(std::lround(calibration_time * rate)),
      static_cast<size_t>(std::max(1L, std::lround(vibration_period * rate))));

    imu_pub_ = create_publisher<sensor_msgs::msg::Imu>("imu/data_raw", 10);
    if (stats_period > 0.0) {
      stats_timer_ = create_wall_timer(
        std::chrono::duration<double>(stats_period), [this] {logStats();});
    }
    last_stats_time_ = Clock::now();
    reader_ = std::thread([this] {readLoop();});
  }

  ~Mpu6050Node() override
  {
    running_ = false;
    if (reader_.joinable()) {
      reader_.join();
    }
  }

private:
  void readLoop()
  {
    while (running_ && rclcpp::ok()) {
      if (!ready_ && !setUp()) {
        waitUntil(Clock::now() + kRetryEvery);
        continue;
      }
      const auto t = Clock::now();
      Sample s;
      switch (mpu_.poll(s)) {
        case ReadStatus::NewSample:
          if (t >= settle_until_) {
            onSample(s, t);
          }
          // Nothing new can arrive for most of a period, so leave the bus idle.
          waitUntil(t + period_ * 6 / 10);
          break;
        case ReadStatus::NoNewSample:
          waitUntil(t + std::chrono::milliseconds(1));
          break;
        case ReadStatus::Error:
          errors_ = mpu_.errorCount();
          waitUntil(t + std::chrono::milliseconds(10));
          break;
      }
      if (Clock::now() - last_sample_time_ > kNoDataTimeout) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "no data for 1 s (%s): setting the MPU6050 up again",
          mpu_.lastError().empty() ? "no new samples" : mpu_.lastError().c_str());
        ready_ = false;
      }
    }
  }

  // Opens the bus and configures the chip; on failure the caller retries later.
  bool setUp()
  {
    if (!mpu_.open(bus_, address_) || !mpu_.configure(config_)) {
      errors_ = mpu_.errorCount();
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "MPU6050 on i2c-%d: %s (retrying every second)",
        bus_, mpu_.lastError().c_str());
      return false;
    }
    if (!mpu_.whoAmIOk() && !warned_who_am_i_) {
      RCLCPP_WARN(get_logger(), "WHO_AM_I is 0x%02X, expected 0x%02X (a clone?); continuing",
        mpu_.whoAmI(), kWhoAmIValue);
      warned_who_am_i_ = true;
    }
    period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / mpu_.sampleRateHz()));
    const auto t = Clock::now();
    settle_until_ = t + kSettle;
    last_sample_time_ = t;
    have_sample_ = false;
    ready_ = true;
    RCLCPP_INFO(get_logger(), "MPU6050 on i2c-%d at 0x%02X: +-%d g, +-%d deg/s, %d Hz filter, %.1f Hz%s",
      bus_, address_, accelFullScaleG(config_.accel_range), gyroFullScaleDps(config_.gyro_range),
      accelBandwidthHz(config_.dlpf), mpu_.sampleRateHz(),
      calibrating_ ? "; calibrating the gyro, keep the robot still" : "");
    return true;
  }

  void onSample(const Sample & s, Clock::time_point t)
  {
    const auto stamp = now();
    if (have_sample_ && t - last_sample_time_ > period_ * 3 / 2) {
      ++missed_;
    }
    have_sample_ = true;
    last_sample_time_ = t;
    for (int i = 0; i < 3; ++i) {
      if (saturated(s.raw.accel[i]) || saturated(s.raw.gyro[i])) {
        ++clipped_;
        break;
      }
    }

    if (calibrating_) {
      calibrate(s);
      return;
    }

    auto msg = std::make_unique<sensor_msgs::msg::Imu>();
    msg->header.stamp = stamp;
    msg->header.frame_id = frame_id_;
    msg->orientation_covariance[0] = -1.0;  // no orientation estimate
    msg->angular_velocity.x = s.gyro[0];
    msg->angular_velocity.y = s.gyro[1];
    msg->angular_velocity.z = s.gyro[2];
    // Gravity stays in: standing still reads about (0, 0, +9.8).
    msg->linear_acceleration.x = s.accel[0];
    msg->linear_acceleration.y = s.accel[1];
    msg->linear_acceleration.z = s.accel[2];
    imu_pub_->publish(std::move(msg));
    ++published_;
  }

  void calibrate(const Sample & s)
  {
    if (!calibration_.add(s.gyro_uncalibrated)) {
      return;
    }
    const bool still = calibration_.still(max_calibration_stddev_);
    if (!still && ++calibration_attempt_ < kCalibrationAttempts) {
      RCLCPP_WARN(get_logger(), "the robot moved during gyro calibration; trying again");
      calibration_.reset();
      return;
    }
    const Vec3 bias = calibration_.bias();
    const Vec3 sd = calibration_.stddev();
    const Vec3 motion = calibration_.blockStddev();
    mpu_.setGyroBias(bias);
    calibrating_ = false;
    RCLCPP_INFO(get_logger(), "gyro bias (%.3f, %.3f, %.3f) deg/s, noise (%.3f, %.3f, %.3f) deg/s, "
      "motion (%.3f, %.3f, %.3f) deg/s (limit %.3f)",
      bias[0] * kRadToDeg, bias[1] * kRadToDeg, bias[2] * kRadToDeg,
      sd[0] * kRadToDeg, sd[1] * kRadToDeg, sd[2] * kRadToDeg,
      motion[0] * kRadToDeg, motion[1] * kRadToDeg, motion[2] * kRadToDeg, max_calibration_stddev_ * kRadToDeg);
    if (!still) {
      RCLCPP_WARN(get_logger(), "the robot kept moving, so the gyro bias is approximate: "
        "restart the driver with the robot still to redo it");
    }
  }

  void waitUntil(Clock::time_point until)
  {
    // Short steps so shutdown never waits long.
    while (running_) {
      const auto left = until - Clock::now();
      if (left <= Clock::duration::zero()) {
        return;
      }
      std::this_thread::sleep_for(std::min<Clock::duration>(left, std::chrono::milliseconds(50)));
    }
  }

  void logStats()
  {
    const auto t = Clock::now();
    const double dt = std::chrono::duration<double>(t - last_stats_time_).count();
    last_stats_time_ = t;
    const uint64_t published = published_, missed = missed_, clipped = clipped_, errors = errors_;
    const uint64_t new_published = published - last_published_;
    last_published_ = published;

    if (calibrating_ || new_published == 0) {
      return;  // setup, calibration and stalls report themselves
    }
    RCLCPP_INFO(get_logger(), "%.1f Hz, %lu missed samples, %lu clipped, %lu I2C errors (totals)",
      new_published / dt, static_cast<unsigned long>(missed), static_cast<unsigned long>(clipped),
      static_cast<unsigned long>(errors));
  }

  int bus_;
  uint8_t address_;
  std::string frame_id_;
  Config config_;
  double max_calibration_stddev_;  // rad/s

  // Used only by the reader thread.
  Mpu6050 mpu_;
  GyroBiasEstimator calibration_;
  int calibration_attempt_ = 0;
  bool ready_ = false;
  bool warned_who_am_i_ = false;
  bool have_sample_ = false;
  Clock::duration period_{};
  Clock::time_point settle_until_;
  Clock::time_point last_sample_time_;

  // Shared with the stats timer.
  std::atomic<bool> running_{true};
  std::atomic<bool> calibrating_{false};
  std::atomic<uint64_t> published_{0};
  std::atomic<uint64_t> missed_{0};  // gaps longer than 1.5 sample periods
  std::atomic<uint64_t> clipped_{0};  // samples with an axis at the end of its range
  std::atomic<uint64_t> errors_{0};

  // Used only by the stats timer.
  Clock::time_point last_stats_time_;
  uint64_t last_published_ = 0;

  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  std::thread reader_;  // last, so everything it uses exists before it starts
};

}  // namespace prowl_mpu6050_driver

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<prowl_mpu6050_driver::Mpu6050Node>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("mpu6050_driver"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
