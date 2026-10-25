#include "prowl_brain/local_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace prowl_brain
{

namespace
{

// The cell a fractional grid position falls in (see worldToGrid).
int toCell(double v)
{
  return static_cast<int>(std::floor(v + 0.5));
}

}  // namespace

LocalMap::LocalMap(const LocalMapConfig & config)
: config_(config)
{
  cols_ = rows_ = static_cast<int>(std::ceil(config_.size / config_.resolution));
  origin_x_ = origin_y_ = -config_.size / 2.0;
  reset();
}

void LocalMap::reset()
{
  cells_.assign(static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_), 0.5);
  scans_inserted_ = 0;
}

void LocalMap::insertScan(const LidarFrame & scan, const Pose2D & pose)
{
  double col = 0.0;
  double row = 0.0;
  worldToGrid(pose.x, pose.y, col, row);
  const int robot_col = toCell(col);
  const int robot_row = toCell(row);
  const double c = std::cos(pose.yaw);
  const double s = std::sin(pose.yaw);

  for (std::size_t i = 0; i < scan.ranges.size(); ++i) {
    const double r = scan.ranges[i];
    if (!std::isfinite(r) || r < scan.range_min || r > scan.range_max) {
      continue;
    }
    // Assumes the LiDAR sits at the robot's center facing forward; a base_link ->
    // laser_link offset would be added to (px, py) here.
    const double a = scan.angle_min + static_cast<double>(i) * scan.angle_increment;
    const double px = r * std::cos(a);
    const double py = r * std::sin(a);
    worldToGrid(c * px - s * py + pose.x, s * px + c * py + pose.y, col, row);
    const int end_col = toCell(col);
    const int end_row = toCell(row);
    traceMisses(robot_col, robot_row, end_col, end_row);
    addToCell(end_col, end_row, config_.hit_increment);
  }
  ++scans_inserted_;
}

MapImage LocalMap::image(const Pose2D & robot, double stamp, double margin) const
{
  // The box around everything that's been seen (cells that moved off 0.5) and the robot.
  double col = 0.0;
  double row = 0.0;
  worldToGrid(robot.x, robot.y, col, row);
  int c_lo = std::clamp(toCell(col), 0, cols_ - 1);  // clamped, in case it's slid off the map
  int c_hi = c_lo;
  int r_lo = std::clamp(toCell(row), 0, rows_ - 1);
  int r_hi = r_lo;
  for (int r = 0; r < rows_; ++r) {
    const double * line = &cells_[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols_)];
    for (int c = 0; c < cols_; ++c) {
      if (line[c] != 0.5) {
        c_lo = std::min(c_lo, c);
        c_hi = std::max(c_hi, c);
        r_lo = std::min(r_lo, r);
        r_hi = std::max(r_hi, r);
      }
    }
  }
  const int pad = static_cast<int>(std::ceil(margin / config_.resolution));
  c_lo = std::max(c_lo - pad, 0);
  r_lo = std::max(r_lo - pad, 0);
  c_hi = std::min(c_hi + pad, cols_ - 1);
  r_hi = std::min(r_hi + pad, rows_ - 1);

  MapImage img;
  img.width = c_hi - c_lo + 1;
  img.height = r_hi - r_lo + 1;
  img.pixels.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
  for (int y = 0; y < img.height; ++y) {
    // Screen rows go down, map rows go up: the top image row is the highest map row.
    const int r = r_hi - y;
    for (int x = 0; x < img.width; ++x) {
      const double wallness = cells_[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols_) +
        static_cast<std::size_t>(c_lo + x)];
      img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)] =
        static_cast<uint8_t>(std::lround(255.0 * (1.0 - wallness)));
    }
  }
  // worldToGrid puts cell centers on whole numbers; pixel centers are at i + 0.5.
  img.robot_x = col - c_lo + 0.5;
  img.robot_y = r_hi - row + 0.5;
  img.robot_yaw = robot.yaw;
  img.resolution = config_.resolution;
  img.stamp = stamp;
  return img;
}

void LocalMap::addToCell(int col, int row, double amount)
{
  if (col < 0 || col >= cols_ || row < 0 || row >= rows_) {
    return;
  }
  double & cell = cells_[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols_) + static_cast<std::size_t>(col)];
  cell = std::clamp(cell + amount, 0.0, 1.0);
}

void LocalMap::traceMisses(int c0, int r0, int c1, int r1)
{
  // Bresenham: step one cell at a time along the longer direction, and keep a running
  // error that says when the line has drifted half a cell along the shorter one.
  // Done with dx and -dy in one error term, it handles every direction without cases.
  const int dx = std::abs(c1 - c0);
  const int dy = std::abs(r1 - r0);
  const int step_c = c0 < c1 ? 1 : -1;
  const int step_r = r0 < r1 ? 1 : -1;
  int err = dx - dy;
  int c = c0;
  int r = r0;
  while (c != c1 || r != r1) {
    addToCell(c, r, -config_.miss_decrement);
    const int err2 = 2 * err;
    if (err2 > -dy) {
      err -= dy;
      c += step_c;
    }
    if (err2 < dx) {
      err += dx;
      r += step_r;
    }
  }
}

}  // namespace prowl_brain
