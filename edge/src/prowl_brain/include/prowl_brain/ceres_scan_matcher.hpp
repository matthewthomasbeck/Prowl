#pragma once

#include <vector>

#include <ceres/ceres.h>

#include "prowl_brain/local_map.hpp"
#include "prowl_brain/scan_match_costs.hpp"
#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// ======================= THE TUNING KNOBS =======================
// The volume knobs for the three complaints in scan_match_costs.hpp. Only the
// RATIOS matter: lidar 10 / imu_yaw 1 means "the LiDAR's opinion counts ten
// times as much as the gyro's". To trust the IMU less, turn imu_yaw down (or
// lidar up). Setting a weight to 0 switches that complaint off completely.
//
// These come from config/brain.yaml, and can be changed while the robot runs:
//   ros2 param set /brain lidar_weight 20.0
// The new value takes effect on the matcher's next run (within 0.1 s).
struct ScanMatcherWeights
{
  double lidar = 10.0;              // LiDAR dots landing on walls: the boss
  double imu_yaw = 1.0;             // the gyro's guess of how far we turned
  double motion_translation = 0.5;  // "keep sliding the way you were sliding"
};

// Everything else about how hard the matcher tries.
struct CeresScanMatcherOptions
{
  ScanMatcherWeights weights;
  int max_iterations = 20;      // max downhill steps per match
  double max_solve_time = 0.05; // s; must stay well under 0.1 s, or the 10 Hz thread falls behind
  int point_stride = 1;         // use every Nth LiDAR bin (2 = 360 dots instead of 720, about twice as fast)
  // How far a dot can be off the walls before we stop believing it fully.
  // Things like a person walking past don't match the map, and without this
  // they'd drag the answer toward them. With it, a dot that's way off only
  // counts "a bit" instead of "a lot" (a Huber loss). 0 = off.
  // It's measured in "1 - wall-ish-ness" (0 to 1), not meters: 0.3 means a dot
  // landing where the map is less than 0.7 wall-ish starts counting less. That
  // includes unexplored squares (0.5). It doesn't depend on lidar_weight or
  // point_stride.
  double outlier_scale = 0.3;
};

// Lines up a new LiDAR scan with the map, starting from a guess.
//
// Like holding a tracing of the room (the new scan) over the real map and
// sliding and turning it until the walls line up. The LiDAR complaint pulls
// toward "walls on walls", the IMU and motion complaints gently pull toward
// "where we expected to be", and the weights decide who wins.
//
// Only thread 2 uses this.
class CeresScanMatcher
{
public:
  explicit CeresScanMatcher(const CeresScanMatcherOptions & options);

  // Swap in new knob settings (thread 2 calls this between matches when the
  // weights were changed live).
  void setOptions(const CeresScanMatcherOptions & options);
  const CeresScanMatcherOptions & options() const {return options_;}

  // The main event: find the pose where `scan` best fits `map`, starting from `guess`.
  MatchResult match(const LidarFrame & scan, const MotionGuess & guess, const LocalMap & map) const;

  // Turn the scan's 720 (distance, angle) pairs into dots in the robot's frame,
  // skipping empty bins and every point_stride-th one. Public so it can be tested alone.
  std::vector<Point2D> scanToPoints(const LidarFrame & scan) const;

private:
  // Add one LidarPointCost per dot to the problem.
  void addLidarCosts(ceres::Problem & problem, double * pose, const std::vector<Point2D> & points,
    const LocalMap & map, const MapInterpolator & interpolator) const;
  // Add the ImuYawCost and MotionTranslationCost (skipping any whose weight is 0,
  // and skipping the IMU one if the guess used no IMU samples).
  void addPriorCosts(ceres::Problem & problem, double * pose, const MotionGuess & guess) const;
  // Fill in Ceres's own settings (iterations, time limit, which linear solver) from options_.
  ceres::Solver::Options solverOptions() const;

  CeresScanMatcherOptions options_;
};

}  // namespace prowl_brain
