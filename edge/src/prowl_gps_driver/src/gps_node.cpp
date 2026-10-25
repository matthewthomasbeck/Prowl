#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "prowl_gps_driver/navsat.hpp"
#include "prowl_gps_driver/nmea.hpp"
#include "prowl_gps_driver/receiver_state.hpp"
#include "prowl_gps_driver/serial_port.hpp"

namespace prowl_gps_driver
{

namespace
{

constexpr int kReadTimeoutMs = 50;
constexpr auto kReopenEvery = std::chrono::seconds(1);

}  // namespace

// Reads the u-blox 7's NMEA sentences on a background thread. For each GGA (once a second by default)
// it publishes /gps/fix and /gps/status, and for each RMC with a valid fix, /gps/vel. The port is
// reopened every second while it is missing or failing, so the node can start before the GPS is
// plugged in. It only listens: nothing is ever sent to the receiver.
class GpsNode : public rclcpp::Node
{
public:
  GpsNode()
  : Node("gps_driver")
  {
    port_name_ = declare_parameter("port", std::string("/dev/ttyAMA4"));
    const auto baud = declare_parameter<int64_t>("baud", 9600);
    frame_id_ = declare_parameter("frame_id", std::string("gps_link"));
    uere_ = declare_parameter("uere", 2.5);
    const double stats_period = declare_parameter("stats_period", 10.0);

    if (!supportedBaud(static_cast<int>(baud))) {
      throw std::runtime_error("baud must be 4800, 9600, 19200, 38400, 57600, 115200 or 230400");
    }
    if (!(uere_ > 0.0)) {
      throw std::runtime_error("uere must be > 0");
    }
    baud_ = static_cast<int>(baud);
    hardware_id_ = "u-blox 7 on " + port_name_;

    fix_pub_ = create_publisher<sensor_msgs::msg::NavSatFix>("gps/fix", 10);
    vel_pub_ = create_publisher<geometry_msgs::msg::TwistStamped>("gps/vel", 10);
    status_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticStatus>("gps/status", 10);
    if (stats_period > 0.0) {
      stats_timer_ = create_wall_timer(
        std::chrono::duration<double>(stats_period), [this] {logStats();});
    }
    started_ = std::chrono::steady_clock::now();
    last_stats_time_ = started_;
    reader_ = std::thread([this] {readLoop();});
  }

  ~GpsNode() override
  {
    running_ = false;
    if (reader_.joinable()) {
      reader_.join();
    }
  }

private:
  void readLoop()
  {
    std::vector<std::string> bodies;
    Sentence sentence;
    uint8_t buf[512];
    while (running_ && rclcpp::ok()) {
      if (!port_.isOpen() && !openPort()) {
        waitToReopen();
        continue;
      }
      const ssize_t n = port_.read(buf, sizeof(buf), kReadTimeoutMs);
      if (n < 0) {
        RCLCPP_ERROR(get_logger(), "%s: read failed: %s", port_name_.c_str(), std::strerror(errno));
        port_.close();
        port_open_ = false;
        waitToReopen();
        continue;
      }
      bodies.clear();
      splitter_.feed(buf, static_cast<size_t>(n), bodies);
      for (const std::string & body : bodies) {
        if (!splitSentence(body, sentence)) {
          continue;
        }
        const double t = seconds();
        switch (state_.apply(sentence, t)) {
          case Update::Fix:
            publishFix(state_.fix(t));
            break;
          case Update::Velocity:
            publishVelocity(state_.fix(t));
            break;
          case Update::None:
            break;
        }
      }
      bytes_ = splitter_.stats().bytes;
      sentences_ = splitter_.stats().sentences;
      checksum_errors_ = splitter_.stats().checksum_errors;
    }
  }

  bool openPort()
  {
    std::string error;
    if (!port_.open(port_name_, baud_, error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "%s (retrying every second)", error.c_str());
      return false;
    }
    RCLCPP_INFO(get_logger(), "reading the GPS on %s at %d baud", port_name_.c_str(), baud_);
    port_open_ = true;
    return true;
  }

  void waitToReopen()
  {
    const auto until = std::chrono::steady_clock::now() + kReopenEvery;
    while (running_ && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  double seconds() const
  {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
  }

  void publishFix(const GpsFix & fix)
  {
    const rclcpp::Time stamp = now();
    auto msg = std::make_unique<sensor_msgs::msg::NavSatFix>();
    msg->header.stamp = stamp;
    msg->header.frame_id = frame_id_;
    fillNavSatFix(fix, uere_, *msg);
    fix_pub_->publish(std::move(msg));

    auto status = std::make_unique<diagnostic_msgs::msg::DiagnosticStatus>();
    fillStatus(fix, hardware_id_, *status);
    status_pub_->publish(std::move(status));

    if (fix.fix != last_fix_) {
      if (fix.fix == FixType::None) {
        RCLCPP_WARN(get_logger(), "lost the fix (%zu satellites in view)", fix.satellites.size());
      } else {
        RCLCPP_INFO(get_logger(), "%s fix from %d satellites: %.6f, %.6f", fix.fix == FixType::Fix3D ? "3D" : "2D",
          fix.satellites_used, *fix.latitude, *fix.longitude);
      }
      last_fix_ = fix.fix;
    }
    in_view_ = fix.satellites.size();
  }

  void publishVelocity(const GpsFix & fix)
  {
    auto msg = std::make_unique<geometry_msgs::msg::TwistStamped>();
    if (!fillVelocity(fix, *msg)) {
      return;
    }
    msg->header.stamp = now();
    msg->header.frame_id = frame_id_;
    vel_pub_->publish(std::move(msg));
  }

  // Rates over the last period. A healthy u-blox 7 at its defaults sends 5-10 sentences a second.
  void logStats()
  {
    const auto t = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t - last_stats_time_).count();
    last_stats_time_ = t;
    const uint64_t bytes = bytes_, sentences = sentences_, checksum_errors = checksum_errors_;
    const uint64_t new_bytes = bytes - last_bytes_;
    const uint64_t new_sentences = sentences - last_sentences_;
    const uint64_t new_checksum_errors = checksum_errors - last_checksum_errors_;
    last_bytes_ = bytes;
    last_sentences_ = sentences;
    last_checksum_errors_ = checksum_errors;

    if (!port_open_) {
      return;  // the open failure is already being reported
    }
    if (new_bytes == 0) {
      RCLCPP_WARN(get_logger(), "no data from %s for %.0f s: check the GPS's power and its TX wire "
        "(to GPIO9, header pin 21)", port_name_.c_str(), dt);
      return;
    }
    if (new_sentences == 0) {
      RCLCPP_WARN(get_logger(), "%lu bytes from %s but no NMEA sentences: is the GPS at %d baud?",
        static_cast<unsigned long>(new_bytes), port_name_.c_str(), baud_);
      return;
    }
    RCLCPP_INFO(get_logger(), "%.1f sentences/s, %lu checksum errors, %s, %lu satellites in view",
      new_sentences / dt, static_cast<unsigned long>(new_checksum_errors),
      last_fix_ == FixType::None ? "no fix" : (last_fix_ == FixType::Fix3D ? "3D fix" : "2D fix"),
      static_cast<unsigned long>(in_view_.load()));
  }

  std::string port_name_;
  int baud_ = 9600;
  std::string frame_id_;
  std::string hardware_id_;
  double uere_ = 2.5;
  std::chrono::steady_clock::time_point started_;

  // Used only by the reader thread.
  SerialPort port_;
  SentenceSplitter splitter_;
  ReceiverState state_;

  // Shared with the stats timer.
  std::atomic<bool> running_{true};
  std::atomic<bool> port_open_{false};
  std::atomic<uint64_t> bytes_{0};
  std::atomic<uint64_t> sentences_{0};
  std::atomic<uint64_t> checksum_errors_{0};
  std::atomic<size_t> in_view_{0};
  std::atomic<FixType> last_fix_{FixType::None};

  // Used only by the stats timer.
  std::chrono::steady_clock::time_point last_stats_time_;
  uint64_t last_bytes_ = 0;
  uint64_t last_sentences_ = 0;
  uint64_t last_checksum_errors_ = 0;

  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_pub_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr vel_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  std::thread reader_;  // last, so everything it uses exists before it starts
};

}  // namespace prowl_gps_driver

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<prowl_gps_driver::GpsNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("gps_driver"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
