#pragma once

#include <optional>
#include <vector>

#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// Makes the matcher's starting guess: "where is the robot probably now?"
//
// Picture walking with your eyes closed for a tenth of a second. You can still
// guess where you are: you know which way you were walking and how fast, and
// your inner ear tells you if you turned. That's this class.
//
//   - Turning comes from the gyro: add up (integrate) the z-axis spin rate over
//     the time between the last match and the new scan.
//   - Sliding (x, y) comes from how fast we were moving between the last two
//     matches ("keep going the way you were going"). We do NOT use the
//     accelerometer for this: it reads gravity about 7% low and isn't
//     calibrated yet (see ROADMAP Phase 2), and adding up a slightly wrong
//     acceleration twice to get position goes wrong fast.
//
// The guess doesn't have to be perfect. It just has to land close enough that
// the matcher, which only searches nearby, can slide it into place.
class MotionPredictor
{
public:
  MotionPredictor() = default;

  // Forget the past (e.g. after SensorLost). The next guess will be "didn't move".
  void reset();

  // Tell it where the matcher just put the robot, so it can work out our speed.
  void addMatch(const MatchResult & match);

  // Guess the pose at time `stamp`, using the gyro readings since the last match.
  MotionGuess predict(double stamp, const std::vector<ImuSample> & imu_since_last_match) const;

  // The last pose the matcher gave us, if any.
  std::optional<Pose2D> lastPose() const {return last_pose_;}
  // When that pose was (only meaningful if lastPose() has a value).
  double lastStamp() const {return last_stamp_;}

private:
  // Adds up gyro z * time between readings: how many radians we turned.
  double integrateYaw(const std::vector<ImuSample> & samples, double t0, double t1) const;

  std::optional<Pose2D> last_pose_;
  double last_stamp_ = 0.0;
  // How fast we were going, in the map frame: m/s and m/s.
  double velocity_x_ = 0.0;
  double velocity_y_ = 0.0;
};

}  // namespace prowl_brain
