#include "prowl_brain/ceres_scan_matcher.hpp"

#include <cmath>

namespace prowl_brain
{

namespace
{

// Fewer dots than this and a scan can't pin down a pose (e.g. the LiDAR is covered).
constexpr std::size_t kMinPoints = 50;

}  // namespace

CeresScanMatcher::CeresScanMatcher(const CeresScanMatcherOptions & options)
: options_(options)
{
}

void CeresScanMatcher::setOptions(const CeresScanMatcherOptions & options)
{
  options_ = options;
}

MatchResult CeresScanMatcher::match(const LidarFrame & scan, const MotionGuess & guess, const LocalMap & map) const
{
  MatchResult result;
  result.stamp = scan.stamp;
  result.pose = guess.pose;

  const std::vector<Point2D> points = scanToPoints(scan);
  if (points.size() < kMinPoints) {
    return result;
  }

  double pose[3] = {guess.pose.x, guess.pose.y, guess.pose.yaw};  // Ceres edits this in place
  MapGrid grid(map.cells().data(), 0, map.rows(), 0, map.cols());
  MapInterpolator interpolator(grid);

  ceres::Problem problem;
  addLidarCosts(problem, pose, points, map, interpolator);
  addPriorCosts(problem, pose, guess);
  if (problem.NumResidualBlocks() == 0) {
    return result;  // every weight is 0: nothing to go on
  }

  ceres::Solver::Summary summary;
  ceres::Solve(solverOptions(), &problem, &summary);

  // IsSolutionUsable(), not termination_type == CONVERGENCE: hitting max_iterations or
  // max_solve_time ends as NO_CONVERGENCE even when the pose is fine.
  result.pose = {pose[0], pose[1], std::remainder(pose[2], 2.0 * M_PI)};
  result.converged = summary.IsSolutionUsable();
  result.final_cost = summary.final_cost;
  result.iterations = static_cast<int>(summary.iterations.size());
  result.solve_time = summary.total_time_in_seconds;
  return result;
}

std::vector<Point2D> CeresScanMatcher::scanToPoints(const LidarFrame & scan) const
{
  // Same LiDAR-at-the-center assumption as LocalMap::insertScan.
  const std::size_t stride = options_.point_stride > 0 ? static_cast<std::size_t>(options_.point_stride) : 1;
  std::vector<Point2D> points;
  points.reserve(scan.ranges.size() / stride + 1);
  for (std::size_t i = 0; i < scan.ranges.size(); i += stride) {
    const double r = scan.ranges[i];
    if (!std::isfinite(r) || r < scan.range_min || r > scan.range_max) {
      continue;
    }
    const double a = scan.angle_min + static_cast<double>(i) * scan.angle_increment;
    points.push_back({r * std::cos(a), r * std::sin(a)});
  }
  return points;
}

void CeresScanMatcher::addLidarCosts(ceres::Problem & problem, double * pose, const std::vector<Point2D> & points,
  const LocalMap & map, const MapInterpolator & interpolator) const
{
  if (options_.weights.lidar <= 0.0 || points.empty()) {
    return;
  }
  // Divided by sqrt(N) so a scan with lots of dots doesn't drown out the IMU just by being long.
  const double weight = options_.weights.lidar / std::sqrt(static_cast<double>(points.size()));
  // The outlier filter judges the plain 0-to-1 residual, then ScaledLoss applies the volume
  // knob (weight^2 on the squared residual = weight on the residual). One loss shared by
  // every dot; the Problem owns it and deletes it once.
  ceres::LossFunction * huber = options_.outlier_scale > 0.0 ? new ceres::HuberLoss(options_.outlier_scale) : nullptr;
  ceres::LossFunction * loss = new ceres::ScaledLoss(huber, weight * weight, ceres::TAKE_OWNERSHIP);
  for (const Point2D & point : points) {
    problem.AddResidualBlock(
      new ceres::AutoDiffCostFunction<LidarPointCost, 1, 3>(new LidarPointCost(point, map, interpolator)),
      loss, pose);
  }
}

void CeresScanMatcher::addPriorCosts(ceres::Problem & problem, double * pose, const MotionGuess & guess) const
{
  // No Huber loss on these: there's only one of each, so nothing to be an outlier against.
  const ScanMatcherWeights & w = options_.weights;
  if (w.imu_yaw > 0.0 && guess.imu_samples > 0) {
    problem.AddResidualBlock(
      new ceres::AutoDiffCostFunction<ImuYawCost, 1, 3>(new ImuYawCost(guess.pose.yaw, w.imu_yaw)),
      nullptr, pose);
  }
  if (w.motion_translation > 0.0) {
    problem.AddResidualBlock(
      new ceres::AutoDiffCostFunction<MotionTranslationCost, 2, 3>(
        new MotionTranslationCost(guess.pose.x, guess.pose.y, w.motion_translation)),
      nullptr, pose);
  }
}

ceres::Solver::Options CeresScanMatcher::solverOptions() const
{
  ceres::Solver::Options o;
  o.max_num_iterations = options_.max_iterations;
  o.max_solver_time_in_seconds = options_.max_solve_time;
  o.linear_solver_type = ceres::DENSE_QR;  // fine for a 3-number problem
  o.num_threads = 1;  // the matcher already has a thread of its own
  o.minimizer_progress_to_stdout = false;
  return o;
}

}  // namespace prowl_brain
