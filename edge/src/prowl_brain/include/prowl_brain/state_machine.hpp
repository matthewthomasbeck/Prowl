#pragma once

namespace prowl_brain
{

// A state machine is a list of "moods" the robot can be in, plus rules for
// when to switch between them. Like a traffic light: it's always exactly one
// of green, yellow or red, and there are clear rules for what comes next.
// Keeping it this explicit means the robot can never be "half driving, half
// stopped": it's in one state, and that state decides what it does.
//
// The drivers are already up by the time this starts (brain_node waits for
// them at startup), so these states are about what happens after that.
//
//   BuildingFirstMap --first good match--> Tracking
//          ^                                  |
//          |                     LiDAR or IMU goes quiet
//          |                                  v
//          +------sensors back, map reset---SensorLost
//
//   Any state --shutdown requested--> Stopped
enum class State
{
  BuildingFirstMap,  // stand still and collect the first scans so the matcher has a map to match against
  Tracking,          // normal running: the matcher is updating our pose 10 times a second
  SensorLost,        // the LiDAR or IMU stopped talking: stop the motors and wait for it
  Stopped,           // shutting down: motors stopped, nothing more happens
};

// "Tracking" etc., for logs and the /brain_state topic.
const char * toString(State state);

// Everything the state machine looks at to make its decision, gathered fresh
// by thread 1 every single frame. It's a snapshot: the state machine never
// reaches out to grab data itself, which keeps it easy to test (just hand it
// made-up inputs and check which state comes out).
struct FrameInputs
{
  double now = 0.0;                 // s
  double lidar_age = 1e9;           // s since the newest LiDAR spin (huge = never got one)
  double imu_age = 1e9;             // s since the newest IMU reading
  double motor_status_age = 1e9;    // s since the motor driver last said "I'm alive"
  bool have_match = false;          // has the matcher produced at least one good pose?
  double match_age = 1e9;           // s since the newest good match
  bool shutdown_requested = false;  // Ctrl-C or the node is being destroyed
};

// The knobs for when to switch states. Loaded from config/brain.yaml.
struct StateMachineConfig
{
  double sensor_timeout = 0.5;  // s of silence from the LiDAR or IMU before we call it lost
  double match_timeout = 1.0;   // s without a good match while Tracking before we worry
};

class StateMachine
{
public:
  explicit StateMachine(const StateMachineConfig & config);

  // Called once per frame by thread 1. Looks at the inputs, maybe switches
  // state, and returns the state we're in now.
  State step(const FrameInputs & in);

  State state() const {return state_;}
  // True on the one frame right after a switch, so brain_node can do one-off
  // things like logging the change or resetting the map.
  bool justChanged() const {return just_changed_;}
  // s since we entered the current state.
  double timeInState(double now) const;

private:
  // One "what next?" rule per state. Each returns the state to be in next
  // (often the same one).
  State fromBuildingFirstMap(const FrameInputs & in) const;
  State fromTracking(const FrameInputs & in) const;
  State fromSensorLost(const FrameInputs & in) const;

  // Are both sensors talking? Used by several rules above.
  bool sensorsHealthy(const FrameInputs & in) const;
  // Switch to `next`, remembering when it happened.
  void enter(State next, double now);

  StateMachineConfig config_;
  State state_ = State::BuildingFirstMap;
  double entered_at_ = 0.0;
  bool just_changed_ = true;  // the very first frame counts as "just entered BuildingFirstMap"
  bool started_ = false;      // has step() run yet? (the first one keeps just_changed_)
};

}  // namespace prowl_brain
