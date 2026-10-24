#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>

namespace prowl_mpu6050_tui
{

struct Summary
{
  size_t count = 0;
  double mean = 0.0;
  double stddev = 0.0;  // sample standard deviation
  double min = 0.0;
  double max = 0.0;
};

// The last `capacity` values of one signal.
class WindowStats
{
public:
  explicit WindowStats(size_t capacity = 200)
  : capacity_(std::max<size_t>(capacity, 1)) {}

  void add(double v)
  {
    values_.push_back(v);
    trim();
  }

  void clear() {values_.clear();}

  void setCapacity(size_t capacity)
  {
    capacity_ = std::max<size_t>(capacity, 1);
    trim();
  }

  size_t capacity() const {return capacity_;}
  size_t size() const {return values_.size();}

  // Two passes over the window, so a large mean doesn't eat the variance.
  Summary summary() const
  {
    Summary s;
    s.count = values_.size();
    if (s.count == 0) {
      return s;
    }
    s.min = s.max = values_.front();
    double sum = 0.0;
    for (double v : values_) {
      sum += v;
      s.min = std::min(s.min, v);
      s.max = std::max(s.max, v);
    }
    s.mean = sum / static_cast<double>(s.count);
    if (s.count > 1) {
      double sq = 0.0;
      for (double v : values_) {
        sq += (v - s.mean) * (v - s.mean);
      }
      s.stddev = std::sqrt(sq / static_cast<double>(s.count - 1));
    }
    return s;
  }

private:
  void trim()
  {
    while (values_.size() > capacity_) {
      values_.pop_front();
    }
  }

  size_t capacity_;
  std::deque<double> values_;
};

}  // namespace prowl_mpu6050_tui
