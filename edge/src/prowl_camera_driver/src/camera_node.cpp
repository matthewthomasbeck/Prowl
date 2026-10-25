#include <stdlib.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_srvs/srv/trigger.hpp>

#include "prowl_camera_driver/jpeg.hpp"
#include "prowl_camera_driver/pi_camera.hpp"
#include "prowl_camera_driver/timing.hpp"

namespace prowl_camera_driver
{

namespace
{

using sensor_msgs::msg::CompressedImage;
using sensor_msgs::msg::Image;
using std_srvs::srv::Trigger;

constexpr auto kFrameTimeout = std::chrono::milliseconds(500);
constexpr auto kStallReopen = std::chrono::seconds(5);  // no frames this long: reopen the camera
constexpr auto kReopenEvery = std::chrono::seconds(1);

int64_t clockNs(clockid_t clock)
{
  timespec ts{};
  clock_gettime(clock, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

}  // namespace

// Runs the camera on a background thread and publishes:
//   camera/image_raw/compressed  JPEG video, every frame
//   camera/image_raw             the same frames as rgb8, only while something subscribes
//   camera/still/compressed      a full-resolution JPEG each time camera/capture is called
// The camera is reopened every second while it is missing or failing, so the node can start
// before the camera works.
class CameraNode : public rclcpp::Node
{
public:
  CameraNode()
  : Node("camera_driver")
  {
    frame_id_ = declare_parameter("frame_id", std::string("camera_link"));
    video_.width = static_cast<int>(declare_parameter<int64_t>("width", 1280));
    video_.height = static_cast<int>(declare_parameter<int64_t>("height", 720));
    video_.fps = declare_parameter("fps", 30.0);
    video_.sensor_width = static_cast<int>(declare_parameter<int64_t>("sensor_width", 2304));
    video_.sensor_height = static_cast<int>(declare_parameter<int64_t>("sensor_height", 1296));
    video_.autofocus = declare_parameter("autofocus", true);
    video_.rotate_180 = declare_parameter("rotate_180", false);
    jpeg_quality_ = static_cast<int>(declare_parameter<int64_t>("jpeg_quality", 80));
    still_quality_ = static_cast<int>(declare_parameter<int64_t>("still_jpeg_quality", 92));
    still_.skip_frames = static_cast<int>(declare_parameter<int64_t>("still_skip_frames", 4));
    const double stats_period = declare_parameter("stats_period", 5.0);
    const std::string log_level = declare_parameter("libcamera_log_level", std::string("ERROR"));

    if (video_.width < 16 || video_.height < 16) {
      throw std::runtime_error("width and height must be at least 16");
    }
    if (!(video_.fps > 0.0 && video_.fps <= 120.0)) {
      throw std::runtime_error("fps must be in (0, 120]");
    }
    if ((video_.sensor_width > 0) != (video_.sensor_height > 0)) {
      throw std::runtime_error("set both sensor_width and sensor_height, or neither (0 = let libcamera pick)");
    }
    if (jpeg_quality_ < 1 || jpeg_quality_ > 100 || still_quality_ < 1 || still_quality_ > 100) {
      throw std::runtime_error("jpeg_quality and still_jpeg_quality must be 1-100");
    }
    if (still_.skip_frames < 0) {
      throw std::runtime_error("still_skip_frames must be >= 0");
    }
    still_.autofocus = video_.autofocus;
    still_.rotate_180 = video_.rotate_180;

    // libcamera logs every configuration at INFO, straight to stderr. Its logger reads this when the
    // camera thread first uses libcamera; a LIBCAMERA_LOG_LEVELS already in the environment wins.
    setenv("LIBCAMERA_LOG_LEVELS", ("*:" + log_level).c_str(), 0);

    jpeg_pub_ = create_publisher<CompressedImage>("camera/image_raw/compressed", rclcpp::SensorDataQoS());
    raw_pub_ = create_publisher<Image>("camera/image_raw", rclcpp::SensorDataQoS());
    // Kept for late subscribers, such as a dashboard bridge started after the picture was taken.
    still_pub_ = create_publisher<CompressedImage>(
      "camera/still/compressed", rclcpp::QoS(1).reliable().transient_local());
    // Answered from the camera thread once the picture is out, so a capture never blocks the
    // executor. Callers that ask while a capture is pending all get the next picture.
    capture_srv_ = create_service<Trigger>(
      "camera/capture",
      [this](const std::shared_ptr<rmw_request_id_t> header, const std::shared_ptr<Trigger::Request>) {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        capture_waiting_.push_back(header);
      });

    if (stats_period > 0.0) {
      stats_timer_ = create_wall_timer(
        std::chrono::duration<double>(stats_period), [this] {logStats();});
    }
    last_stats_time_ = std::chrono::steady_clock::now();
    worker_ = std::thread([this] {run();});
  }

  ~CameraNode() override
  {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

private:
  void run()
  {
    Jpeg jpeg;
    auto last_frame = std::chrono::steady_clock::now();
    while (running_ && rclcpp::ok()) {
      if (!camera_.isOpen()) {
        if (!openCamera()) {
          answerCaptures(false, "the camera isn't working: " + last_error_);
          waitToReopen();
          continue;
        }
        last_frame = std::chrono::steady_clock::now();
      }
      serveCaptures(jpeg);

      std::string error;
      const auto result = camera_.nextVideoFrame(
        kFrameTimeout, [&](const Frame & frame) {publishVideo(jpeg, frame);}, error);
      if (result == PiCamera::Result::Frame) {
        last_frame = std::chrono::steady_clock::now();
      } else if (result == PiCamera::Result::Failed) {
        RCLCPP_ERROR(get_logger(), "%s; reopening the camera", error.c_str());
        closeCamera();
        waitToReopen();
      } else if (std::chrono::steady_clock::now() - last_frame > kStallReopen) {
        RCLCPP_ERROR(get_logger(), "no frames from the camera for %ld s; reopening it",
          static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(kStallReopen).count()));
        closeCamera();
      }
    }
    answerCaptures(false, "the camera driver is shutting down");
    closeCamera();
  }

  bool openCamera()
  {
    std::string error;
    if (!camera_.open(error) || !camera_.startVideo(video_, error)) {
      camera_.close();
      last_error_ = error;
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "%s (retrying every second)", error.c_str());
      return false;
    }
    RCLCPP_INFO(get_logger(), "%s: video %s at %.1f fps", camera_.model().c_str(),
      camera_.videoMode().c_str(), video_.fps);
    camera_open_ = true;
    return true;
  }

  void closeCamera()
  {
    camera_.close();
    camera_open_ = false;
  }

  void waitToReopen()
  {
    const auto until = std::chrono::steady_clock::now() + kReopenEvery;
    while (running_ && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  builtin_interfaces::msg::Time stampOf(const Frame & frame) const
  {
    return rclcpp::Time(
      monotonicToSystemNs(frame.timestamp_ns, clockNs(CLOCK_MONOTONIC), clockNs(CLOCK_REALTIME)),
      RCL_SYSTEM_TIME);
  }

  void publishVideo(Jpeg & jpeg, const Frame & frame)
  {
    const auto stamp = stampOf(frame);
    std::string error;
    const auto start = std::chrono::steady_clock::now();

    auto msg = std::make_unique<CompressedImage>();
    msg->header.stamp = stamp;
    msg->header.frame_id = frame_id_;
    msg->format = "jpeg";
    if (!jpeg.encode(frame.image, jpeg_quality_, msg->data, error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "couldn't encode a frame: %s", error.c_str());
      return;
    }
    encode_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
    jpeg_bytes_ += msg->data.size();
    jpeg_pub_->publish(std::move(msg));
    ++frames_;

    // Converting to RGB costs about as much as the JPEG, so only do it for someone.
    if (raw_pub_->get_subscription_count() > 0) {
      auto raw = std::make_unique<Image>();
      raw->header.stamp = stamp;
      raw->header.frame_id = frame_id_;
      raw->width = static_cast<uint32_t>(frame.image.width);
      raw->height = static_cast<uint32_t>(frame.image.height);
      raw->encoding = "rgb8";
      raw->step = raw->width * 3;
      if (jpeg.toRgb(frame.image, raw->data, error)) {
        raw_pub_->publish(std::move(raw));
      } else {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "couldn't convert a frame to RGB: %s", error.c_str());
      }
    }
  }

  void serveCaptures(Jpeg & jpeg)
  {
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);
      if (capture_waiting_.empty()) {
        return;
      }
    }
    const auto start = std::chrono::steady_clock::now();
    std::string error, encode_error, summary;
    bool published = false;
    const bool taken = camera_.captureStill(
      still_, [&](const Frame & frame) {
        auto msg = std::make_unique<CompressedImage>();
        msg->header.stamp = stampOf(frame);
        msg->header.frame_id = frame_id_;
        msg->format = "jpeg";
        if (!jpeg.encode(frame.image, still_quality_, msg->data, encode_error)) {
          return;
        }
        summary = std::to_string(frame.image.width) + "x" + std::to_string(frame.image.height) + ", " +
          std::to_string(msg->data.size() / 1024) + " KB";
        still_pub_->publish(std::move(msg));
        published = true;
      }, error);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (published) {
      ++stills_;
      RCLCPP_INFO(get_logger(), "still: %s, %.1f s with the video paused", summary.c_str(), seconds);
      answerCaptures(true, summary + " on " + std::string(still_pub_->get_topic_name()));
    } else {
      const std::string why = !encode_error.empty() ? "couldn't encode the still: " + encode_error : error;
      RCLCPP_ERROR(get_logger(), "still capture failed: %s", why.c_str());
      answerCaptures(false, why);
    }
    if (taken && !error.empty()) {
      // The picture is out, but the video didn't come back; the next frame wait reopens the camera.
      RCLCPP_ERROR(get_logger(), "%s", error.c_str());
    }
  }

  void answerCaptures(bool success, const std::string & message)
  {
    std::vector<std::shared_ptr<rmw_request_id_t>> waiting;
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);
      waiting.swap(capture_waiting_);
    }
    Trigger::Response response;
    response.success = success;
    response.message = message;
    for (const auto & header : waiting) {
      capture_srv_->send_response(*header, response);
    }
  }

  // Rates over the last period. Encoding a 1280x720 frame takes about 10 ms on a Pi 4.
  void logStats()
  {
    const auto t = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t - last_stats_time_).count();
    last_stats_time_ = t;
    const uint64_t frames = frames_, bytes = jpeg_bytes_, encode_ns = encode_ns_, dropped = camera_.dropped();
    const uint64_t new_frames = frames - last_frames_;
    const uint64_t new_bytes = bytes - last_bytes_;
    const uint64_t new_encode_ns = encode_ns - last_encode_ns_;
    const uint64_t new_dropped = dropped - last_dropped_;
    last_frames_ = frames;
    last_bytes_ = bytes;
    last_encode_ns_ = encode_ns;
    last_dropped_ = dropped;

    if (!camera_open_ || new_frames == 0) {
      return;  // the open failure or stall is already being reported
    }
    RCLCPP_INFO(get_logger(), "%.1f fps, %.0f KB per JPEG, %.1f ms to encode, %lu frames dropped, %lu stills",
      new_frames / dt, new_bytes / 1024.0 / new_frames, new_encode_ns / 1e6 / new_frames,
      static_cast<unsigned long>(new_dropped), static_cast<unsigned long>(stills_.load()));
  }

  std::string frame_id_;
  VideoSettings video_;
  StillSettings still_;
  int jpeg_quality_ = 80;
  int still_quality_ = 92;

  // Used only by the camera thread.
  PiCamera camera_;
  std::string last_error_;

  // Capture requests waiting for the camera thread.
  std::mutex capture_mutex_;
  std::vector<std::shared_ptr<rmw_request_id_t>> capture_waiting_;

  // Shared with the stats timer.
  std::atomic<bool> running_{true};
  std::atomic<bool> camera_open_{false};
  std::atomic<uint64_t> frames_{0};
  std::atomic<uint64_t> jpeg_bytes_{0};
  std::atomic<uint64_t> encode_ns_{0};
  std::atomic<uint64_t> stills_{0};

  // Used only by the stats timer.
  std::chrono::steady_clock::time_point last_stats_time_;
  uint64_t last_frames_ = 0;
  uint64_t last_bytes_ = 0;
  uint64_t last_encode_ns_ = 0;
  uint64_t last_dropped_ = 0;

  rclcpp::Publisher<CompressedImage>::SharedPtr jpeg_pub_;
  rclcpp::Publisher<Image>::SharedPtr raw_pub_;
  rclcpp::Publisher<CompressedImage>::SharedPtr still_pub_;
  rclcpp::Service<Trigger>::SharedPtr capture_srv_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  std::thread worker_;  // last, so everything it uses exists before it starts
};

}  // namespace prowl_camera_driver

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<prowl_camera_driver::CameraNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("camera_driver"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
