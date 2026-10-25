#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "prowl_brain/brain_node.hpp"

namespace
{

// Raised by Ctrl-C (SIGINT) or SIGTERM. Only a lock-free atomic is safe to touch in a signal handler.
std::atomic<bool> g_interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free, "the signal handler needs a lock-free flag");

void onSignal(int)
{
  g_interrupted = true;
}

}  // namespace

// Where the brain starts. The order of things here IS the architecture:
//
//   1. Start ROS and build the node.
//   2. Initialize: open the map window on the robot's screen (if there is one), then wait
//      until every driver (LiDAR, IMU, motors) is talking.
//   3. Start thread 2 (the 10 Hz scan matcher).
//   4. Become thread 1: loop "deliver messages, tick the state machine" as fast as possible.
//   5. On Ctrl-C: tick until Stopped (motors stopped), stop thread 2, shut down.
int main(int argc, char ** argv)
{
  // Our own signal handler instead of rclcpp's: rclcpp's shuts ROS down straight away,
  // before the brain can send its last zero /cmd_vel. Ours only raises a flag; the loop
  // below stops the robot, and ROS shuts down after that.
  rclcpp::init(argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  int code = 0;
  try {
    auto node = std::make_shared<prowl_brain::BrainNode>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);

    node->openDisplay();
    if (node->waitForDrivers(executor, g_interrupted)) {
      node->startMatcher();
      // spin_once runs at most one waiting callback and returns, or naps until one arrives
      // or 5 ms pass: the loop keeps up with the data without spinning a core on nothing.
      while (!node->stopped()) {
        if (g_interrupted || !rclcpp::ok()) {
          node->requestShutdown();  // the next tick() enters Stopped and sends a zero /cmd_vel
        }
        executor.spin_once(std::chrono::milliseconds(5));
        node->tick();
      }
      // Give DDS a moment to deliver that zero /cmd_vel before everything is torn down.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else if (!g_interrupted) {
      code = 1;
    }
    executor.remove_node(node);
    node.reset();  // the destructor stops thread 2
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("brain"), "%s", e.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
