#pragma once

#include <cstddef>
#include <cstdint>

#include <sensor_msgs/msg/laser_scan.hpp>

#include "prowl_ld19_driver/scan_assembler.hpp"

namespace prowl_ld19_driver
{

struct LaserScanConfig
{
  size_t bins = 720;
  double range_min = 0.02;  // m
  double range_max = 12.0;  // m
  uint8_t min_intensity = 0;  // weaker points are dropped
};

// Fills everything but the header. Bin i holds the point nearest sensor angle
// i * 360 / bins (clockwise), which is ROS angle -i * 2pi / bins: angle_min is 0
// and angle_increment is negative. That keeps the bins in the order the LD19
// measured them, so time_increment is right for anything that corrects a scan
// for the robot's motion. Bins with no usable point are +inf, intensity 0; if
// several points land in one bin the nearest wins.
void fillLaserScan(const Scan & scan, const LaserScanConfig & config, sensor_msgs::msg::LaserScan & msg);

// How long the rotation took, in seconds: from the measured speed, or from the
// device timestamps if the speed field read 0.
double scanDuration(const Scan & scan);

}  // namespace prowl_ld19_driver
