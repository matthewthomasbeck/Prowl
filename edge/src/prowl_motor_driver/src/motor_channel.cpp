#include "prowl_motor_driver/motor_channel.hpp"

#include <pigpiod_if2.h>

#include <algorithm>
#include <cmath>

namespace prowl_motor_driver
{

// pigpio maps this logical range onto whatever real resolution the chosen
// PWM frequency allows.
constexpr unsigned kPwmRange = 1000;

MotorChannel::MotorChannel(int pi, ChannelPins pins, bool inverted, double max_duty)
: pi_(pi), pins_(pins), inverted_(inverted), max_duty_(std::clamp(max_duty, 0.0, 1.0))
{
}

// pigpiod keeps driving pins after we disconnect, so always leave the motor coasting.
MotorChannel::~MotorChannel()
{
  coast();
}

bool MotorChannel::init(unsigned pwm_frequency)
{
  if (!live()) {
    return true;
  }
  for (int gpio : {pins_.en, pins_.in1, pins_.in2}) {
    if (gpio < 0) {
      continue;
    }
    if (set_mode(pi_, gpio, PI_OUTPUT) < 0 || gpio_write(pi_, gpio, 0) < 0) {
      return false;
    }
  }
  for (int gpio : {pins_.in1, pins_.in2}) {
    if (set_PWM_frequency(pi_, gpio, pwm_frequency) < 0 || set_PWM_range(pi_, gpio, kPwmRange) < 0) {
      return false;
    }
  }
  return true;
}

void MotorChannel::setSpeed(double speed)
{
  speed_ = std::clamp(speed, -1.0, 1.0);
  if (!live()) {
    return;
  }
  const double duty = std::min(std::abs(speed_), max_duty_);
  const bool forward = (speed_ >= 0.0) != inverted_;
  const int drive_pin = forward ? pins_.in1 : pins_.in2;
  const int idle_pin = forward ? pins_.in2 : pins_.in1;

  // Drop the idle side first so both inputs are never PWMing at once.
  writeDuty(idle_pin, 0.0);
  writeDuty(drive_pin, duty);
  writeEnable(1);
}

void MotorChannel::brake()
{
  speed_ = 0.0;
  if (!live()) {
    return;
  }
  writeEnable(1);
  gpio_write(pi_, pins_.in1, 1);
  gpio_write(pi_, pins_.in2, 1);
}

// Without an EN pin this is a zero-duty stop rather than a true coast.
void MotorChannel::coast()
{
  speed_ = 0.0;
  if (!live()) {
    return;
  }
  writeEnable(0);
  gpio_write(pi_, pins_.in1, 0);
  gpio_write(pi_, pins_.in2, 0);
}

void MotorChannel::writeEnable(unsigned level)
{
  if (pins_.en >= 0) {
    gpio_write(pi_, pins_.en, level);
  }
}

void MotorChannel::writeDuty(int gpio, double duty)
{
  set_PWM_dutycycle(pi_, gpio, static_cast<unsigned>(std::lround(duty * kPwmRange)));
}

}  // namespace prowl_motor_driver
