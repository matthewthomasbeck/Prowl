#include "prowl_mpu6050_driver/gyro_bias.hpp"

#include <algorithm>
#include <cmath>

namespace prowl_mpu6050_driver
{

namespace
{

// Welford's update of a running mean and sum of squared deviations.
void accumulate(const Vec3 & x, size_t n, Vec3 & mean, Vec3 & m2)
{
  for (int i = 0; i < 3; ++i) {
    const double delta = x[i] - mean[i];
    mean[i] += delta / static_cast<double>(n);
    m2[i] += delta * (x[i] - mean[i]);
  }
}

Vec3 sampleStddev(const Vec3 & m2, size_t n)
{
  Vec3 s{};
  if (n > 1) {
    for (int i = 0; i < 3; ++i) {
      s[i] = std::sqrt(m2[i] / static_cast<double>(n - 1));
    }
  }
  return s;
}

}  // namespace

GyroBiasEstimator::GyroBiasEstimator(size_t samples, size_t block)
: block_(std::max<size_t>(block, 1))
{
  // Whole blocks, so the bias also averages over whole LiDAR rotations.
  const size_t blocks = (std::max<size_t>(samples, 2) + block_ - 1) / block_;
  target_ = std::max<size_t>(blocks, 2) * block_;
}

void GyroBiasEstimator::reset()
{
  count_ = 0;
  mean_ = {};
  m2_ = {};
  block_sum_ = {};
  blocks_ = 0;
  block_mean_ = {};
  block_m2_ = {};
}

bool GyroBiasEstimator::add(const Vec3 & gyro)
{
  if (done()) {
    return true;
  }
  ++count_;
  accumulate(gyro, count_, mean_, m2_);
  for (int i = 0; i < 3; ++i) {
    block_sum_[i] += gyro[i];
  }
  if (count_ % block_ == 0) {
    Vec3 avg;
    for (int i = 0; i < 3; ++i) {
      avg[i] = block_sum_[i] / static_cast<double>(block_);
    }
    block_sum_ = {};
    accumulate(avg, ++blocks_, block_mean_, block_m2_);
  }
  return done();
}

Vec3 GyroBiasEstimator::bias() const
{
  return mean_;
}

Vec3 GyroBiasEstimator::stddev() const
{
  return sampleStddev(m2_, count_);
}

Vec3 GyroBiasEstimator::blockStddev() const
{
  return sampleStddev(block_m2_, blocks_);
}

bool GyroBiasEstimator::still(double max_stddev) const
{
  const Vec3 s = blockStddev();
  return *std::max_element(s.begin(), s.end()) <= max_stddev;
}

}  // namespace prowl_mpu6050_driver
