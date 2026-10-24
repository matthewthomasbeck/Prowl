#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "prowl_ld19_driver/scan_assembler.hpp"

namespace prowl_ld19_driver
{

// How the sensor sits on the robot. Bearings are in the robot frame:
// degrees clockwise from the robot's front, [0, 360).
struct Mount
{
  double yaw_deg = 0.0;  // robot bearing of the sensor's 0 deg
  bool mirrored = false;  // sensor upside down: its angles run counterclockwise on the robot

  double bearing(double sensor_angle_deg) const;
};

struct AnalysisConfig
{
  uint16_t min_range_mm = 1;  // closer points (and 0 = no return) are ignored
  // A cluster ends where the range jumps by more than jump_mm + jump_ratio * range ...
  double jump_mm = 100.0;
  double jump_ratio = 0.03;
  // ... or after more than this many points without a return, or an angular gap this wide.
  size_t max_missing_points = 2;
  double max_gap_deg = 3.0;
  size_t min_cluster_points = 3;  // smaller clusters are treated as noise
};

struct Nearest
{
  bool found = false;
  double distance_m = 0.0;
  double bearing_deg = 0.0;
};

struct Object
{
  double distance_m;  // its closest point
  double bearing_deg;  // bearing of that closest point
  double width_m;  // straight line between its two ends
  size_t points;
};

// 8 sectors of 45 deg, centered on these directions, in this order.
constexpr size_t kSectors = 8;
constexpr std::array<const char *, kSectors> kSectorNames = {"F", "FR", "R", "BR", "B", "BL", "L", "FL"};
size_t sectorOf(double bearing_deg);

Nearest nearestPoint(const Scan & scan, const Mount & mount, const AnalysisConfig & config);
std::array<Nearest, kSectors> sectorNearest(const Scan & scan, const Mount & mount, const AnalysisConfig & config);

// Groups consecutive valid points into objects, nearest first.
std::vector<Object> findObjects(const Scan & scan, const Mount & mount, const AnalysisConfig & config);

}  // namespace prowl_ld19_driver
