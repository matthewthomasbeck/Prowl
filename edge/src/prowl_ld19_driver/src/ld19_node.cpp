#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>

#include "prowl_ld19_driver/laser_scan.hpp"
#include "prowl_ld19_driver/packet_parser.hpp"
#include "prowl_ld19_driver/protocol.hpp"
#include "prowl_ld19_driver/scan_assembler.hpp"
#include "prowl_ld19_driver/serial_port.hpp"

namespace prowl_ld19_driver
{

namespace
{

constexpr int kReadTimeoutMs = 20;
constexpr auto kReopenEvery = std::chrono::seconds(1);

}  // namespace

// Reads the LD19 on a background thread and publishes each full rotation on
// /scan. The port is reopened every second while it is missing or failing, so
// the node can start before the LiDAR is plugged in.
class Ld19Node : public rclcpp::Node
{
public:
  Ld19Node()
  : Node("ld19_driver")
  {
    port_name_ = declare_parameter("port", std::string("/dev/ttyS0"));
    frame_id_ = declare_parameter("frame_id", std::string("laser_link"));
    config_.range_min = declare_parameter("range_min", 0.02);
    config_.range_max = declare_parameter("range_max", 12.0);
    const auto bins = declare_parameter<int64_t>("bins", 720);
    const auto min_intensity = declare_parameter<int64_t>("min_intensity", 0);
    const double stats_period = declare_parameter("stats_period", 5.0);

    if (bins < 1) {
      throw std::runtime_error("bins must be at least 1");
    }
    if (config_.range_min < 0.0 || config_.range_max <= config_.range_min) {
      throw std::runtime_error("need 0 <= range_min < range_max");
    }
    if (min_intensity < 0 || min_intensity > 255) {
      throw std::runtime_error("min_intensity must be 0-255");
    }
    config_.bins = static_cast<size_t>(bins);
    config_.min_intensity = static_cast<uint8_t>(min_intensity);

    scan_pub_ = create_publisher<sensor_msgs::msg::LaserScan>("scan", 10);
    if (stats_period > 0.0) {
      stats_timer_ = create_wall_timer(
        std::chrono::duration<double>(stats_period), [this] {logStats();});
    }
    last_stats_time_ = std::chrono::steady_clock::now();
    reader_ = std::thread([this] {readLoop();});
  }

  ~Ld19Node() override
  {
    running_ = false;
    if (reader_.joinable()) {
      reader_.join();
    }
  }

private:
  void readLoop()
  {
    std::vector<Packet> packets;
    uint8_t buf[4096];
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
      packets.clear();
      parser_.feed(buf, static_cast<size_t>(n), packets);
      for (const Packet & p : packets) {
        if (assembler_.add(p)) {
          publishScan(assembler_.scan());
        }
      }
      packets_ = parser_.stats().packets;
      crc_errors_ = parser_.stats().crc_errors;
    }
  }

  bool openPort()
  {
    std::string error;
    if (!port_.open(port_name_, error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "%s (retrying every second)", error.c_str());
      return false;
    }
    RCLCPP_INFO(get_logger(), "reading the LD19 on %s", port_name_.c_str());
    assembler_.reset();  // a rotation interrupted by the reopen is incomplete
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

  void publishScan(const Scan & scan)
  {
    // The rotation ended about now; it started one rotation's worth of device time earlier.
    const rclcpp::Time end = now();
    const auto span = std::chrono::milliseconds(scan.end_time_ms - scan.start_time_ms);

    auto msg = std::make_unique<sensor_msgs::msg::LaserScan>();
    msg->header.stamp = end - rclcpp::Duration(span);
    msg->header.frame_id = frame_id_;
    fillLaserScan(scan, config_, *msg);
    scan_pub_->publish(std::move(msg));
    ++rotations_;
  }

  // Rates over the last period. A healthy LD19 gives ~10 rotations and ~416 packets a second.
  void logStats()
  {
    const auto t = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t - last_stats_time_).count();
    last_stats_time_ = t;
    const uint64_t packets = packets_, crc_errors = crc_errors_, rotations = rotations_;
    const uint64_t new_packets = packets - last_packets_;
    const uint64_t new_crc_errors = crc_errors - last_crc_errors_;
    const uint64_t new_rotations = rotations - last_rotations_;
    last_packets_ = packets;
    last_crc_errors_ = crc_errors;
    last_rotations_ = rotations;

    if (!port_open_) {
      return;  // the open failure is already being reported
    }
    if (new_packets == 0) {
      RCLCPP_WARN(get_logger(), "no data from %s for %.0f s: check the LD19's power and its Tx wire "
        "(to GPIO15, header pin 10)", port_name_.c_str(), dt);
      return;
    }
    RCLCPP_INFO(get_logger(), "%.1f rotations/s, %.0f packets/s, %lu CRC errors (%lu total)",
      new_rotations / dt, new_packets / dt, static_cast<unsigned long>(new_crc_errors),
      static_cast<unsigned long>(crc_errors));
  }

  std::string port_name_;
  std::string frame_id_;
  LaserScanConfig config_;

  // Used only by the reader thread.
  SerialPort port_;
  PacketParser parser_;
  ScanAssembler assembler_;

  // Shared with the stats timer.
  std::atomic<bool> running_{true};
  std::atomic<bool> port_open_{false};
  std::atomic<uint64_t> packets_{0};
  std::atomic<uint64_t> crc_errors_{0};
  std::atomic<uint64_t> rotations_{0};

  // Used only by the stats timer.
  std::chrono::steady_clock::time_point last_stats_time_;
  uint64_t last_packets_ = 0;
  uint64_t last_crc_errors_ = 0;
  uint64_t last_rotations_ = 0;

  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr scan_pub_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  std::thread reader_;  // last, so everything it uses exists before it starts
};

}  // namespace prowl_ld19_driver

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<prowl_ld19_driver::Ld19Node>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("ld19_driver"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
