#include <pigpiod_if2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>

#include "prowl_motor_driver/motor_channel.hpp"

namespace prowl_motor_driver
{

// Wheel order used for parameters, internal arrays, and the status message.
constexpr std::array<const char *, 4> kWheelNames = {
  "front_left", "rear_left", "front_right", "rear_right"};

// Owns the connection to the pigpio daemon. Skips connecting in dry-run mode.
class PigpioSession
{
public:
  explicit PigpioSession(bool connect)
  : pi_(connect ? pigpio_start(nullptr, nullptr) : -1)
  {
    if (connect && pi_ < 0) {
      throw std::runtime_error("could not connect to pigpiod (is the daemon running?)");
    }
  }

  ~PigpioSession()
  {
    if (pi_ >= 0) {
      pigpio_stop(pi_);
    }
  }

  PigpioSession(const PigpioSession &) = delete;
  PigpioSession & operator=(const PigpioSession &) = delete;

  int handle() const {return pi_;}

private:
  int pi_;
};

// Drives the four motors from either /cmd_vel (whole-robot motion, mixed into
// mecanum wheel duties) or /wheel_cmd (one signed duty per wheel); the most
// recent message on either topic wins. Open loop: there are no encoders, so
// speed is a fraction of max_linear_speed mapped straight to duty cycle.
class MotorDriverNode : public rclcpp::Node
{
public:
  MotorDriverNode()
  : Node("motor_driver"),
    session_(!declare_parameter("dry_run", false))
  {
    wheel_separation_ = declare_parameter("wheel_separation", 0.3);
    wheel_base_ = declare_parameter("wheel_base", 0.3);
    max_linear_speed_ = declare_parameter("max_linear_speed", 1.0);
    max_accel_ = declare_parameter("max_accel", 2.0);
    cmd_timeout_ = declare_parameter("cmd_timeout", 0.5);
    const double max_duty = declare_parameter("max_duty", 0.98);
    const auto pwm_frequency = declare_parameter<int64_t>("pwm_frequency", 2000);
    const double loop_rate = declare_parameter("loop_rate", 50.0);
    period_ = 1.0 / loop_rate;

    const bool dry_run = session_.handle() < 0;
    for (const std::string wheel : kWheelNames) {
      const ChannelPins pins{
        static_cast<int>(declare_parameter<int64_t>(wheel + ".en", -1)),
        static_cast<int>(declare_parameter<int64_t>(wheel + ".in1", -1)),
        static_cast<int>(declare_parameter<int64_t>(wheel + ".in2", -1))};
      const bool inverted = declare_parameter(wheel + ".inverted", false);

      if (!dry_run && (pins.in1 < 0 || pins.in2 < 0)) {
        throw std::runtime_error(
                "pins for " + wheel + " are not set (set them in the config, or run with dry_run:=true)");
      }
      auto & motor = motors_.emplace_back(
        std::make_unique<MotorChannel>(session_.handle(), pins, inverted, max_duty));
      if (!motor->init(static_cast<unsigned>(pwm_frequency))) {
        throw std::runtime_error("failed to configure GPIO for " + wheel);
      }
    }

    last_cmd_time_ = now();
    cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", 10, [this](const geometry_msgs::msg::Twist & msg) {onCmdVel(msg);});
    wheel_cmd_sub_ = create_subscription<std_msgs::msg::Float64MultiArray>(
      "wheel_cmd", 10, [this](const std_msgs::msg::Float64MultiArray & msg) {onWheelCmd(msg);});
    status_pub_ = create_publisher<std_msgs::msg::Float64MultiArray>("motor_status", 10);
    timer_ = create_wall_timer(
      std::chrono::duration<double>(period_), [this] {controlLoop();});

    RCLCPP_INFO(get_logger(), "motor driver started%s", dry_run ? " (dry run)" : "");
  }

private:
  // Mecanum mixing (rollers in the usual X seen from above): linear.x drives
  // every wheel the same way, linear.y (left) runs the two diagonals against
  // each other, and angular.z spins the sides in opposite directions.
  void onCmdVel(const geometry_msgs::msg::Twist & msg)
  {
    const double vx = msg.linear.x;
    const double vy = msg.linear.y;
    const double turn = msg.angular.z * (wheel_separation_ + wheel_base_) / 2.0;
    std::array<double, 4> wheels = {
      vx - vy - turn,   // front_left
      vx + vy - turn,   // rear_left
      vx + vy + turn,   // front_right
      vx - vy + turn};  // rear_right

    // Scale all wheels together so the direction of travel survives saturation.
    double peak = 0.0;
    for (double & wheel : wheels) {
      wheel /= max_linear_speed_;
      peak = std::max(peak, std::abs(wheel));
    }
    if (peak > 1.0) {
      for (double & wheel : wheels) {
        wheel /= peak;
      }
    }
    target_ = wheels;
    last_cmd_time_ = now();
  }

  // Direct per-wheel control: [front_left, rear_left, front_right, rear_right],
  // each in [-1, 1] where the sign is direction and the magnitude is speed.
  void onWheelCmd(const std_msgs::msg::Float64MultiArray & msg)
  {
    if (msg.data.size() != target_.size()) {
      RCLCPP_WARN(get_logger(), "wheel_cmd needs %zu values, got %zu", target_.size(), msg.data.size());
      return;
    }
    for (size_t i = 0; i < target_.size(); ++i) {
      target_[i] = std::clamp(msg.data[i], -1.0, 1.0);
    }
    last_cmd_time_ = now();
  }

  void controlLoop()
  {
    // Watchdog: stop if the command stream goes quiet.
    if ((now() - last_cmd_time_).seconds() > cmd_timeout_) {
      target_.fill(0.0);
    }

    // Slew-rate limit so direction changes pass through zero gradually.
    const double max_step = max_accel_ * period_;
    std_msgs::msg::Float64MultiArray status;
    for (size_t i = 0; i < motors_.size(); ++i) {
      const double current = motors_[i]->speed();
      const double next = current + std::clamp(target_[i] - current, -max_step, max_step);
      motors_[i]->setSpeed(next);
      status.data.push_back(next);
    }
    status_pub_->publish(status);
  }

  // Declared before motors_ so it outlives them: motors coast on destruction.
  PigpioSession session_;
  std::vector<std::unique_ptr<MotorChannel>> motors_;
  std::array<double, 4> target_{};

  double wheel_separation_;
  double wheel_base_;
  double max_linear_speed_;
  double max_accel_;
  double cmd_timeout_;
  double period_;
  rclcpp::Time last_cmd_time_;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr wheel_cmd_sub_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace prowl_motor_driver

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<prowl_motor_driver::MotorDriverNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("motor_driver"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
