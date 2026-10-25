#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "prowl_camera_driver/yuv_image.hpp"

namespace libcamera
{
class Camera;
class CameraManager;
class Request;
}  // namespace libcamera

namespace prowl_camera_driver
{

struct VideoSettings
{
  int width = 1280;
  int height = 720;
  double fps = 30.0;
  // The sensor mode to read out, e.g. 2304x1296 (the IMX708's full field of view, binned 2x2).
  // 0 lets libcamera pick, and for 1280x720 it picks 1536x864, a crop that loses a third of the view.
  int sensor_width = 0;
  int sensor_height = 0;
  bool autofocus = true;    // continuous autofocus, on cameras that have it (the Camera Module 3 does)
  bool rotate_180 = false;  // for a camera mounted upside down
};

struct StillSettings
{
  // Frames thrown away after switching to full resolution, while exposure and white balance settle.
  int skip_frames = 4;
  // After that, how many more frames to wait at most for autofocus to stop scanning.
  int max_focus_frames = 20;
  bool autofocus = true;
  bool rotate_180 = false;
};

struct Frame
{
  YuvImage image;
  int64_t timestamp_ns = 0;  // start of exposure, CLOCK_MONOTONIC
  uint32_t sequence = 0;     // counts every frame the sensor sent, so gaps show dropped frames
};

// A Raspberry Pi camera through libcamera, giving YUV 4:2:0 frames in full-range BT.601 (sYCC),
// which can go straight into a JPEG.
//
// It runs in one of two modes: video (VideoSettings, for as long as it runs) and, for a moment,
// still (the sensor's full resolution, one picture). Not thread safe: call everything from one
// thread. libcamera's own thread only hands finished frames over through a queue.
class PiCamera
{
public:
  using FrameFn = std::function<void (const Frame &)>;

  enum class Result
  {
    Frame,    // a frame arrived (fn was called, unless the frame was corrupt)
    Timeout,  // nothing within the timeout
    Failed,   // see the error; close() and open() again to recover
  };

  PiCamera();
  ~PiCamera();
  PiCamera(const PiCamera &) = delete;
  PiCamera & operator=(const PiCamera &) = delete;

  // Starts libcamera and takes the first camera for this process alone.
  bool open(std::string & error);
  bool isOpen() const {return camera_ != nullptr;}
  void close();

  // The sensor's name, e.g. "imx708_wide" for the Camera Module 3 Wide.
  std::string model() const;

  bool startVideo(const VideoSettings & settings, std::string & error);
  // What startVideo() ended up with, e.g. "1280x720 YUV420 sYCC, sensor mode 2304x1296".
  const std::string & videoMode() const {return video_mode_;}

  // Waits up to `timeout` for the next video frame and calls `fn` with it. The pixels are only
  // valid during the call. If frames piled up meanwhile, only the newest is used; the rest count
  // as dropped, as do frames the camera threw away because we held on to every buffer.
  Result nextVideoFrame(std::chrono::milliseconds timeout, const FrameFn & fn, std::string & error);
  uint64_t dropped() const {return dropped_;}

  // Stops the video, takes one picture at the sensor's full resolution, calls `fn` with it, then
  // restarts the video as it was. The video pauses for about a second. Returns whether the picture
  // was taken; if the video couldn't restart, `error` says so even when it returns true.
  bool captureStill(const StillSettings & settings, const FrameFn & fn, std::string & error);

private:
  struct Session;  // one configured mode: its buffers, their memory mappings and requests

  bool startSession(std::unique_ptr<Session> session, bool autofocus, double fps, std::string & error);
  void stopSession();
  libcamera::Request * waitForRequest(std::chrono::milliseconds timeout);
  bool requeue(libcamera::Request * request, std::string & error);
  void deliver(libcamera::Request * request, const FrameFn & fn);
  void onRequestCompleted(libcamera::Request * request);

  std::unique_ptr<libcamera::CameraManager> manager_;
  std::shared_ptr<libcamera::Camera> camera_;
  std::unique_ptr<Session> session_;
  VideoSettings video_;
  bool video_running_ = false;
  std::string video_mode_;
  std::atomic<uint64_t> dropped_{0};
  uint32_t last_sequence_ = 0;
  bool have_sequence_ = false;

  // Finished requests, handed over from libcamera's thread.
  std::mutex mutex_;
  std::condition_variable completed_cv_;
  std::deque<libcamera::Request *> completed_;
};

}  // namespace prowl_camera_driver
