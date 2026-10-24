// Terminal test panel for the motor driver. Drives the four wheels directly
// through MotorChannel with no ROS2 involved, and works over SSH.

#include <curses.h>
#include <pigpiod_if2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>

#include "prowl_motor_driver/motor_channel.hpp"

using prowl_motor_driver::ChannelPins;
using prowl_motor_driver::MotorChannel;

namespace
{

// Mirrors config/motor_driver.yaml. EN is hardwired HIGH, so it isn't driven.
struct WheelConfig
{
  const char * name;
  ChannelPins pins;
};

constexpr std::array<WheelConfig, 4> kWheels = {{
  {"front_left", {-1, 20, 21}},
  {"rear_left", {-1, 5, 6}},
  {"front_right", {-1, 17, 27}},
  {"rear_right", {-1, 22, 23}},
}};

constexpr int kAllRow = static_cast<int>(kWheels.size());
constexpr int kRows = kAllRow + 1;
constexpr double kStep = 0.05;
constexpr double kMaxDuty = 0.98;
constexpr unsigned kPwmFrequency = 2000;
constexpr int kCtrlC = 3;

enum class Mode { Drive, Brake, Coast };

struct Wheel
{
  std::unique_ptr<MotorChannel> motor;
  double speed = 0.0;
  bool inverted = false;
  Mode mode = Mode::Drive;
};

volatile std::sig_atomic_t g_quit = 0;

void onSignal(int)
{
  g_quit = 1;
}

void apply(Wheel & w)
{
  switch (w.mode) {
    case Mode::Drive:
      w.motor->setSpeed(w.inverted ? -w.speed : w.speed);
      break;
    case Mode::Brake:
      w.motor->brake();
      break;
    case Mode::Coast:
      w.motor->coast();
      break;
  }
}

// Steps in exact multiples of kStep so repeated presses don't drift.
void nudge(Wheel & w, double delta)
{
  w.mode = Mode::Drive;
  w.speed = std::clamp(std::round((w.speed + delta) / kStep) * kStep, -1.0, 1.0);
}

const char * stateLabel(const Wheel & w)
{
  switch (w.mode) {
    case Mode::Brake:
      return "BRAKE";
    case Mode::Coast:
      return "COAST";
    case Mode::Drive:
      break;
  }
  return w.speed > 0.0 ? "FWD" : w.speed < 0.0 ? "REV" : "STOP";
}

// Centered bar: fills right of the midline for forward, left for reverse.
void drawBar(int y, int x, double speed)
{
  constexpr int kHalf = 20;
  const int fill = static_cast<int>(std::lround(std::abs(speed) * kHalf));
  const int color = speed > 0.0 ? 1 : 2;
  mvaddch(y, x, '[');
  for (int i = -kHalf; i <= kHalf; ++i) {
    const bool lit = (speed > 0.0 && i > 0 && i <= fill) || (speed < 0.0 && i < 0 && -i <= fill);
    if (i == 0) {
      addch('|');
    } else if (lit) {
      attron(COLOR_PAIR(color));
      addch('#');
      attroff(COLOR_PAIR(color));
    } else {
      addch(' ');
    }
  }
  addch(']');
}

void draw(const std::array<Wheel, 4> & wheels, int selected, bool dry_run)
{
  erase();
  attron(A_BOLD);
  mvprintw(0, 1, "Prowl motor test");
  attroff(A_BOLD);
  attron(COLOR_PAIR(dry_run ? 3 : 1));
  printw("   %s", dry_run ? "DRY RUN - no GPIO" : "LIVE - pigpiod connected");
  attroff(COLOR_PAIR(dry_run ? 3 : 1));

  mvprintw(2, 3, "%-12s %-7s %-43s %-6s %s", "wheel", "pins", "speed", "", "state");
  for (int i = 0; i < kAllRow; ++i) {
    const Wheel & w = wheels[i];
    const int y = 3 + i;
    if (i == selected) {
      attron(A_REVERSE);
    }
    mvprintw(y, 1, "%c %-12s %2d/%-4d", i == selected ? '>' : ' ', kWheels[i].name,
      kWheels[i].pins.in1, kWheels[i].pins.in2);
    attroff(A_REVERSE);
    drawBar(y, 24, w.speed);
    printw(" %+4ld%%  %-5s %s", std::lround(w.speed * 100), stateLabel(w), w.inverted ? "INV" : "");
  }
  if (selected == kAllRow) {
    attron(A_REVERSE);
  }
  mvprintw(3 + kAllRow, 1, "%c %-19s", selected == kAllRow ? '>' : ' ', "ALL WHEELS");
  attroff(A_REVERSE);

  const int help = 5 + kAllRow;
  mvprintw(help, 1, "Up/Down  select         Left/Right  speed -/+ 5%%    -  flip direction");
  mvprintw(help + 1, 1, "Space    stop selected  b  brake   c  coast          i  toggle inverted");
  mvprintw(help + 2, 1, "x        STOP ALL       q / Ctrl-C  quit (all motors coast)");
  refresh();
}

template<typename F>
void forSelected(std::array<Wheel, 4> & wheels, int selected, F f)
{
  if (selected == kAllRow) {
    for (Wheel & w : wheels) {
      f(w);
    }
  } else {
    f(wheels[selected]);
  }
}

int run(int pi, bool dry_run)
{
  // Destroyed before returning, which coasts every motor.
  std::array<Wheel, 4> wheels;
  for (size_t i = 0; i < wheels.size(); ++i) {
    wheels[i].motor = std::make_unique<MotorChannel>(pi, kWheels[i].pins, false, kMaxDuty);
    if (!wheels[i].motor->init(kPwmFrequency)) {
      std::fprintf(stderr, "failed to configure GPIO for %s\n", kWheels[i].name);
      return 1;
    }
  }

  initscr();
  raw();  // Ctrl-C/Ctrl-Z arrive as keys, so the motors can't be left running by a suspend
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(100);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_RED, -1);
    init_pair(3, COLOR_YELLOW, -1);
  }

  int selected = 0;
  while (!g_quit) {
    draw(wheels, selected, dry_run);
    const int key = getch();
    switch (key) {
      case KEY_UP:
        selected = (selected + kRows - 1) % kRows;
        break;
      case KEY_DOWN:
        selected = (selected + 1) % kRows;
        break;
      case KEY_RIGHT:
        forSelected(wheels, selected, [](Wheel & w) {nudge(w, kStep);});
        break;
      case KEY_LEFT:
        forSelected(wheels, selected, [](Wheel & w) {nudge(w, -kStep);});
        break;
      case '-':
        forSelected(wheels, selected, [](Wheel & w) {w.mode = Mode::Drive; w.speed = -w.speed;});
        break;
      case ' ':
        forSelected(wheels, selected, [](Wheel & w) {w.mode = Mode::Drive; w.speed = 0.0;});
        break;
      case 'b':
        forSelected(wheels, selected, [](Wheel & w) {w.mode = Mode::Brake; w.speed = 0.0;});
        break;
      case 'c':
        forSelected(wheels, selected, [](Wheel & w) {w.mode = Mode::Coast; w.speed = 0.0;});
        break;
      case 'i':
        forSelected(wheels, selected, [](Wheel & w) {w.inverted = !w.inverted;});
        break;
      case 'x':
        for (Wheel & w : wheels) {
          w.mode = Mode::Drive;
          w.speed = 0.0;
        }
        break;
      case 'q':
      case kCtrlC:
        g_quit = 1;
        break;
      default:
        continue;
    }
    for (Wheel & w : wheels) {
      apply(w);
    }
  }

  endwin();
  return 0;
}

}  // namespace

int main(int argc, char ** argv)
{
  bool dry_run = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dry-run") == 0) {
      dry_run = true;
    } else {
      std::fprintf(stderr, "usage: %s [--dry-run]\n", argv[0]);
      return 2;
    }
  }

  // Stop cleanly if the SSH session drops or the process is asked to exit.
  std::signal(SIGHUP, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGINT, onSignal);

  int pi = -1;
  if (!dry_run) {
    pi = pigpio_start(nullptr, nullptr);
    if (pi < 0) {
      std::fprintf(stderr, "could not connect to pigpiod (is the daemon running?)\n");
      return 1;
    }
  }

  const int code = run(pi, dry_run);
  if (pi >= 0) {
    pigpio_stop(pi);
  }
  return code;
}
