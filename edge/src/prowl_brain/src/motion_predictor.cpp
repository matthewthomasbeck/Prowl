#include "prowl_brain/motion_predictor.hpp"

#include <cmath>

namespace prowl_brain
{

namespace
{

// Longest gap between two matches that still gives a believable speed.
constexpr double kMaxVelocityGap = 1.0;  // s

}  // namespace

void MotionPredictor::reset()
{
  last_pose_.reset();
  last_stamp_ = 0.0;
  velocity_x_ = 0.0;
  velocity_y_ = 0.0;
}

void MotionPredictor::addMatch(const MatchResult & match)
{
  const double dt = match.stamp - last_stamp_;
  if (last_pose_ && dt > 0.0 && dt < kMaxVelocityGap) {
    velocity_x_ = (match.pose.x - last_pose_->x) / dt;
    velocity_y_ = (match.pose.y - last_pose_->y) / dt;
  } else {
    velocity_x_ = 0.0;
    velocity_y_ = 0.0;
  }
  last_pose_ = match.pose;
  last_stamp_ = match.stamp;
}

MotionGuess MotionPredictor::predict(double stamp, const std::vector<ImuSample> & imu_since_last_match) const
{
  MotionGuess guess;
  if (!last_pose_) {
    return guess;
  }
  const double dt = stamp - last_stamp_;
  guess.delta_yaw = integrateYaw(imu_since_last_match, last_stamp_, stamp);
  guess.imu_samples = imu_since_last_match.size();
  guess.pose.x = last_pose_->x + velocity_x_ * dt;
  guess.pose.y = last_pose_->y + velocity_y_ * dt;
  guess.pose.yaw = std::remainder(last_pose_->yaw + guess.delta_yaw, 2.0 * M_PI);
  return guess;
}

double MotionPredictor::integrateYaw(const std::vector<ImuSample> & samples, double t0, double t1) const
{
  // Each reading's z rate covers the time since the reading before it (t0 for the
  // first one); the last rate also covers the gap up to t1. Assumes the IMU is flat,
  // z up (ROADMAP Phase 2, step 6).
  if (samples.empty()) {
    return 0.0;
  }
  double yaw = 0.0;
  double t = t0;
  for (const ImuSample & s : samples) {
    yaw += s.gyro[2] * (s.stamp - t);
    t = s.stamp;
  }
  yaw += samples.back().gyro[2] * (t1 - t);
  return yaw;
}

}  // namespace prowl_brain
