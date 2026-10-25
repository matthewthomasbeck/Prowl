#include "prowl_camera_driver/pi_camera.hpp"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include <libcamera/libcamera.h>

#include "prowl_camera_driver/timing.hpp"

namespace prowl_camera_driver
{

using libcamera::Request;

namespace
{

std::string errnoText(int err)
{
  return std::strerror(err < 0 ? -err : err);
}

// Tells the kernel the CPU is about to read (or has finished reading) a buffer the ISP wrote, so
// any CPU cache lines covering it are dealt with. A no-op where the memory isn't cached.
void syncForRead(const std::vector<int> & fds, bool start)
{
  dma_buf_sync sync{};
  sync.flags = DMA_BUF_SYNC_READ | (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END);
  for (int fd : fds) {
    ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
  }
}

}  // namespace

// Everything that belongs to one configured mode. Built by startVideo() or captureStill(), and
// torn down by stopSession() before the next one.
struct PiCamera::Session
{
  struct Mapping
  {
    void * address = MAP_FAILED;
    size_t length = 0;
  };

  // Where one buffer's three planes are in our address space.
  struct View
  {
    const uint8_t * planes[3] = {nullptr, nullptr, nullptr};
    std::vector<int> fds;  // the dma-bufs it lives in, for syncForRead()
  };

  std::unique_ptr<libcamera::CameraConfiguration> config;
  libcamera::Stream * stream = nullptr;  // the YUV stream; a raw stream, if any, is libcamera's business
  std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
  std::map<int, Mapping> mappings;  // by dma-buf fd: the planes of a buffer often share one
  std::map<const libcamera::FrameBuffer *, View> views;
  std::vector<std::unique_ptr<Request>> requests;

  ~Session()
  {
    requests.clear();
    for (auto & entry : mappings) {
      if (entry.second.address != MAP_FAILED) {
        munmap(entry.second.address, entry.second.length);
      }
    }
    allocator.reset();  // frees the buffers
  }

  // Maps every buffer of `stream` for reading and works out where its planes are.
  bool mapBuffers(std::string & error)
  {
    const libcamera::StreamConfiguration & cfg = stream->configuration();
    const int y_stride = static_cast<int>(cfg.stride);
    const int uv_stride = y_stride / 2;
    const size_t y_size = static_cast<size_t>(y_stride) * cfg.size.height;
    const size_t uv_size = static_cast<size_t>(uv_stride) * chromaHeight(static_cast<int>(cfg.size.height));

    for (const auto & buffer : allocator->buffers(stream)) {
      View view;
      const auto & planes = buffer->planes();
      for (size_t i = 0; i < planes.size() && i < 3; ++i) {
        const int fd = planes[i].fd.get();
        auto it = mappings.find(fd);
        if (it == mappings.end()) {
          Mapping m;
          m.length = static_cast<size_t>(lseek(fd, 0, SEEK_END));
          m.address = mmap(nullptr, m.length, PROT_READ, MAP_SHARED, fd, 0);
          if (m.address == MAP_FAILED) {
            error = "couldn't map a camera buffer: " + errnoText(errno);
            return false;
          }
          it = mappings.emplace(fd, m).first;
          view.fds.push_back(fd);
        } else if (std::find(view.fds.begin(), view.fds.end(), fd) == view.fds.end()) {
          view.fds.push_back(fd);
        }
        view.planes[i] = static_cast<const uint8_t *>(it->second.address) + planes[i].offset;
      }
      if (planes.size() == 1) {
        // All three planes in one, back to back.
        view.planes[1] = view.planes[0] + y_size;
        view.planes[2] = view.planes[1] + uv_size;
      } else if (planes.size() != 3) {
        error = "camera buffer has " + std::to_string(planes.size()) + " planes; expected 1 or 3";
        return false;
      }
      views[buffer.get()] = view;
    }
    return true;
  }
};

PiCamera::PiCamera() = default;

PiCamera::~PiCamera()
{
  close();
}

bool PiCamera::open(std::string & error)
{
  if (camera_) {
    return true;
  }
  manager_ = std::make_unique<libcamera::CameraManager>();
  if (const int ret = manager_->start()) {
    manager_.reset();
    error = "couldn't start libcamera: " + errnoText(ret);
    return false;
  }
  const auto cameras = manager_->cameras();
  if (cameras.empty()) {
    manager_.reset();
    error = "no camera found: check the ribbon cable (contacts facing the right way at both ends) "
      "and camera_auto_detect=1 in /boot/firmware/config.txt; inside Docker, recreate the container "
      "so it gets /dev/media* and /dev/video*";
    return false;
  }
  camera_ = cameras.front();
  if (camera_->acquire() != 0) {
    camera_.reset();
    manager_.reset();
    error = "the camera is in use by another program (an rpicam app, or a second camera node)";
    return false;
  }
  camera_->requestCompleted.connect(this, &PiCamera::onRequestCompleted);
  return true;
}

void PiCamera::close()
{
  stopSession();
  video_running_ = false;
  if (camera_) {
    camera_->requestCompleted.disconnect(this);
    camera_->release();
    camera_.reset();
  }
  manager_.reset();
}

std::string PiCamera::model() const
{
  if (!camera_) {
    return "";
  }
  const auto model = camera_->properties().get(libcamera::properties::Model);
  return model ? *model : camera_->id();
}

bool PiCamera::startVideo(const VideoSettings & settings, std::string & error)
{
  if (!camera_) {
    error = "camera not open";
    return false;
  }
  stopSession();
  video_running_ = false;

  const bool pick_mode = settings.sensor_width > 0 && settings.sensor_height > 0;
  std::vector<libcamera::StreamRole> roles{libcamera::StreamRole::VideoRecording};
  if (pick_mode) {
    // libcamera picks the sensor mode from the size of the raw stream, when there is one. We never
    // give it buffers, so the pipeline uses its own.
    roles.push_back(libcamera::StreamRole::Raw);
  }
  auto session = std::make_unique<Session>();
  session->config = camera_->generateConfiguration(roles);
  if (!session->config) {
    error = "the camera can't do video";
    return false;
  }
  libcamera::StreamConfiguration & video = session->config->at(0);
  video.pixelFormat = libcamera::formats::YUV420;
  video.size = libcamera::Size(settings.width, settings.height);
  video.colorSpace = libcamera::ColorSpace::Sycc;
  video.bufferCount = 4;
  if (pick_mode) {
    session->config->at(1).size = libcamera::Size(settings.sensor_width, settings.sensor_height);
  }
  if (settings.rotate_180) {
    session->config->orientation = libcamera::Orientation::Rotate180;
  }
  if (session->config->validate() == libcamera::CameraConfiguration::Invalid) {
    error = "the camera can't do " + video.toString();
    return false;
  }
  if (video.pixelFormat != libcamera::formats::YUV420) {
    error = "the camera can't give YUV420 video (it offered " + video.pixelFormat.toString() + ")";
    return false;
  }

  video_mode_ = video.size.toString() + " " + video.pixelFormat.toString() + " " +
    (video.colorSpace ? video.colorSpace->toString() : std::string("unknown colour space")) +
    (pick_mode ? ", sensor mode " + session->config->at(1).size.toString() : ", sensor mode chosen by libcamera");
  if (settings.rotate_180) {
    video_mode_ += ", rotated 180";
  }
  if (!startSession(std::move(session), settings.autofocus, settings.fps, error)) {
    return false;
  }
  video_ = settings;
  video_running_ = true;
  return true;
}

bool PiCamera::startSession(std::unique_ptr<Session> session, bool autofocus, double fps, std::string & error)
{
  if (const int ret = camera_->configure(session->config.get())) {
    error = "couldn't configure the camera: " + errnoText(ret);
    return false;
  }
  session->stream = session->config->at(0).stream();
  session->allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
  if (session->allocator->allocate(session->stream) < 0) {
    error = "couldn't allocate camera buffers (out of CMA memory?)";
    return false;
  }
  if (!session->mapBuffers(error)) {
    return false;
  }
  for (const auto & buffer : session->allocator->buffers(session->stream)) {
    std::unique_ptr<Request> request = camera_->createRequest();
    if (!request || request->addBuffer(session->stream, buffer.get()) != 0) {
      error = "couldn't set up a camera request";
      return false;
    }
    session->requests.push_back(std::move(request));
  }

  libcamera::ControlList controls;
  if (fps > 0.0) {
    const int64_t frame_us = frameDurationUs(fps);
    const std::array<int64_t, 2> limits{frame_us, frame_us};
    controls.set(libcamera::controls::FrameDurationLimits, libcamera::Span<const int64_t, 2>(limits));
  }
  if (autofocus && camera_->controls().count(&libcamera::controls::AfMode)) {
    controls.set(libcamera::controls::AfMode, libcamera::controls::AfModeContinuous);
  }
  if (const int ret = camera_->start(&controls)) {
    error = "couldn't start the camera: " + errnoText(ret);
    return false;
  }
  session_ = std::move(session);
  have_sequence_ = false;  // a new mode counts from 0 again
  for (const auto & request : session_->requests) {
    if (const int ret = camera_->queueRequest(request.get())) {
      error = "couldn't queue a camera request: " + errnoText(ret);
      stopSession();
      return false;
    }
  }
  return true;
}

void PiCamera::stopSession()
{
  if (!session_) {
    return;
  }
  camera_->stop();  // finishes every queued request as cancelled
  {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_.clear();
  }
  session_.reset();
}

void PiCamera::onRequestCompleted(Request * request)
{
  // Runs on libcamera's thread. Cancelled requests only come from stop().
  if (request->status() == Request::RequestCancelled) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_.push_back(request);
  }
  completed_cv_.notify_one();
}

Request * PiCamera::waitForRequest(std::chrono::milliseconds timeout)
{
  std::deque<Request *> ready;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!completed_cv_.wait_for(lock, timeout, [this] {return !completed_.empty();})) {
      return nullptr;
    }
    ready.swap(completed_);
  }
  // Keep the newest; the older ones go straight back to the camera.
  Request * newest = ready.back();
  ready.pop_back();
  for (Request * old : ready) {
    ++dropped_;
    std::string ignored;
    requeue(old, ignored);
  }
  return newest;
}

bool PiCamera::requeue(Request * request, std::string & error)
{
  request->reuse(Request::ReuseBuffers);
  if (const int ret = camera_->queueRequest(request)) {
    error = "couldn't queue a camera request: " + errnoText(ret);
    return false;
  }
  return true;
}

void PiCamera::deliver(Request * request, const FrameFn & fn)
{
  libcamera::FrameBuffer * buffer = request->findBuffer(session_->stream);
  if (!buffer || buffer->metadata().status != libcamera::FrameMetadata::FrameSuccess) {
    ++dropped_;
    return;
  }
  const Session::View & view = session_->views.at(buffer);
  const libcamera::StreamConfiguration & cfg = session_->stream->configuration();

  Frame frame;
  frame.image.y = view.planes[0];
  frame.image.u = view.planes[1];
  frame.image.v = view.planes[2];
  frame.image.y_stride = static_cast<int>(cfg.stride);
  frame.image.uv_stride = static_cast<int>(cfg.stride) / 2;
  frame.image.width = static_cast<int>(cfg.size.width);
  frame.image.height = static_cast<int>(cfg.size.height);
  const auto sensor_time = request->metadata().get(libcamera::controls::SensorTimestamp);
  frame.timestamp_ns = sensor_time ? *sensor_time : static_cast<int64_t>(buffer->metadata().timestamp);
  frame.sequence = buffer->metadata().sequence;
  // A gap means the sensor sent frames while every buffer was still with us, so the camera had to
  // throw them away.
  if (have_sequence_ && frame.sequence > last_sequence_ + 1) {
    dropped_ += frame.sequence - last_sequence_ - 1;
  }
  last_sequence_ = frame.sequence;
  have_sequence_ = true;

  syncForRead(view.fds, true);
  fn(frame);
  syncForRead(view.fds, false);
}

PiCamera::Result PiCamera::nextVideoFrame(
  std::chrono::milliseconds timeout, const FrameFn & fn, std::string & error)
{
  if (!session_ || !video_running_) {
    error = "video isn't running";
    return Result::Failed;
  }
  Request * request = waitForRequest(timeout);
  if (!request) {
    return Result::Timeout;
  }
  deliver(request, fn);
  return requeue(request, error) ? Result::Frame : Result::Failed;
}

bool PiCamera::captureStill(const StillSettings & settings, const FrameFn & fn, std::string & error)
{
  if (!camera_) {
    error = "camera not open";
    return false;
  }
  const bool resume_video = video_running_;
  stopSession();
  video_running_ = false;

  bool ok = false;
  auto session = std::make_unique<Session>();
  const std::vector<libcamera::StreamRole> roles{libcamera::StreamRole::StillCapture};
  session->config = camera_->generateConfiguration(roles);
  if (!session->config) {
    error = "the camera can't take stills";
  } else {
    libcamera::StreamConfiguration & still = session->config->at(0);
    still.pixelFormat = libcamera::formats::YUV420;
    if (const auto full = camera_->properties().get(libcamera::properties::PixelArraySize)) {
      still.size = *full;
    }
    still.colorSpace = libcamera::ColorSpace::Sycc;
    still.bufferCount = 2;
    if (settings.rotate_180) {
      session->config->orientation = libcamera::Orientation::Rotate180;
    }
    if (session->config->validate() == libcamera::CameraConfiguration::Invalid ||
      still.pixelFormat != libcamera::formats::YUV420)
    {
      error = "the camera can't do " + still.toString();
    } else if (startSession(std::move(session), settings.autofocus, 0.0, error)) {
      const bool has_af = settings.autofocus && camera_->controls().count(&libcamera::controls::AfMode);
      for (int seen = 1;; ++seen) {
        Request * request = waitForRequest(std::chrono::seconds(2));
        if (!request) {
          error = "no frame within 2 s at full resolution";
          break;
        }
        bool focused = true;
        if (has_af) {
          const auto state = request->metadata().get(libcamera::controls::AfState);
          focused = !state || *state != libcamera::controls::AfStateScanning;
        }
        if (seen > settings.skip_frames && (focused || seen > settings.skip_frames + settings.max_focus_frames)) {
          deliver(request, fn);
          ok = true;
          break;
        }
        if (!requeue(request, error)) {
          break;
        }
      }
    }
  }
  stopSession();

  // A failed restart doesn't undo the picture: report it in `error`, and nextVideoFrame() fails.
  std::string video_error;
  if (resume_video && !startVideo(video_, video_error)) {
    error = (ok ? std::string() : error + "; ") + "couldn't restart the video: " + video_error;
  }
  return ok;
}

}  // namespace prowl_camera_driver
