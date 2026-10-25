#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "prowl_brain/ceres_scan_matcher.hpp"
#include "prowl_brain/local_map.hpp"
#include "prowl_brain/motion_predictor.hpp"
#include "prowl_brain/sensor_buffer.hpp"
#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// Counters for the stats log, so we can see if the matcher keeps up.
struct MatcherStats
{
  uint64_t matches = 0;    // runs that converged
  uint64_t failures = 0;   // runs that didn't (pose not updated)
  uint64_t skipped = 0;    // ticks with no new LiDAR spin to match
  uint64_t overruns = 0;   // ticks that took longer than 1 / rate (we fell behind)
  double last_solve_time = 0.0;  // s
};

// THREAD 2: the 10 Hz scan matcher.
//
// Ten times a second this wakes up, like an alarm clock, and:
//   1. takes the newest LiDAR spin out of the SensorBuffer (if there's a new one),
//   2. grabs the gyro readings since the last match,
//   3. asks MotionPredictor for a starting guess,
//   4. asks CeresScanMatcher to line the scan up with the map,
//   5. draws the lined-up scan onto the map so the map keeps growing,
//   6. leaves the answer where thread 1 can pick it up (latestResult()),
//   7. and every map_image_period, a picture of the map for the screen (latestMapImage()).
//
// Why a separate thread? Matching takes a few tens of milliseconds. If thread 1
// did it, it would be "frozen" during that time and couldn't react to anything
// (like a sensor dropping out). This way thread 1 stays quick, and the slow
// thinking happens next door.
//
// Why 10 Hz? The LD19 only finishes a new spin 10 times a second, so matching
// any faster would just match the same spin again.
//
// The matcher, map and predictor below belong to this thread only. The only
// things shared with thread 1 are the SensorBuffer (has its own lock), the
// result mailbox and the pending options (both behind mutex_), and the atomic
// flags.
class MatcherThread
{
public:
  // map_image_period: s between pictures of the map for the screen; 0 = never make them.
  MatcherThread(SensorBuffer & sensors, const CeresScanMatcherOptions & matcher_options,
    const LocalMapConfig & map_config, double rate_hz, double map_image_period = 0.0);
  ~MatcherThread();  // calls stop()

  MatcherThread(const MatcherThread &) = delete;
  MatcherThread & operator=(const MatcherThread &) = delete;

  // Start / stop the alarm-clock loop. stop() waits for the current run to finish.
  void start();
  void stop();

  // ---- Called from thread 1 ----

  // The newest successful match, or nothing yet.
  std::optional<MatchResult> latestResult() const;
  // The newest picture of the map, or null (none yet, or a reset since). Shared, not copied:
  // thread 2 never changes a picture once it's handed over, it makes a new one.
  std::shared_ptr<const MapImage> latestMapImage() const;
  // New knob settings (from `ros2 param set`); applied at the start of the next run.
  void setOptions(const CeresScanMatcherOptions & options);
  // Throw away the map and pose and start over (e.g. after SensorLost). latestResult() and
  // latestMapImage() are empty from the moment this returns; the map itself is wiped at the start of the next run.
  void requestReset();
  MatcherStats stats() const;

private:
  // The loop itself: matchOnce(), then sleep until the next tick, until stop().
  void run();
  // One run of steps 1-6 above.
  void matchOnce();
  // Before each run: pick up new options and do a requested reset.
  void applyPendingChanges();
  // Sleep until `t`, in short naps so stop() never waits long.
  void sleepUntil(std::chrono::steady_clock::time_point t) const;

  SensorBuffer & sensors_;
  std::chrono::steady_clock::duration period_;  // 1 / rate_hz
  std::chrono::steady_clock::duration map_image_period_;  // zero = no pictures

  // Thread 2 only.
  CeresScanMatcher matcher_;
  LocalMap map_;
  MotionPredictor predictor_;
  std::chrono::steady_clock::time_point last_map_image_;

  // Shared with thread 1, behind mutex_.
  mutable std::mutex mutex_;
  std::optional<MatchResult> latest_result_;
  std::shared_ptr<const MapImage> latest_map_image_;
  std::optional<CeresScanMatcherOptions> pending_options_;
  MatcherStats stats_;

  std::atomic<bool> reset_requested_{false};
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace prowl_brain
