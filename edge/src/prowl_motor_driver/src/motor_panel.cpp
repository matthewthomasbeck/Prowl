// Terminal control panel for the motor driver over ROS2. Starts and stops
// motor_driver_node as a child process, drives the wheels through /wheel_cmd,
// and shows /motor_status live. Works over SSH.

#include <curses.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <ament_index_cpp/get_package_prefix.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>

namespace
{

using Clock = std::chrono::steady_clock;

// Same order as the driver's /wheel_cmd and /motor_status arrays.
constexpr std::array<const char *, 4> kWheelNames = {
  "front_left", "rear_left", "front_right", "rear_right"};
using Duties = std::array<double, kWheelNames.size()>;

constexpr auto kTick = std::chrono::milliseconds(50);  // redraw and publish at ~20 Hz
constexpr auto kTestDuration = std::chrono::seconds(3);
// Hold-to-drive: a drive key keeps the wheels turning only while the terminal's
// key repeat keeps re-sending it, so letting go (or losing the connection)
// stops them. Terminals wait up to ~0.66 s before they start repeating, so the
// first press gets a longer window than the repeats that follow it.
constexpr auto kFirstPressHold = std::chrono::milliseconds(700);
constexpr auto kRepeatHold = std::chrono::milliseconds(300);
constexpr auto kStatusStale = std::chrono::milliseconds(500);
constexpr auto kMessageTime = std::chrono::seconds(3);
constexpr double kSpeedStep = 0.05;
constexpr int kBarHalf = 10;
constexpr size_t kLogKeep = 50;
constexpr int kCtrlC = 3;

// Steps in exact multiples of kSpeedStep so repeated presses don't drift.
double stepSpeed(double speed, int steps)
{
  return std::clamp(std::round(speed / kSpeedStep + steps) * kSpeedStep, kSpeedStep, 1.0);
}

// "[     ####|          ]" with the fill growing left of center for reverse.
std::string bar(double value)
{
  std::string s(2 * kBarHalf + 1, ' ');
  s[kBarHalf] = '|';
  const int cells = static_cast<int>(std::lround(std::min(std::abs(value), 1.0) * kBarHalf));
  for (int i = 1; i <= cells; ++i) {
    s[kBarHalf + (value > 0 ? i : -i)] = '#';
  }
  return "[" + s + "]";
}

// motor_driver_node run as a child process, with its output captured for display.
class DriverProcess
{
public:
  DriverProcess() = default;
  ~DriverProcess() {stop();}

  DriverProcess(const DriverProcess &) = delete;
  DriverProcess & operator=(const DriverProcess &) = delete;

  bool running() const {return pid_ > 0;}
  bool dryRun() const {return dry_run_;}
  const std::string & lastExit() const {return last_exit_;}
  const std::deque<std::string> & log() const {return log_;}

  void start(bool dry_run)
  {
    if (running()) {
      return;
    }
    std::vector<std::string> args;
    try {
      args = {
        ament_index_cpp::get_package_prefix("prowl_motor_driver") +
        "/lib/prowl_motor_driver/motor_driver_node",
        "--ros-args", "--params-file",
        ament_index_cpp::get_package_share_directory("prowl_motor_driver") +
        "/config/motor_driver.yaml",
        "-p", dry_run ? "dry_run:=true" : "dry_run:=false"};
    } catch (const std::exception & e) {
      addLine(std::string("cannot find motor_driver_node: ") + e.what());
      return;
    }
    std::vector<char *> argv;
    for (auto & arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) < 0) {
      addLine("pipe() failed");
      return;
    }
    const pid_t pid = fork();
    if (pid < 0) {
      close(fds[0]);
      close(fds[1]);
      addLine("fork() failed");
      return;
    }
    if (pid == 0) {
      // Child: only async-signal-safe calls until exec.
      prctl(PR_SET_PDEATHSIG, SIGTERM);  // never outlive the panel, so motors can't be orphaned
      setpgid(0, 0);  // keep terminal signals away from it
      const int devnull = open("/dev/null", O_RDONLY);
      dup2(devnull, STDIN_FILENO);
      dup2(fds[1], STDOUT_FILENO);
      dup2(fds[1], STDERR_FILENO);
      close(fds[0]);
      close(fds[1]);
      execv(argv[0], argv.data());
      _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    pid_ = pid;
    out_fd_ = fds[0];
    dry_run_ = dry_run;
    stop_requested_ = false;
    last_exit_.clear();
  }

  // SIGTERM lets the node shut down normally, which leaves every motor coasting.
  void stop()
  {
    if (!running()) {
      return;
    }
    stop_requested_ = true;
    kill(pid_, SIGTERM);
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (running() && Clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      poll();
    }
    if (running()) {
      kill(pid_, SIGKILL);
      waitpid(pid_, nullptr, 0);
      finish("killed (did not stop within 3 s)");
    }
  }

  // Call every tick: collects output and notices if the node exited by itself.
  void poll()
  {
    if (!running()) {
      return;
    }
    drain();
    int status = 0;
    if (waitpid(pid_, &status, WNOHANG) != pid_) {
      return;
    }
    if (stop_requested_) {
      finish("");
    } else if (WIFEXITED(status)) {
      finish("exited with code " + std::to_string(WEXITSTATUS(status)));
    } else {
      finish("killed by signal " + std::to_string(WTERMSIG(status)));
    }
  }

private:
  void drain()
  {
    char buf[512];
    ssize_t n;
    while ((n = read(out_fd_, buf, sizeof(buf))) > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\n') {
          addLine(partial_);
          partial_.clear();
        } else {
          partial_ += buf[i];
        }
      }
    }
  }

  void finish(const std::string & how)
  {
    drain();
    if (!partial_.empty()) {
      addLine(partial_);
      partial_.clear();
    }
    close(out_fd_);
    out_fd_ = -1;
    pid_ = -1;
    last_exit_ = how;
  }

  void addLine(const std::string & line)
  {
    log_.push_back(line);
    if (log_.size() > kLogKeep) {
      log_.pop_front();
    }
  }

  pid_t pid_ = -1;
  int out_fd_ = -1;
  bool dry_run_ = true;
  bool stop_requested_ = false;
  std::string last_exit_;
  std::string partial_;
  std::deque<std::string> log_;
};

// Puts the terminal in curses mode for its lifetime.
struct Screen
{
  Screen()
  {
    initscr();
    raw();  // Ctrl-C/Ctrl-Z arrive as keys, so the motors can't be left running by a suspend
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(static_cast<int>(kTick.count()));
  }
  ~Screen() {endwin();}
};

class Panel
{
public:
  explicit Panel(rclcpp::Node::SharedPtr node)
  : node_(std::move(node)),
    wheel_cmd_pub_(node_->create_publisher<std_msgs::msg::Float64MultiArray>("wheel_cmd", 10)),
    status_sub_(node_->create_subscription<std_msgs::msg::Float64MultiArray>(
        "motor_status", 10, [this](const std_msgs::msg::Float64MultiArray & msg) {
          if (msg.data.size() == status_.size()) {
            std::copy(msg.data.begin(), msg.data.end(), status_.begin());
            last_status_ = Clock::now();
          }
        }))
  {}

  ~Panel()
  {
    stopMotion();
    driver_.stop();
  }

  void run()
  {
    Screen screen;
    while (!quit_ && rclcpp::ok()) {
      const int key = getch();  // waits up to one tick
      if (key != ERR) {
        handleKey(key);
      }
      rclcpp::spin_some(node_);
      driver_.poll();
      publish();
      draw();
    }
  }

private:
  enum class Command { Idle, Drive, Test };

  void handleKey(int key)
  {
    switch (key) {
      // Wheel directions in /wheel_cmd order (front_left, rear_left,
      // front_right, rear_right), the same mecanum mixing the driver uses.
      case 'w': case 'W': case KEY_UP:
        drive({1.0, 1.0, 1.0, 1.0}, "forward");
        break;
      case 's': case 'S': case KEY_DOWN:
        drive({-1.0, -1.0, -1.0, -1.0}, "backward");
        break;
      case 'a': case 'A':
        drive({-1.0, 1.0, 1.0, -1.0}, "strafe left");
        break;
      case 'd': case 'D':
        drive({1.0, -1.0, -1.0, 1.0}, "strafe right");
        break;
      case 'z': case 'Z': case KEY_LEFT:
        drive({-1.0, -1.0, 1.0, 1.0}, "turn left");
        break;
      case 'c': case 'C': case KEY_RIGHT:
        drive({1.0, 1.0, -1.0, -1.0}, "turn right");
        break;
      case '1': case '2': case '3': case '4': {
        const size_t wheel = static_cast<size_t>(key - '1');
        Duties duties{};
        duties[wheel] = speed_;
        startTest(duties, kWheelNames[wheel]);
        break;
      }
      case 't': case 'T': {
        Duties duties;
        duties.fill(speed_);
        startTest(duties, "all wheels");
        break;
      }
      case '+': case '=':
        speed_ = stepSpeed(speed_, 1);
        break;
      case '-': case '_':
        speed_ = stepSpeed(speed_, -1);
        break;
      case ' ':
        stopMotion();
        break;
      case 'n': case 'N':
        toggleNode();
        break;
      case 'l': case 'L':
        if (driver_.running()) {
          say("stop the driver (N) before switching live/dry run");
        } else {
          dry_run_ = !dry_run_;
        }
        break;
      case 'q': case 'Q': case kCtrlC:
        quit_ = true;
        break;
    }
  }

  // directions: +1/-1 per wheel, scaled by speed_.
  void drive(const Duties & directions, const char * label)
  {
    Duties duties;
    for (size_t i = 0; i < duties.size(); ++i) {
      duties[i] = directions[i] * speed_;
    }
    const auto now = Clock::now();
    const bool repeat = command_ == Command::Drive && duties == duties_ && now < until_;
    command_ = Command::Drive;
    duties_ = duties;
    label_ = label;
    until_ = now + (repeat ? kRepeatHold : kFirstPressHold);
  }

  void startTest(const Duties & duties, const std::string & label)
  {
    command_ = Command::Test;
    duties_ = duties;
    label_ = label;
    until_ = Clock::now() + kTestDuration;
  }

  // Zero immediately rather than waiting out the driver's cmd_timeout.
  void stopMotion()
  {
    command_ = Command::Idle;
    publishWheels(Duties{});
  }

  void toggleNode()
  {
    if (driver_.running()) {
      stopMotion();
      driver_.stop();
    } else if (externalDriver()) {
      say("a motor driver is already running elsewhere; stop it first");
    } else {
      driver_.start(dry_run_);
      dry_run_ = true;  // live lasts one run: after any shutoff, N starts in dry run again
    }
  }

  bool externalDriver() const
  {
    return !driver_.running() && node_->count_publishers("motor_status") > 0;
  }

  // The driver's watchdog stops the motors unless commands keep arriving.
  void publish()
  {
    if (command_ == Command::Idle) {
      return;
    }
    if (Clock::now() >= until_) {
      stopMotion();
    } else {
      publishWheels(duties_);
    }
  }

  void publishWheels(const Duties & duties)
  {
    std_msgs::msg::Float64MultiArray msg;
    msg.data.assign(duties.begin(), duties.end());
    wheel_cmd_pub_->publish(msg);
  }

  void say(const std::string & text)
  {
    message_ = text;
    message_until_ = Clock::now() + kMessageTime;
  }

  void line(int row, const std::string & text, attr_t attr = A_NORMAL)
  {
    attron(attr);
    mvaddnstr(row, 0, text.c_str(), COLS);
    attroff(attr);
  }

  std::string nodeState() const
  {
    if (driver_.running()) {
      return driver_.dryRun() ? "RUNNING (dry run)" : "RUNNING (LIVE)";
    }
    if (externalDriver()) {
      return "RUNNING (started outside the panel)";
    }
    return driver_.lastExit().empty() ? "STOPPED" : "STOPPED - " + driver_.lastExit();
  }

  void draw()
  {
    char buf[160];
    erase();
    int row = 0;
    const bool live = driver_.running() && !driver_.dryRun();
    line(row++, " Prowl motor panel    driver: " + nodeState(), live ? A_BOLD | A_REVERSE : A_BOLD);
    ++row;

    const bool fresh = Clock::now() - last_status_ < kStatusStale;
    for (size_t i = 0; i < kWheelNames.size(); ++i) {
      if (fresh) {
        std::snprintf(buf, sizeof(buf), " %-12s %s  %+.2f", kWheelNames[i], bar(status_[i]).c_str(), status_[i]);
      } else {
        std::snprintf(buf, sizeof(buf), " %-12s %s    --", kWheelNames[i], bar(0.0).c_str());
      }
      line(row++, buf);
    }
    line(row++, fresh ? "" : " (no /motor_status - is the driver running?)");

    switch (command_) {
      case Command::Idle:
        line(row++, " command: idle");
        break;
      case Command::Drive:
        line(row++, " command: " + label_);
        break;
      case Command::Test: {
        const double left = std::chrono::duration<double>(until_ - Clock::now()).count();
        std::snprintf(buf, sizeof(buf), " command: test %s  %.1f s left", label_.c_str(), std::max(left, 0.0));
        line(row++, buf);
        break;
      }
    }
    std::snprintf(buf, sizeof(buf), " speed: %.2f", speed_);
    if (driver_.running()) {
      line(row++, buf);
    } else {
      line(row++, std::string(buf) + (dry_run_ ? "    next start: dry run" : "    next start: LIVE"),
        dry_run_ ? A_NORMAL : A_REVERSE);
    }
    line(row++, Clock::now() < message_until_ ? " ! " + message_ : "", A_BOLD);

    line(row++, " Hold to move:  W/S or Up/Down  forward/back    A/D  strafe left/right");
    line(row++, "                Z/C or Left/Right  turn left/right");
    line(row++, " SPACE stop   1-4 test one wheel 3 s   T test all wheels   +/- speed");
    line(row++, " N start/stop driver   L live/dry run (next start only)   Q quit");
    ++row;

    line(row++, " driver output:", A_DIM);
    const auto & log = driver_.log();
    const int room = std::max(LINES - row, 0);
    const size_t first = log.size() > static_cast<size_t>(room) ? log.size() - room : 0;
    for (size_t i = first; i < log.size(); ++i) {
      line(row++, "  " + log[i], A_DIM);
    }
    refresh();
  }

  rclcpp::Node::SharedPtr node_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr wheel_cmd_pub_;
  rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr status_sub_;
  DriverProcess driver_;

  Duties status_{};
  Clock::time_point last_status_{};

  // What the wheels are being told to do, and until when.
  Command command_ = Command::Idle;
  Duties duties_{};
  std::string label_;
  Clock::time_point until_{};
  double speed_ = 0.5;  // duty for driving and wheel tests

  bool dry_run_ = true;  // start safe; L arms live for the next start only
  bool quit_ = false;
  std::string message_;
  Clock::time_point message_until_{};
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  {
    Panel panel(std::make_shared<rclcpp::Node>("motor_panel"));
    panel.run();
  }
  rclcpp::shutdown();
  return 0;
}
