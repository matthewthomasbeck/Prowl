#include "prowl_brain/brain_node.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace prowl_brain
{

namespace
{

using Clock = std::chrono::steady_clock;

// Copies one matcher knob into `o`. Returns false if `p` isn't a matcher knob.
bool setMatcherOption(CeresScanMatcherOptions & o, const rclcpp::Parameter & p)
{
  const std::string & n = p.get_name();
  if (n == "lidar_weight") {
    o.weights.lidar = p.as_double();
  } else if (n == "imu_yaw_weight") {
    o.weights.imu_yaw = p.as_double();
  } else if (n == "motion_translation_weight") {
    o.weights.motion_translation = p.as_double();
  } else if (n == "max_iterations") {
    o.max_iterations = static_cast<int>(p.as_int());
  } else if (n == "max_solve_time") {
    o.max_solve_time = p.as_double();
  } else if (n == "point_stride") {
    o.point_stride = static_cast<int>(p.as_int());
  } else if (n == "outlier_scale") {
    o.outlier_scale = p.as_double();
  } else {
    return false;
  }
  return true;
}

// What's wrong with `p`'s value, or "" if it's fine (or isn't one of ours).
// Used at startup and for `ros2 param set`.
std::string checkParameter(const rclcpp::Parameter & p)
{
  const std::string & n = p.get_name();
  if (n == "driver_wait_timeout" || n == "sensor_timeout" || n == "match_timeout" || n == "matcher_rate" ||
    n == "max_solve_time" || n == "map_resolution" || n == "map_size" || n == "map_hit_increment" ||
    n == "display_rate")
  {
    return p.as_double() > 0.0 ? "" : "must be positive";
  }
  if (n == "lidar_weight" || n == "imu_yaw_weight" || n == "motion_translation_weight" ||
    n == "outlier_scale" || n == "map_miss_decrement" || n == "stats_period")
  {
    return p.as_double() >= 0.0 ? "" : "must not be negative";
  }
  if (n == "expected_bins" || n == "max_iterations" || n == "point_stride") {
    return p.as_int() >= 1 ? "" : "must be at least 1";
  }
  if (n == "map_frame") {
    return p.as_string().empty() ? "must not be empty" : "";
  }
  return "";
}

// The frame around the map on the screen says what the brain is doing.
constexpr Rgb kWaitingColor{70, 110, 200};  // blue: waiting for the drivers

Rgb stateColor(State state)
{
  switch (state) {
    case State::BuildingFirstMap: return {230, 180, 40};  // yellow: getting ready
    case State::Tracking: return {60, 180, 90};           // green: all good
    case State::SensorLost: return {220, 60, 50};         // red: motors stopped
    case State::Stopped: return {90, 90, 90};             // grey: shutting down
  }
  return {};
}

// "1.2 s", or "never" for the huge default age.
std::string describeAge(double age)
{
  if (age >= 1e8) {
    return "never";
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f s", age);
  return buf;
}

}  // namespace

BrainNode::BrainNode()
: Node("brain"),
  state_machine_(StateMachineConfig{})
{
  declareParameters();
  state_machine_ = StateMachine(state_machine_config_);

  // Depth 10 and the default (reliable) QoS, like the drivers' publishers.
  scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
    "scan", 10, [this](sensor_msgs::msg::LaserScan::ConstSharedPtr m) {onScan(m);});
  imu_sub_ = create_subscription<sensor_msgs::msg::Imu>(
    "imu/data_raw", 10, [this](sensor_msgs::msg::Imu::ConstSharedPtr m) {onImu(m);});
  motor_status_sub_ = create_subscription<std_msgs::msg::Float64MultiArray>(
    "motor_status", 10, [this](std_msgs::msg::Float64MultiArray::ConstSharedPtr m) {onMotorStatus(m);});

  pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>("robot_pose", 10);
  // Latched, so a dashboard that connects later still learns the current state.
  state_pub_ = create_publisher<std_msgs::msg::String>("brain_state", rclcpp::QoS(1).transient_local());
  cmd_vel_pub_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 10);

  param_callback_ = add_on_set_parameters_callback(
    [this](const std::vector<rclcpp::Parameter> & p) {return onParametersChanged(p);});

  const double stats_period = get_parameter("stats_period").as_double();
  if (stats_period > 0.0) {
    stats_timer_ = create_wall_timer(std::chrono::duration<double>(stats_period), [this] {logStats();});
  }
  last_stats_time_ = Clock::now();
}

BrainNode::~BrainNode()
{
  // Stop thread 2 before anything it uses (sensors_) goes away.
  if (matcher_) {
    matcher_->stop();
  }
}

bool BrainNode::waitForDrivers(rclcpp::Executor & executor, const std::atomic<bool> & interrupted)
{
  const auto missing = [this] {
      std::string m;
      const auto add = [&m](const char * what) {
          m += m.empty() ? what : std::string(", ") + what;
        };
      if (!sensors_.latestLidarStamp()) {
        add("/scan from ld19_driver");
      }
      if (!sensors_.latestImuStamp()) {
        add("/imu/data_raw from mpu6050_driver (it calibrates its gyro for ~2 s first)");
      }
      if (require_motor_driver_ && !last_motor_status_) {
        add("/motor_status from motor_driver");
      }
      return m;
    };

  RCLCPP_INFO(get_logger(), "waiting up to %.0f s for the drivers", driver_wait_timeout_);
  const auto deadline = Clock::now() + std::chrono::duration<double>(driver_wait_timeout_);
  std::string still_missing = missing();
  while (rclcpp::ok() && !interrupted && Clock::now() < deadline) {
    executor.spin_once(std::chrono::milliseconds(100));
    if (display_) {
      updateDisplay(kWaitingColor);
    }
    still_missing = missing();
    if (still_missing.empty()) {
      RCLCPP_INFO(get_logger(), "all drivers up");
      return true;
    }
    RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000, "still waiting for %s", still_missing.c_str());
  }
  if (!interrupted && rclcpp::ok()) {
    RCLCPP_ERROR(get_logger(), "gave up after %.0f s: no %s. Start the drivers with "
      "`docker/dev.sh ros2 launch prowl_brain brain.launch.py`, or run the missing one on its own",
      driver_wait_timeout_, still_missing.c_str());
  }
  return false;
}

void BrainNode::openDisplay()
{
  if (!display_enabled_) {
    return;
  }
  display_ = std::make_unique<MapDisplay>();
  if (display_->open()) {
    RCLCPP_INFO(get_logger(), "showing the map on the screen (%s, %.0f Hz); Esc or q there closes it",
      display_->describe().c_str(), display_rate_);
  } else {
    RCLCPP_INFO(get_logger(), "no screen for the map (%s); carrying on without it", display_->error().c_str());
    display_.reset();
  }
}

void BrainNode::startMatcher()
{
  // Thread 2 only makes map pictures if there's a screen to show them on.
  matcher_ = std::make_unique<MatcherThread>(sensors_, matcherOptionsFromParameters(), map_config_, matcher_rate_,
    display_ ? 1.0 / display_rate_ : 0.0);
  matcher_->start();
  RCLCPP_INFO(get_logger(), "scan matcher running at %.1f Hz", matcher_rate_);
}

void BrainNode::tick()
{
  // Keep this fast: no sleeping, no waiting, no heavy math. That's thread 2's job.
  const FrameInputs in = gatherInputs();
  const State state = state_machine_.step(in);
  if (state_machine_.justChanged()) {
    onStateEntered(state, in);
  }
  runState(state, in);
  if (display_) {
    updateDisplay(stateColor(state));
  }
  ++frames_;
}

void BrainNode::requestShutdown()
{
  shutdown_requested_ = true;
}

bool BrainNode::stopped() const
{
  return state_machine_.state() == State::Stopped;
}

void BrainNode::declareParameters()
{
  driver_wait_timeout_ = declare_parameter("driver_wait_timeout", 30.0);
  require_motor_driver_ = declare_parameter("require_motor_driver", true);
  declare_parameter<int64_t>("expected_bins", static_cast<int64_t>(kLidarBins));

  state_machine_config_.sensor_timeout = declare_parameter("sensor_timeout", 0.5);
  state_machine_config_.match_timeout = declare_parameter("match_timeout", 1.0);

  matcher_rate_ = declare_parameter("matcher_rate", 10.0);
  // The matcher knobs are read back in matcherOptionsFromParameters().
  const CeresScanMatcherOptions defaults;
  declare_parameter("lidar_weight", defaults.weights.lidar);
  declare_parameter("imu_yaw_weight", defaults.weights.imu_yaw);
  declare_parameter("motion_translation_weight", defaults.weights.motion_translation);
  declare_parameter<int64_t>("max_iterations", defaults.max_iterations);
  declare_parameter("max_solve_time", defaults.max_solve_time);
  declare_parameter<int64_t>("point_stride", defaults.point_stride);
  declare_parameter("outlier_scale", defaults.outlier_scale);

  map_config_.resolution = declare_parameter("map_resolution", 0.05);
  map_config_.size = declare_parameter("map_size", 20.0);
  map_config_.hit_increment = declare_parameter("map_hit_increment", 0.35);
  map_config_.miss_decrement = declare_parameter("map_miss_decrement", 0.1);

  map_frame_ = declare_parameter("map_frame", std::string("map"));
  display_enabled_ = declare_parameter("display", true);
  display_rate_ = declare_parameter("display_rate", 5.0);
  declare_parameter("stats_period", 5.0);

  for (const rclcpp::Parameter & p : get_parameters(list_parameters({}, 0).names)) {
    const std::string why = checkParameter(p);
    if (!why.empty()) {
      throw std::runtime_error(p.get_name() + " " + why);
    }
  }
  if (map_config_.size < map_config_.resolution) {
    throw std::runtime_error("map_size must be at least map_resolution");
  }
  expected_bins_ = static_cast<std::size_t>(get_parameter("expected_bins").as_int());
  if (get_parameter("max_solve_time").as_double() >= 1.0 / matcher_rate_) {
    RCLCPP_WARN(get_logger(), "max_solve_time is longer than 1 / matcher_rate: the matcher will fall behind");
  }
}

CeresScanMatcherOptions BrainNode::matcherOptionsFromParameters() const
{
  CeresScanMatcherOptions o;
  for (const char * name : {"lidar_weight", "imu_yaw_weight", "motion_translation_weight", "max_iterations",
      "max_solve_time", "point_stride", "outlier_scale"})
  {
    setMatcherOption(o, get_parameter(name));
  }
  return o;
}

rcl_interfaces::msg::SetParametersResult BrainNode::onParametersChanged(const std::vector<rclcpp::Parameter> & params)
{
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;

  // get_parameter() still returns the old values inside this callback, so start from
  // those and lay the new ones on top.
  CeresScanMatcherOptions options = matcherOptionsFromParameters();
  bool matcher_changed = false;
  std::optional<std::string> new_map_frame;
  for (const rclcpp::Parameter & p : params) {
    std::string why = checkParameter(p);
    if (why.empty()) {
      if (setMatcherOption(options, p)) {
        matcher_changed = true;
      } else if (p.get_name() == "map_frame") {
        new_map_frame = p.as_string();
      } else {
        why = "can't change while running: restart the node to change this";
      }
    }
    if (!why.empty()) {
      result.successful = false;
      result.reason = p.get_name() + " " + why;
      return result;
    }
  }

  if (new_map_frame) {
    map_frame_ = *new_map_frame;
  }
  // Before startMatcher() there's no thread 2 yet; it reads the new values when it starts.
  if (matcher_changed && matcher_) {
    matcher_->setOptions(options);
  }
  return result;
}

void BrainNode::onScan(sensor_msgs::msg::LaserScan::ConstSharedPtr msg)
{
  if (msg->ranges.size() != expected_bins_) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "ignoring /scan with %zu bins: the matcher expects %zu (expected_bins, and the LD19 driver's bins)",
      msg->ranges.size(), expected_bins_);
    return;
  }
  sensors_.pushLidar(toLidarFrame(*msg));
}

void BrainNode::onImu(sensor_msgs::msg::Imu::ConstSharedPtr msg)
{
  sensors_.pushImu(toImuSample(*msg));
}

void BrainNode::onMotorStatus(std_msgs::msg::Float64MultiArray::ConstSharedPtr msg)
{
  (void)msg;  // the duties aren't needed yet, only that the driver is alive
  last_motor_status_ = now();
}

LidarFrame BrainNode::toLidarFrame(const sensor_msgs::msg::LaserScan & msg) const
{
  LidarFrame f;
  f.stamp = rclcpp::Time(msg.header.stamp).seconds();
  f.angle_min = msg.angle_min;
  f.angle_increment = msg.angle_increment;
  f.range_min = msg.range_min;
  f.range_max = msg.range_max;
  f.ranges = msg.ranges;
  f.intensities = msg.intensities;
  return f;
}

ImuSample BrainNode::toImuSample(const sensor_msgs::msg::Imu & msg) const
{
  // Nothing uses accel yet (it reads gravity ~7% low, uncalibrated); it's kept for
  // when the six-position calibration is done.
  ImuSample s;
  s.stamp = rclcpp::Time(msg.header.stamp).seconds();
  s.gyro = {msg.angular_velocity.x, msg.angular_velocity.y, msg.angular_velocity.z};
  s.accel = {msg.linear_acceleration.x, msg.linear_acceleration.y, msg.linear_acceleration.z};
  return s;
}

FrameInputs BrainNode::gatherInputs()
{
  FrameInputs in;
  const rclcpp::Time t = now();
  in.now = t.seconds();
  // The scan stamp is the START of a 0.1 s spin, so a healthy scan is ~0.1-0.2 s old.
  if (const auto stamp = sensors_.latestLidarStamp()) {
    in.lidar_age = in.now - *stamp;
  }
  if (const auto stamp = sensors_.latestImuStamp()) {
    in.imu_age = in.now - *stamp;
  }
  if (last_motor_status_) {
    in.motor_status_age = (t - *last_motor_status_).seconds();
  }
  if (matcher_) {
    if (const auto result = matcher_->latestResult()) {
      in.have_match = true;
      in.match_age = in.now - result->stamp;
    }
  }
  in.shutdown_requested = shutdown_requested_;
  return in;
}

void BrainNode::onStateEntered(State state, const FrameInputs & in)
{
  RCLCPP_INFO(get_logger(), "state: %s", toString(state));
  publishState(state);
  switch (state) {
    case State::BuildingFirstMap:
      if (matcher_) {
        matcher_->requestReset();
      }
      last_published_match_stamp_.reset();
      stopMotors();
      break;
    case State::Tracking:
      break;
    case State::SensorLost: {
        stopMotors();
        std::string quiet;
        if (in.lidar_age >= state_machine_config_.sensor_timeout) {
          quiet += "/scan (last one " + describeAge(in.lidar_age) + " ago)";
        }
        if (in.imu_age >= state_machine_config_.sensor_timeout) {
          quiet += std::string(quiet.empty() ? "" : " and ") + "/imu/data_raw (last one " +
            describeAge(in.imu_age) + " ago)";
        }
        RCLCPP_WARN(get_logger(), "motors stopped: no %s; the map restarts once it's back", quiet.c_str());
        break;
      }
    case State::Stopped:
      stopMotors();
      break;
  }
}

void BrainNode::runState(State state, const FrameInputs & in)
{
  // BuildingFirstMap and SensorLost need nothing every frame: the motors were stopped
  // on entry, and the motor driver's timeout keeps them stopped while we send nothing.
  if (state != State::Tracking || !matcher_) {
    return;
  }
  const auto result = matcher_->latestResult();
  if (result && result->stamp != last_published_match_stamp_) {
    publishPose(*result);
    last_published_match_stamp_ = result->stamp;
  }
  if (in.match_age > state_machine_config_.match_timeout) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "no good scan match for %.1f s, so /robot_pose is stale (turning too fast?)", in.match_age);
  }
  // Later: driving decisions go here, publishing /cmd_vel based on the pose.
}

void BrainNode::publishPose(const MatchResult & result)
{
  geometry_msgs::msg::PoseStamped msg;
  msg.header.stamp = rclcpp::Time(static_cast<int64_t>(std::llround(result.stamp * 1e9)), get_clock()->get_clock_type());
  msg.header.frame_id = map_frame_;
  msg.pose.position.x = result.pose.x;
  msg.pose.position.y = result.pose.y;
  // A turn of yaw around z, as a quaternion.
  msg.pose.orientation.z = std::sin(result.pose.yaw / 2.0);
  msg.pose.orientation.w = std::cos(result.pose.yaw / 2.0);
  pose_pub_->publish(msg);
}

void BrainNode::publishState(State state)
{
  std_msgs::msg::String msg;
  msg.data = toString(state);
  state_pub_->publish(msg);
}

void BrainNode::stopMotors()
{
  // All zeros. The motor driver also stops by itself 0.5 s after commands stop; this is
  // the polite way and the timeout is the backup.
  cmd_vel_pub_->publish(geometry_msgs::msg::Twist());
}

void BrainNode::updateDisplay(Rgb frame)
{
  const auto t = Clock::now();
  if (t < next_display_) {
    return;
  }
  next_display_ = t + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / display_rate_));
  display_->show(matcher_ ? matcher_->latestMapImage() : nullptr, frame);
  last_display_time_ = std::chrono::duration<double>(Clock::now() - t).count();
  if (!display_->isOpen()) {
    RCLCPP_INFO(get_logger(), "map window %s; the brain keeps running", display_->error().c_str());
    display_.reset();
  }
}

void BrainNode::logStats()
{
  const auto t = Clock::now();
  const double dt = std::chrono::duration<double>(t - last_stats_time_).count();
  const double fps = dt > 0.0 ? static_cast<double>(frames_) / dt : 0.0;
  last_stats_time_ = t;
  frames_ = 0;
  if (!matcher_) {
    return;  // still waiting for the drivers, which reports itself
  }
  const MatcherStats m = matcher_->stats();
  char screen[48] = "";
  if (display_) {
    std::snprintf(screen, sizeof(screen), "; screen frame %.1f ms", last_display_time_ * 1e3);
  }
  RCLCPP_INFO(get_logger(), "%s: %.0f ticks/s; %lu scans, %lu IMU readings; matcher %lu matches, "
    "%lu failures, %lu skipped, %lu overruns, last solve %.1f ms (totals)%s",
    toString(state_machine_.state()), fps,
    static_cast<unsigned long>(sensors_.lidarCount()), static_cast<unsigned long>(sensors_.imuCount()),
    static_cast<unsigned long>(m.matches), static_cast<unsigned long>(m.failures),
    static_cast<unsigned long>(m.skipped), static_cast<unsigned long>(m.overruns), m.last_solve_time * 1e3, screen);
}

}  // namespace prowl_brain
