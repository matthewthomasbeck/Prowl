#pragma once

#include <cstddef>

#include "prowl_mpu6050_driver/conversions.hpp"

namespace prowl_mpu6050_driver
{

// Averages uncorrected gyro readings (rad/s) taken while the robot is still.
// Feed it samples until done(); bias() is then the per-axis zero-rate offset.
//
// The stillness check looks at averages of `block` consecutive samples, not
// single samples. On Prowl the spinning LiDAR shakes the board at its 10 Hz
// rotation rate, which swings the gyro by up to several deg/s but averages to
// zero over one rotation (0.1 s). Turning or bumping the robot still shifts the
// block averages. block = 1 checks the raw samples.
class GyroBiasEstimator
{
public:
  // The window is rounded up to a whole number of blocks, at least two.
  explicit GyroBiasEstimator(size_t samples = 200, size_t block = 1);

  void reset();
  // Returns true once the target count has been reached; extra samples are ignored.
  bool add(const Vec3 & gyro);

  bool done() const {return count_ >= target_;}
  size_t count() const {return count_;}
  size_t target() const {return target_;}
  size_t block() const {return block_;}
  Vec3 bias() const;
  // Per-axis standard deviation of the single samples: sensor noise plus vibration.
  Vec3 stddev() const;
  // Per-axis standard deviation of the block averages: how much the robot moved.
  Vec3 blockStddev() const;
  // Largest per-axis blockStddev(), compared against max_stddev.
  bool still(double max_stddev) const;

private:
  size_t block_;
  size_t target_;
  size_t count_ = 0;
  Vec3 mean_{};
  Vec3 m2_{};  // running sum of squared deviations (Welford)
  Vec3 block_sum_{};
  size_t blocks_ = 0;
  Vec3 block_mean_{};
  Vec3 block_m2_{};
};

}  // namespace prowl_mpu6050_driver
