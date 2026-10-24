#pragma once

namespace prowl_motor_driver
{

// BCM GPIO numbers wired to one channel of a DBH-12V H-bridge.
// en may be -1 when EN is hardwired HIGH instead of driven by the Pi.
struct ChannelPins
{
  int en;
  int in1;
  int in2;
};

// One motor on one H-bridge channel, driven through the pigpio daemon.
//
// DBH-12V control logic (from the manual):
//   EN LOW                 -> coast
//   EN HIGH, IN1 PWM, IN2 LOW -> forward
//   EN HIGH, IN1 LOW, IN2 PWM -> reverse
//   EN HIGH, IN1 HIGH, IN2 HIGH -> short brake
//
// Speed is a signed duty cycle in [-1, 1]. A negative pigpio handle runs in
// dry-run mode: state is tracked but no GPIO is touched.
class MotorChannel
{
public:
  MotorChannel(int pi, ChannelPins pins, bool inverted, double max_duty);
  ~MotorChannel();

  MotorChannel(const MotorChannel &) = delete;
  MotorChannel & operator=(const MotorChannel &) = delete;

  bool init(unsigned pwm_frequency);
  void setSpeed(double speed);
  void brake();
  void coast();

  double speed() const {return speed_;}

private:
  bool live() const {return pi_ >= 0;}
  void writeDuty(int gpio, double duty);
  void writeEnable(unsigned level);

  int pi_;
  ChannelPins pins_;
  bool inverted_;
  double max_duty_;
  double speed_ = 0.0;
};

}  // namespace prowl_motor_driver
