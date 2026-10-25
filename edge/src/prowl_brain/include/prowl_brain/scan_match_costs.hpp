#pragma once

#include <cmath>

#include <ceres/ceres.h>
#include <ceres/cubic_interpolation.h>

#include "prowl_brain/local_map.hpp"
#include "prowl_brain/types.hpp"

// The three "complaints" the scan matcher tries to quiet down.
//
// How Ceres works, in one picture: imagine the robot's pose (x, y, yaw) as a
// ball rolling on a bumpy landscape. Each complaint below adds some height to
// that landscape wherever it's unhappy. Ceres rolls the ball downhill until
// it settles in the lowest valley: the pose where all the complaints, added
// together, are as small as possible.
//
// Each complaint has a WEIGHT, a volume knob. Turn the LiDAR's knob way up and
// the IMU's way down, and the LiDAR wins every argument: that's how we say
// "trust the LiDAR more than the IMU". The knobs live in config/brain.yaml.
//
// Each complaint is a "residual": a number that should be 0 when everything
// agrees. Ceres squares them and adds them up (so 2 counts as 4, and a minus
// sign doesn't let a complaint cancel another one out).
//
// Every complaint is a small class with a templated operator(). The template
// lets Ceres feed in a special number type (a Jet) that carries its own slope
// along with its value, so Ceres learns which way is downhill for free
// ("automatic differentiation"). Write the math once with T, and it works for
// both plain doubles and Jets.
//
// pose[0] = x (m), pose[1] = y (m), pose[2] = yaw (rad), in the map frame.

namespace prowl_brain
{

// The smooth "how wall-ish is this spot?" lookup the LiDAR complaint uses.
using MapGrid = ceres::Grid2D<double, 1>;
using MapInterpolator = ceres::BiCubicInterpolator<MapGrid>;

// Complaint 1, the LOUD one: "this LiDAR dot isn't sitting on a wall".
// There's one of these per LiDAR dot (up to 720 per scan).
//
// Take one dot (where a beam hit something, in the robot's frame), move it to
// where it would land on the map if the robot stood at `pose`, and look up how
// wall-ish the map is there. On a wall (1.0): no complaint. On empty floor
// (0.0): full complaint. So the residual is 1 - wall-ish-ness, always 0 to 1.
//
// Unlike the other two complaints, this one has NO weight inside it. Its
// volume knob is added outside, by the matcher (a ceres::ScaledLoss wrapped
// around the outlier filter). That way the outlier filter always judges the
// plain 0-to-1 "how far off the wall" number, so turning lidar_weight up or
// down never changes which dots count as outliers.
class LidarPointCost
{
public:
  LidarPointCost(const Point2D & point, const LocalMap & map, const MapInterpolator & interpolator)
  : point_(point), map_(map), interpolator_(interpolator)
  {
  }

  template<typename T>
  bool operator()(const T * const pose, T * residual) const
  {
    // Unqualified cos/sin: std:: for doubles, Ceres's own for Jets.
    using std::cos;
    using std::sin;
    const T c = cos(pose[2]);
    const T s = sin(pose[2]);
    const T mx = c * point_.x - s * point_.y + pose[0];
    const T my = s * point_.x + c * point_.y + pose[1];
    T col;
    T row;
    map_.worldToGrid(mx, my, col, row);
    T wallness;
    interpolator_.Evaluate(row, col, &wallness);  // row first
    residual[0] = T(1.0) - wallness;
    return true;
  }

private:
  Point2D point_;
  const LocalMap & map_;
  const MapInterpolator & interpolator_;
};

// Complaint 2, the quiet one: "you turned a different amount than the gyro says".
//
// The gyro (part of the MPU6050) is decent at measuring turns over a short
// time, but its zero point drifts as the chip warms up (ROADMAP Phase 2), so we
// only let it nudge the answer. Residual = weight * (yaw - gyro's guess),
// wrapped so that turning 359 degrees and turning -1 degree count as the same.
class ImuYawCost
{
public:
  ImuYawCost(double guessed_yaw, double weight)
  : guessed_yaw_(guessed_yaw), weight_(weight)
  {
  }

  template<typename T>
  bool operator()(const T * const pose, T * residual) const
  {
    using std::atan2;
    using std::cos;
    using std::sin;
    // atan2(sin, cos) wraps into -pi..pi and, unlike std::remainder, works on Jets.
    const T diff = pose[2] - guessed_yaw_;
    residual[0] = weight_ * atan2(sin(diff), cos(diff));
    return true;
  }

private:
  double guessed_yaw_;
  double weight_;
};

// Complaint 3, also quiet: "you slid somewhere different than where you were
// heading". This is the "keep going the way you were going" guess from
// MotionPredictor. It mostly matters in boring places like a long blank
// hallway, where the LiDAR alone can't tell how far along you are.
// Two residuals: weight * (x - guessed x) and weight * (y - guessed y).
class MotionTranslationCost
{
public:
  MotionTranslationCost(double guessed_x, double guessed_y, double weight)
  : guessed_x_(guessed_x), guessed_y_(guessed_y), weight_(weight)
  {
  }

  template<typename T>
  bool operator()(const T * const pose, T * residual) const
  {
    residual[0] = weight_ * (pose[0] - guessed_x_);
    residual[1] = weight_ * (pose[1] - guessed_y_);
    return true;
  }

private:
  double guessed_x_;
  double guessed_y_;
  double weight_;
};

}  // namespace prowl_brain
