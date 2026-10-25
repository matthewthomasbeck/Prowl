#include "prowl_brain/matcher_thread.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace prowl_brain
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr auto kNap = std::chrono::milliseconds(10);  // longest stop() ever waits on a sleep
constexpr double kMapImageMargin = 1.0;  // m of border around the explored part in map pictures

}  // namespace

MatcherThread::MatcherThread(SensorBuffer & sensors, const CeresScanMatcherOptions & matcher_options,
  const LocalMapConfig & map_config, double rate_hz, double map_image_period)
: sensors_(sensors),
  period_(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / rate_hz))),
  map_image_period_(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(map_image_period))),
  matcher_(matcher_options),
  map_(map_config)
{
}

MatcherThread::~MatcherThread()
{
  stop();
}

void MatcherThread::start()
{
  if (running_.exchange(true)) {
    return;
  }
  thread_ = std::thread([this] {run();});
}

void MatcherThread::stop()
{
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::optional<MatchResult> MatcherThread::latestResult() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_result_;
}

std::shared_ptr<const MapImage> MatcherThread::latestMapImage() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_map_image_;
}

void MatcherThread::setOptions(const CeresScanMatcherOptions & options)
{
  std::lock_guard<std::mutex> lock(mutex_);
  pending_options_ = options;
}

void MatcherThread::requestReset()
{
  reset_requested_ = true;
  // Clear the result now, not on thread 2's next run: otherwise thread 1's next tick
  // would take the old map's pose as a fresh match and jump straight to Tracking.
  std::lock_guard<std::mutex> lock(mutex_);
  latest_result_.reset();
  latest_map_image_.reset();
}

MatcherStats MatcherThread::stats() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

void MatcherThread::run()
{
  // "next += period_" rather than "sleep one period" keeps the beat steady: the time
  // matchOnce() took comes off the sleep by itself.
  auto next = Clock::now();
  while (running_) {
    applyPendingChanges();
    matchOnce();
    next += period_;
    const auto t = Clock::now();
    if (t > next) {
      // Fell behind. Start the beat again from now rather than catching up with a burst.
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_.overruns;
      next = t;
    }
    sleepUntil(next);
  }
}

void MatcherThread::matchOnce()
{
  const std::optional<LidarFrame> scan = sensors_.takeUnmatchedLidar();
  if (!scan) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.skipped;
    return;
  }

  MatchResult result;
  if (map_.empty()) {
    // Nothing to match against yet, so the first scan defines where we are: the origin.
    result.stamp = scan->stamp;
    result.converged = true;
  } else {
    std::vector<ImuSample> imu;
    if (predictor_.lastPose()) {
      imu = sensors_.imuBetween(predictor_.lastStamp(), scan->stamp);
    }
    const MotionGuess guess = predictor_.predict(scan->stamp, imu);
    result = matcher_.match(*scan, guess, map_);
  }

  // Thread-2-only work stays outside the lock, so thread 1 never waits on it.
  // (Later idea: only insert every Nth scan, or once the robot has moved a bit, so
  // standing still doesn't keep piling the same scan onto the map.)
  std::shared_ptr<const MapImage> image;
  if (result.converged) {
    map_.insertScan(*scan, result.pose);
    predictor_.addMatch(result);
    const auto t = Clock::now();
    if (map_image_period_ > Clock::duration::zero() && t - last_map_image_ >= map_image_period_) {
      image = std::make_shared<const MapImage>(map_.image(result.pose, result.stamp, kMapImageMargin));
      last_map_image_ = t;
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  stats_.last_solve_time = result.solve_time;
  if (!result.converged) {
    ++stats_.failures;  // keep the old result: thread 1 sees its age grow
    return;
  }
  ++stats_.matches;
  // Checked under the lock, the same one requestReset() clears the result with: a reset
  // asked for while we were matching means this pose belongs to the old map.
  if (!reset_requested_) {
    latest_result_ = result;
    if (image) {
      latest_map_image_ = std::move(image);
    }
  }
}

void MatcherThread::applyPendingChanges()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_options_) {
      matcher_.setOptions(*pending_options_);
      pending_options_.reset();
    }
  }
  if (reset_requested_.exchange(false)) {
    // requestReset() already cleared latest_result_.
    map_.reset();
    predictor_.reset();
  }
}

void MatcherThread::sleepUntil(std::chrono::steady_clock::time_point t) const
{
  while (running_) {
    const auto left = t - Clock::now();
    if (left <= Clock::duration::zero()) {
      return;
    }
    std::this_thread::sleep_for(std::min<Clock::duration>(left, kNap));
  }
}

}  // namespace prowl_brain
