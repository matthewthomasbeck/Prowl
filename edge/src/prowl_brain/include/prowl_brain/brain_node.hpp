#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <std_msgs/msg/string.hpp>

#include "prowl_brain/ceres_scan_matcher.hpp"
#include "prowl_brain/local_map.hpp"
#include "prowl_brain/map_display.hpp"
#include "prowl_brain/matcher_thread.hpp"
#include "prowl_brain/sensor_buffer.hpp"
#include "prowl_brain/state_machine.hpp"
#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// The robot's "brain": the one ROS node that listens to all the drivers and
// decides what to do.
//
// The big picture (each box is a separate program, the drivers you already have):
//
//   ld19_driver ----/scan (10 Hz, 720 bins)----+
//   mpu6050_driver --/imu/data_raw (100 Hz)----+--> brain --/robot_pose--> dashboard
//   motor_driver ---/motor_status (50 Hz)------+      |  --/brain_state->
//        ^                                            |
//        +-------------/cmd_vel-----------------------+
//
// Inside the brain there are exactly two threads (two things happening at once):
//
//   THREAD 1, main thread, as fast as possible ("the reflexes")
//     Loops forever: let ROS deliver any new messages (which land in the
//     SensorBuffer), then run one tick() of the state machine. It never does
//     anything slow, so it notices problems right away.
//
//   THREAD 2, MatcherThread, 10 times a second ("the thinking")
//     Takes the newest scan out of the SensorBuffer, works out where the robot
//     is with the Ceres scan matcher, and leaves the answer for thread 1, plus a
//     picture of the map a few times a second, which thread 1 shows on the
//     robot's screen (MapDisplay).
//
// Only thread 1 ever touches ROS (subscriptions, publishers, parameters).
// Thread 2 only talks to the SensorBuffer and its own result mailbox. Keeping
// ROS on one thread means we never have to wonder which thread a callback
// runs on: it's always thread 1.
class BrainNode : public rclcpp::Node
{
public:
  BrainNode();
  ~BrainNode() override;

  // ---- Startup, called once by main() ----

  // Open the map window on the robot's screen, if `display` is on and there is a
  // screen. Before waitForDrivers(): opening takes a moment, which would otherwise
  // make the sensors look quiet right after startup.
  void openDisplay();

  // Keep handing messages to the node (spinning `executor`) until every driver
  // has said something: a /scan with the right number of bins, an
  // /imu/data_raw (which only starts after the IMU's 2 s gyro calibration), and
  // a /motor_status (if require_motor_driver). Returns false if that takes
  // longer than driver_wait_timeout, so main() can give up with a clear message
  // about which driver is missing. Also returns false as soon as `interrupted`
  // turns true (Ctrl-C while waiting).
  bool waitForDrivers(rclcpp::Executor & executor, const std::atomic<bool> & interrupted);

  // Start thread 2.
  void startMatcher();

  // ---- Thread 1, called by main() over and over ----

  // One frame: gather inputs, step the state machine, do what the state says.
  void tick();

  // Ask for a clean stop: the next tick() moves to State::Stopped and stops the motors.
  void requestShutdown();
  bool stopped() const;

private:
  // ---- Setup ----

  // Declare every parameter from config/brain.yaml and store the values in the members below.
  void declareParameters();
  // Build the matcher's options (weights etc.) from the current parameter values.
  CeresScanMatcherOptions matcherOptionsFromParameters() const;
  // Runs when someone does `ros2 param set /brain ...`. Checks the new values
  // make sense (no negative weights, rate > 0, ...) and passes matcher knobs on
  // to thread 2. Returns "rejected" with a reason if a value is bad.
  rcl_interfaces::msg::SetParametersResult onParametersChanged(const std::vector<rclcpp::Parameter> & params);

  // ---- Subscription callbacks (run on thread 1, inside the executor) ----

  // Convert to a LidarFrame and drop it in the SensorBuffer. Rejects scans
  // whose bin count isn't expected_bins (720), with a throttled warning.
  void onScan(sensor_msgs::msg::LaserScan::ConstSharedPtr msg);
  // Convert to an ImuSample and drop it in the SensorBuffer.
  void onImu(sensor_msgs::msg::Imu::ConstSharedPtr msg);
  // Just remember when the motor driver last spoke, to know it's alive.
  void onMotorStatus(std_msgs::msg::Float64MultiArray::ConstSharedPtr msg);

  // ROS message -> our plain types (from types.hpp).
  LidarFrame toLidarFrame(const sensor_msgs::msg::LaserScan & msg) const;
  ImuSample toImuSample(const sensor_msgs::msg::Imu & msg) const;

  // ---- The tick, split into steps ----

  // Step 1: snapshot everything the state machine needs (ages of the newest data, etc.).
  FrameInputs gatherInputs();
  // Step 2b: one-off actions when a state is first entered (log it, publish /brain_state,
  // reset the matcher on BuildingFirstMap, stop the motors on SensorLost/Stopped).
  void onStateEntered(State state, const FrameInputs & in);
  // Step 3: the every-frame actions for the current state (e.g. publish a new pose when
  // thread 2 has one, keep the motors stopped while not Tracking).
  void runState(State state, const FrameInputs & in);

  // ---- Outputs ----

  // MatchResult -> geometry_msgs/PoseStamped on /robot_pose, in frame map_frame_.
  void publishPose(const MatchResult & result);
  void publishState(State state);
  // Send an all-zero /cmd_vel. The motor driver also stops by itself 0.5 s
  // after commands stop, so this is the polite way and the timeout is the backup.
  void stopMotors();
  // Draw thread 2's newest map picture (if it's running yet) on the screen inside a
  // `frame`-colored border, at most display_rate times a second.
  void updateDisplay(Rgb frame);
  // Every stats_period: frames per second of thread 1, matches/failures/overruns of thread 2.
  void logStats();

  // ---- Parameters (see config/brain.yaml) ----
  std::size_t expected_bins_ = kLidarBins;
  double driver_wait_timeout_ = 30.0;
  bool require_motor_driver_ = true;
  double matcher_rate_ = 10.0;
  std::string map_frame_ = "map";
  bool display_enabled_ = true;
  double display_rate_ = 5.0;
  StateMachineConfig state_machine_config_;
  LocalMapConfig map_config_;

  // ---- The two-thread machinery ----
  SensorBuffer sensors_;
  StateMachine state_machine_;
  std::unique_ptr<MatcherThread> matcher_;  // thread 2; created in startMatcher()

  // ---- Thread 1 bookkeeping ----
  std::optional<rclcpp::Time> last_motor_status_;
  std::optional<double> last_published_match_stamp_;  // so each pose is published once
  bool shutdown_requested_ = false;
  uint64_t frames_ = 0;  // ticks since the last stats log
  std::chrono::steady_clock::time_point last_stats_time_;
  std::unique_ptr<MapDisplay> display_;  // null when there's no screen (or it was closed)
  std::chrono::steady_clock::time_point next_display_;
  double last_display_time_ = 0.0;  // s the last frame took to draw

  // ---- ROS plumbing ----
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
  rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr motor_status_sub_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_pub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  OnSetParametersCallbackHandle::SharedPtr param_callback_;
};

}  // namespace prowl_brain
