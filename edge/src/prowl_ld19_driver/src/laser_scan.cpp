#include "prowl_ld19_driver/laser_scan.hpp"

#include <cmath>
#include <limits>

namespace prowl_ld19_driver
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

}  // namespace

double scanDuration(const Scan & scan)
{
  if (scan.speed_dps > 0.0) {
    return 360.0 / scan.speed_dps;
  }
  return static_cast<double>(scan.end_time_ms - scan.start_time_ms) / 1000.0;
}

void fillLaserScan(const Scan & scan, const LaserScanConfig & config, sensor_msgs::msg::LaserScan & msg)
{
  const size_t bins = config.bins;
  const double increment = 2.0 * kPi / static_cast<double>(bins);
  msg.angle_min = 0.0f;
  msg.angle_increment = static_cast<float>(-increment);
  msg.angle_max = static_cast<float>(-increment * static_cast<double>(bins - 1));
  msg.range_min = static_cast<float>(config.range_min);
  msg.range_max = static_cast<float>(config.range_max);
  msg.scan_time = static_cast<float>(scanDuration(scan));
  msg.time_increment = msg.scan_time / static_cast<float>(bins);
  msg.ranges.assign(bins, std::numeric_limits<float>::infinity());
  msg.intensities.assign(bins, 0.0f);

  for (const ScanPoint & p : scan.points) {
    if (p.distance_mm == 0 || p.intensity < config.min_intensity) {
      continue;
    }
    const double range = p.distance_mm / 1000.0;
    if (range < config.range_min || range > config.range_max) {
      continue;
    }
    const size_t bin = static_cast<size_t>(std::lround(p.angle_deg / 360.0 * static_cast<double>(bins))) % bins;
    if (range < msg.ranges[bin]) {
      msg.ranges[bin] = static_cast<float>(range);
      msg.intensities[bin] = static_cast<float>(p.intensity);
    }
  }
}

}  // namespace prowl_ld19_driver
