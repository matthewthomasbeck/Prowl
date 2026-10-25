#include "prowl_brain/state_machine.hpp"

namespace prowl_brain
{

const char * toString(State state)
{
  switch (state) {
    case State::BuildingFirstMap: return "BuildingFirstMap";
    case State::Tracking: return "Tracking";
    case State::SensorLost: return "SensorLost";
    case State::Stopped: return "Stopped";
  }
  return "?";
}

StateMachine::StateMachine(const StateMachineConfig & config)
: config_(config)
{
}

State StateMachine::step(const FrameInputs & in)
{
  if (!started_) {
    // The first frame is the moment we enter BuildingFirstMap: keep just_changed_ and
    // switch nothing, so brain_node runs its entry actions even if a match is already in.
    started_ = true;
    entered_at_ = in.now;
    if (!in.shutdown_requested) {
      return state_;
    }
  }
  just_changed_ = false;

  if (in.shutdown_requested) {
    if (state_ != State::Stopped) {
      enter(State::Stopped, in.now);
    }
    return state_;
  }

  State next = state_;
  switch (state_) {
    case State::BuildingFirstMap: next = fromBuildingFirstMap(in); break;
    case State::Tracking: next = fromTracking(in); break;
    case State::SensorLost: next = fromSensorLost(in); break;
    case State::Stopped: break;  // there's no coming back
  }
  if (next != state_) {
    enter(next, in.now);
  }
  return state_;
}

double StateMachine::timeInState(double now) const
{
  return now - entered_at_;
}

State StateMachine::fromBuildingFirstMap(const FrameInputs & in) const
{
  if (!sensorsHealthy(in)) {
    return State::SensorLost;
  }
  if (in.have_match) {
    return State::Tracking;
  }
  return state_;
}

State StateMachine::fromTracking(const FrameInputs & in) const
{
  if (!sensorsHealthy(in)) {
    return State::SensorLost;
  }
  // A matcher that keeps failing (in.match_age > match_timeout) doesn't change the state:
  // nothing drives from the pose yet, and rebuilding would throw the map away. brain_node
  // warns about it instead. Once the brain drives by itself, this should stop the robot.
  return state_;
}

State StateMachine::fromSensorLost(const FrameInputs & in) const
{
  // Start fresh once both are back: the robot may have been pushed while we were blind.
  if (sensorsHealthy(in)) {
    return State::BuildingFirstMap;
  }
  return state_;
}

bool StateMachine::sensorsHealthy(const FrameInputs & in) const
{
  // The motor driver doesn't count: if it's missing, the robot is already stopped.
  return in.lidar_age < config_.sensor_timeout && in.imu_age < config_.sensor_timeout;
}

void StateMachine::enter(State next, double now)
{
  state_ = next;
  entered_at_ = now;
  just_changed_ = true;
}

}  // namespace prowl_brain
