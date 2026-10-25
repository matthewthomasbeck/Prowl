#pragma once

#include <cmath>
#include <cstdint>

namespace prowl_camera_driver
{

// libcamera stamps each frame (controls::SensorTimestamp) with the time its first row started
// exposing, in nanoseconds on CLOCK_MONOTONIC, the clock V4L2 uses. ROS stamps use the system
// clock, so shift it by the gap between the two clocks, both read at about the same moment.
inline int64_t monotonicToSystemNs(int64_t monotonic_ns, int64_t monotonic_now_ns, int64_t system_now_ns)
{
  return system_now_ns - (monotonic_now_ns - monotonic_ns);
}

// libcamera's FrameDurationLimits are in microseconds. Fixing both limits to one frame time fixes
// the frame rate; exposure can then be at most that long.
inline int64_t frameDurationUs(double fps)
{
  return std::llround(1e6 / fps);
}

}  // namespace prowl_camera_driver
