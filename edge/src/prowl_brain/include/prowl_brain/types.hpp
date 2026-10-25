#pragma once

#include <array>
#include <cstddef>
#include <vector>

// The small "nouns" the rest of prowl_brain passes around. None of this knows
// about ROS: brain_node turns ROS messages into these, so the matcher, the map
// and the state machine are plain C++ that's easy to test without a robot.
//
// Units everywhere (ROS rule REP 103): meters, radians, seconds. x points
// forward, y points left, and angles grow counter-clockwise seen from above.

namespace prowl_brain
{

// The LD19 driver chops each spin into 720 slices of 0.5 degrees ("bins").
constexpr std::size_t kLidarBins = 720;

// Where the robot is on the floor. Think of a dot on a map (x, y) with an arrow
// showing which way it faces (yaw). yaw = 0 faces along the map's x axis.
struct Pose2D
{
  double x = 0.0;    // m
  double y = 0.0;    // m
  double yaw = 0.0;  // rad, counter-clockwise
};

// A point in the robot's own frame: x meters ahead of it, y meters to its left.
struct Point2D
{
  double x = 0.0;
  double y = 0.0;
};

// One full spin of the LiDAR: 720 distances, one per 0.5 degree slice.
// Slice i points at angle angle_min + i * angle_increment. On Prowl the
// increment is negative because the LD19 spins clockwise.
struct LidarFrame
{
  double stamp = 0.0;            // s, when the spin started
  double angle_min = 0.0;        // rad
  double angle_increment = 0.0;  // rad, negative on Prowl
  double range_min = 0.0;        // m, closer than this is junk
  double range_max = 0.0;        // m, farther than this is junk
  std::vector<float> ranges;       // m, +inf means "nothing seen in this slice"
  std::vector<float> intensities;  // how bright the reflection was, 0-255
};

// One reading from the MPU6050 (100 of these arrive every second).
struct ImuSample
{
  double stamp = 0.0;                  // s
  std::array<double, 3> gyro{};        // rad/s, how fast it's spinning around x, y, z
  std::array<double, 3> accel{};       // m/s^2, gravity included (standing still reads about +9.1 on z)
};

// The matcher's starting guess: where we think the robot is before looking at
// the LiDAR, built from the gyro (for turning) and the last few matches (for
// sliding). The matcher then nudges this guess until the LiDAR lines up.
struct MotionGuess
{
  Pose2D pose;                   // the guessed pose at the new scan's time
  double delta_yaw = 0.0;        // rad, how far the gyro says we turned since the last match
  std::size_t imu_samples = 0;   // how many gyro readings went into delta_yaw (0 = no IMU help)
};

// What one run of the scan matcher produced.
struct MatchResult
{
  double stamp = 0.0;        // s, the LiDAR frame's time
  Pose2D pose;               // the best pose the matcher found
  bool converged = false;    // is the answer usable? (Ceres finished without failing, see CeresScanMatcher::match)
  double final_cost = 0.0;   // how badly things still disagree (lower is better)
  int iterations = 0;        // how many improvement steps Ceres took
  double solve_time = 0.0;   // s, must stay well under 0.1 s to keep up with 10 Hz
};

}  // namespace prowl_brain
