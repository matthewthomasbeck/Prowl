#include "prowl_brain/sensor_buffer.hpp"

#include <utility>

namespace prowl_brain
{

SensorBuffer::SensorBuffer(std::size_t imu_capacity)
: imu_capacity_(imu_capacity)
{
}

void SensorBuffer::pushLidar(LidarFrame frame)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_lidar_ = std::move(frame);
  lidar_taken_ = false;
  ++lidar_count_;
}

void SensorBuffer::pushImu(const ImuSample & sample)
{
  std::lock_guard<std::mutex> lock(mutex_);
  imu_history_.push_back(sample);
  while (imu_history_.size() > imu_capacity_) {
    imu_history_.pop_front();
  }
  ++imu_count_;
}

std::optional<LidarFrame> SensorBuffer::latestLidar() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_lidar_;
}

std::optional<ImuSample> SensorBuffer::latestImu() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (imu_history_.empty()) {
    return std::nullopt;
  }
  return imu_history_.back();
}

std::optional<double> SensorBuffer::latestLidarStamp() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_lidar_) {
    return std::nullopt;
  }
  return latest_lidar_->stamp;
}

std::optional<double> SensorBuffer::latestImuStamp() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (imu_history_.empty()) {
    return std::nullopt;
  }
  return imu_history_.back().stamp;
}

std::optional<LidarFrame> SensorBuffer::takeUnmatchedLidar()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!latest_lidar_ || lidar_taken_) {
    return std::nullopt;
  }
  // A copy, not a move: thread 1 still wants latestLidar().
  lidar_taken_ = true;
  return latest_lidar_;
}

std::vector<ImuSample> SensorBuffer::imuBetween(double t0, double t1) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ImuSample> out;
  for (const ImuSample & s : imu_history_) {
    if (s.stamp > t1) {
      break;  // in time order, so nothing later can be inside
    }
    if (s.stamp > t0) {
      out.push_back(s);
    }
  }
  return out;
}

uint64_t SensorBuffer::lidarCount() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return lidar_count_;
}

uint64_t SensorBuffer::imuCount() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return imu_count_;
}

void SensorBuffer::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  latest_lidar_.reset();
  imu_history_.clear();
  lidar_taken_ = true;
}

}  // namespace prowl_brain
