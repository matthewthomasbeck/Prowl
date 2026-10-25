#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// The mailbox the two threads share.
//
// Thread 1 (the main, as-fast-as-possible loop) drops every new LiDAR spin and
// IMU reading in here the moment it arrives. Thread 2 (the 10 Hz matcher)
// comes by and takes out what it needs.
//
// Why a lock (mutex)? Two threads touching the same data at the same moment is
// like two people writing on the same line of paper at once: you get garbage.
// The mutex is a "talking stick": only the thread holding it may read or write.
// Every function below grabs it, does something quick, and lets go, so neither
// thread waits long. Big work (the actual matching) happens on copies, outside
// the lock.
class SensorBuffer
{
public:
  // imu_capacity: how many IMU readings to remember. At 100 Hz, 400 is 4 s,
  // plenty to cover the gap between two matches even if one runs late.
  explicit SensorBuffer(std::size_t imu_capacity = 400);

  // ---- Called by thread 1 (writers) ----

  // Replace the newest LiDAR spin and mark it "not matched yet".
  void pushLidar(LidarFrame frame);
  // Add one IMU reading to the history, dropping the oldest if full.
  void pushImu(const ImuSample & sample);

  // ---- Called by thread 1 (readers, for the state machine) ----

  // Copies of the newest data, or nothing if none has arrived yet.
  std::optional<LidarFrame> latestLidar() const;
  std::optional<ImuSample> latestImu() const;
  // Stamp of the newest data, so the state machine can tell if a sensor went quiet.
  std::optional<double> latestLidarStamp() const;
  std::optional<double> latestImuStamp() const;

  // ---- Called by thread 2 (the matcher) ----

  // Hands over the newest LiDAR spin if the matcher hasn't seen it yet, and
  // marks it as seen. Returns nothing if there's no new spin (so the matcher
  // doesn't waste time matching the same spin twice).
  std::optional<LidarFrame> takeUnmatchedLidar();
  // Every IMU reading with t0 < stamp <= t1: the gyro readings between two scans.
  std::vector<ImuSample> imuBetween(double t0, double t1) const;

  // ---- Counters for the stats log ----
  uint64_t lidarCount() const;
  uint64_t imuCount() const;

  // Forget everything (e.g. after a sensor dropped out and came back).
  void clear();

private:
  mutable std::mutex mutex_;  // the "talking stick"; everything below needs it held

  std::optional<LidarFrame> latest_lidar_;
  bool lidar_taken_ = true;  // has thread 2 already taken latest_lidar_?
  std::deque<ImuSample> imu_history_;  // oldest at the front, newest at the back
  std::size_t imu_capacity_;
  uint64_t lidar_count_ = 0;
  uint64_t imu_count_ = 0;
};

}  // namespace prowl_brain
