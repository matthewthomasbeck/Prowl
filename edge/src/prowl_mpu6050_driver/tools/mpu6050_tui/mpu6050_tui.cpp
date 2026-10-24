// Terminal monitor for the MPU6050 IMU. Talks to the sensor directly over
// /dev/i2c-N with no ROS2 involved, and works over SSH.
//
//   mpu6050_tui [bus] [address]      defaults: 1 0x68
//
// On Prowl the sensor is on I2C bus 1 (/dev/i2c-1): SDA on header pin 3
// (GPIO2), SCL on pin 5 (GPIO3); address 0x68 with AD0 low.

#include <curses.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "prowl_mpu6050_driver/conversions.hpp"
#include "prowl_mpu6050_driver/gyro_bias.hpp"
#include "prowl_mpu6050_driver/mpu6050.hpp"
#include "window_stats.hpp"

namespace
{

namespace imu = prowl_mpu6050_driver;
using prowl_mpu6050_tui::Summary;
using prowl_mpu6050_tui::WindowStats;
using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

constexpr std::array<double, 3> kRates = {50.0, 100.0, 200.0};
constexpr double kStatsWindowS = 2.0;
constexpr double kCalibrationS = 2.0;
constexpr double kStillDps = 0.3;  // spread of the 0.1 s averages above this means the robot moved
constexpr double kVibrationS = 0.1;  // one LiDAR rotation; averaging over it cancels its vibration
constexpr int kCalibrationAttempts = 3;  // then accept it anyway, flagged MOVED
constexpr auto kDrawPeriod = std::chrono::milliseconds(66);
constexpr auto kSettle = std::chrono::milliseconds(60);  // discard samples after a config change
constexpr auto kNoDataTimeout = std::chrono::seconds(1);
constexpr auto kMessageTime = std::chrono::seconds(4);
constexpr int kCtrlC = 3;
constexpr const char * kAxes = "XYZ";

volatile std::sig_atomic_t g_quit = 0;

void onSignal(int)
{
  g_quit = 1;
}

double ms(Clock::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

// Statistics channels, in display order.
enum Channel {kAx, kAy, kAz, kAmag, kGx, kGy, kGz, kChannels};

class Monitor
{
public:
  explicit Monitor(imu::Mpu6050 & mpu)
  : mpu_(mpu), cfg_(mpu.config())
  {
    resize();
    startCalibration();
  }

  void run();

private:
  void resize();
  void startCalibration();
  void onSample(const imu::Sample & s, Clock::time_point now);
  void poll(Clock::time_point now);
  void change(const imu::Config & next, const std::string & what);
  void handleKey(int key);
  void message(const std::string & text, int color);

  void draw(Clock::time_point now);
  void drawBar(int y, int x, int half, double fraction, bool clipped) const;
  void drawStat(int y, const char * label, const Summary & s, bool accel) const;

  imu::Mpu6050 & mpu_;
  imu::Config cfg_;
  imu::GyroBiasEstimator calib_;
  bool calibrating_ = false;
  bool have_bias_ = false;
  imu::Vec3 calib_std_{};
  bool calib_still_ = true;
  int calib_attempt_ = 0;

  std::array<WindowStats, kChannels> stats_;
  imu::Sample last_;
  bool have_sample_ = false;
  double yaw_ = 0.0;
  Clock::time_point last_sample_time_{};
  Clock::time_point settle_until_{};
  Clock::time_point next_poll_{};
  Clock::time_point last_reinit_{};

  Clock::time_point rate_start_ = Clock::now();
  int rate_count_ = 0;
  double rate_ = 0.0;
  unsigned long gaps_ = 0;     // intervals > 1.5 sample periods: likely a missed sample

  std::string message_;
  int message_color_ = 0;
  Clock::time_point message_time_{};
};

void Monitor::resize()
{
  const auto n = static_cast<size_t>(std::lround(kStatsWindowS * mpu_.sampleRateHz()));
  for (auto & s : stats_) {
    s.setCapacity(n);
    s.clear();
  }
  calib_ = imu::GyroBiasEstimator(static_cast<size_t>(std::lround(kCalibrationS * mpu_.sampleRateHz())),
    static_cast<size_t>(std::max(1L, std::lround(kVibrationS * mpu_.sampleRateHz()))));
}

void Monitor::startCalibration()
{
  calib_.reset();
  calibrating_ = true;
  calib_attempt_ = 0;
}

void Monitor::onSample(const imu::Sample & s, Clock::time_point now)
{
  const double period = 1.0 / mpu_.sampleRateHz();
  if (have_sample_) {
    const double dt = Seconds(now - last_sample_time_).count();
    if (dt > 1.5 * period) {
      ++gaps_;
    }
    // Integrate with the host clock so a missed sample doesn't lose angle.
    if (!calibrating_ && dt < 0.5) {
      yaw_ = std::remainder(yaw_ + s.gyro[2] * dt, 2.0 * imu::kPi);
    }
  }
  last_ = s;
  have_sample_ = true;
  last_sample_time_ = now;
  ++rate_count_;

  if (calibrating_ && calib_.add(s.gyro_uncalibrated)) {
    const bool still = calib_.still(kStillDps * imu::kDegToRad);
    if (!still && ++calib_attempt_ < kCalibrationAttempts) {
      calib_.reset();  // moved during the window: try again
      return;
    }
    mpu_.setGyroBias(calib_.bias());
    calib_std_ = calib_.blockStddev();
    calib_still_ = still;
    calibrating_ = false;
    have_bias_ = true;
    yaw_ = 0.0;
    for (auto & st : stats_) {
      st.clear();  // drop the uncorrected gyro values
    }
    if (calib_still_) {
      message("gyro bias calibrated", 1);
    } else {
      message("robot kept moving: bias is approximate, press c to redo", 3);
    }
    return;
  }

  for (int i = 0; i < 3; ++i) {
    stats_[kAx + i].add(s.accel[i]);
    stats_[kGx + i].add(s.gyro[i] * imu::kRadToDeg);
  }
  stats_[kAmag].add(imu::norm(s.accel));
}

void Monitor::poll(Clock::time_point now)
{
  if (now < next_poll_) {
    return;
  }
  const auto period = std::chrono::duration_cast<Clock::duration>(Seconds(1.0 / mpu_.sampleRateHz()));
  imu::Sample s;
  switch (mpu_.poll(s)) {
    case imu::ReadStatus::NewSample:
      if (now >= settle_until_) {
        onSample(s, now);
      } else {
        last_sample_time_ = now;
        have_sample_ = false;
      }
      // Nothing new can arrive for most of a period, so leave the bus idle.
      next_poll_ = now + period * 6 / 10;
      break;
    case imu::ReadStatus::NoNewSample:
      next_poll_ = now + std::chrono::milliseconds(1);
      break;
    case imu::ReadStatus::Error:
      next_poll_ = now + std::chrono::milliseconds(10);
      break;
  }

  // Stopped producing data (power blip, wiring): it wakes up asleep, so redo setup.
  if (now - last_sample_time_ > kNoDataTimeout && now - last_reinit_ > kNoDataTimeout) {
    last_reinit_ = now;
    if (mpu_.configure(cfg_)) {
      message("no data for 1 s: sensor re-initialised", 3);
    }
    last_sample_time_ = Clock::now();
    have_sample_ = false;
  }
}

void Monitor::change(const imu::Config & next, const std::string & what)
{
  if (!mpu_.applyConfig(next)) {
    message("config change failed: " + mpu_.lastError(), 2);
    return;
  }
  const bool rate_changed = next.sample_rate_hz != cfg_.sample_rate_hz;
  cfg_ = next;
  const auto now = Clock::now();
  settle_until_ = now + kSettle;
  have_sample_ = false;
  rate_start_ = now;
  rate_count_ = 0;
  if (rate_changed) {
    rate_ = 0.0;
    const bool was_calibrating = calibrating_;
    resize();
    if (was_calibrating) {
      startCalibration();
    }
  } else {
    for (auto & s : stats_) {
      s.clear();
    }
    if (calibrating_) {
      startCalibration();
    }
  }
  message(what, 1);
}

template<typename E>
E nextEnum(E value, int count)
{
  return static_cast<E>((static_cast<int>(value) + 1) % count);
}

void Monitor::handleKey(int key)
{
  imu::Config next = cfg_;
  char text[80];
  switch (key) {
    case 'a':
      next.accel_range = nextEnum(cfg_.accel_range, 4);
      std::snprintf(text, sizeof(text), "accel range +-%d g", imu::accelFullScaleG(next.accel_range));
      change(next, text);
      break;
    case 'g':
      next.gyro_range = nextEnum(cfg_.gyro_range, 4);
      std::snprintf(text, sizeof(text), "gyro range +-%d deg/s", imu::gyroFullScaleDps(next.gyro_range));
      change(next, text);
      break;
    case 'f':
      next.dlpf = nextEnum(cfg_.dlpf, 7);
      std::snprintf(text, sizeof(text), "filter %d Hz (accel) / %d Hz (gyro)",
        imu::accelBandwidthHz(next.dlpf), imu::gyroBandwidthHz(next.dlpf));
      change(next, text);
      break;
    case 'r': {
        auto it = std::find(kRates.begin(), kRates.end(), cfg_.sample_rate_hz);
        next.sample_rate_hz = (it == kRates.end() || ++it == kRates.end()) ? kRates.front() : *it;
        std::snprintf(text, sizeof(text), "sample rate %.0f Hz", next.sample_rate_hz);
        change(next, text);
        break;
      }
    case 'c':
      startCalibration();
      message("calibrating gyro bias: keep the robot still", 3);
      break;
    case 'z':
      yaw_ = 0.0;
      message("yaw zeroed", 1);
      break;
    case 'x':
      for (auto & s : stats_) {
        s.clear();
      }
      gaps_ = 0;
      message("stats cleared", 1);
      break;
    case 'q':
    case kCtrlC:
      g_quit = 1;
      break;
    default:
      break;
  }
}

void Monitor::message(const std::string & text, int color)
{
  message_ = text;
  message_color_ = color;
  message_time_ = Clock::now();
}

// Bar from the midline, scaled to full scale; red when the reading is clipped.
void Monitor::drawBar(int y, int x, int half, double fraction, bool clipped) const
{
  const int fill = static_cast<int>(std::lround(std::min(std::abs(fraction), 1.0) * half));
  const int color = clipped ? 2 : 4;
  mvaddch(y, x, '[');
  for (int i = -half; i <= half; ++i) {
    const bool lit = (fraction > 0.0 && i > 0 && i <= fill) || (fraction < 0.0 && i < 0 && -i <= fill);
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

void Monitor::drawStat(int y, const char * label, const Summary & s, bool accel) const
{
  mvprintw(y, 1, "%-15s", label);
  if (s.count == 0) {
    printw("%10s", "-");
    return;
  }
  if (accel) {
    printw("%+10.4f %10.4f %+10.4f %+10.4f %10.4f", s.mean, s.stddev, s.min, s.max, s.max - s.min);
  } else {
    printw("%+10.3f %10.3f %+10.3f %+10.3f %10.3f", s.mean, s.stddev, s.min, s.max, s.max - s.min);
  }
}

void Monitor::draw(Clock::time_point now)
{
  erase();
  const bool stale = now - last_sample_time_ > std::chrono::milliseconds(500);

  attron(A_BOLD);
  mvprintw(0, 1, "Prowl MPU6050");
  attroff(A_BOLD);
  printw("  i2c-%d 0x%02X  WHO_AM_I 0x%02X ", mpu_.bus(), mpu_.address(), mpu_.whoAmI());
  attron(COLOR_PAIR(mpu_.whoAmIOk() ? 1 : 3));
  printw("%s", mpu_.whoAmIOk() ? "ok" : "unexpected (clone?)");
  attroff(COLOR_PAIR(mpu_.whoAmIOk() ? 1 : 3));
  printw("  I2C errors ");
  attron(COLOR_PAIR(mpu_.errorCount() ? 2 : 1));
  printw("%lu", static_cast<unsigned long>(mpu_.errorCount()));
  attroff(COLOR_PAIR(mpu_.errorCount() ? 2 : 1));
  if (stale) {
    attron(COLOR_PAIR(2) | A_BOLD);
    printw("  NO DATA");
    attroff(COLOR_PAIR(2) | A_BOLD);
  }

  mvprintw(1, 1, "accel +-%dg  gyro +-%d deg/s  filter %d/%d Hz  rate %5.1f/%.1f Hz  gaps %lu",
    imu::accelFullScaleG(cfg_.accel_range), imu::gyroFullScaleDps(cfg_.gyro_range),
    imu::accelBandwidthHz(cfg_.dlpf), imu::gyroBandwidthHz(cfg_.dlpf), rate_, mpu_.sampleRateHz(), gaps_);

  if (calibrating_) {
    attron(COLOR_PAIR(3));
    mvprintw(2, 1, "calibrating gyro bias - keep the robot still  %zu/%zu", calib_.count(), calib_.target());
    if (calib_attempt_ > 0) {
      printw("  (moved, try %d/%d)", calib_attempt_ + 1, kCalibrationAttempts);
    }
    attroff(COLOR_PAIR(3));
  } else if (!message_.empty() && now - message_time_ < kMessageTime) {
    attron(COLOR_PAIR(message_color_));
    mvprintw(2, 1, "%.78s", message_.c_str());
    attroff(COLOR_PAIR(message_color_));
  } else if (mpu_.errorCount() > 0) {
    attron(COLOR_PAIR(2));
    mvprintw(2, 1, "last I2C error: %.62s", mpu_.lastError().c_str());
    attroff(COLOR_PAIR(2));
  }

  // Bars grow with the terminal width; 7 cells each side at 80 columns.
  const int half = std::max(4, 7 + (COLS - 80) / 4);
  const int bar_w = 2 * half + 3;
  const int accel_bar_x = 23;
  const int gyro_x = accel_bar_x + bar_w + 2;
  const int gyro_bar_x = gyro_x + 11;

  attron(A_BOLD);
  mvprintw(3, 1, "accel   m/s^2       g");
  mvprintw(3, accel_bar_x, "%-*s", bar_w, "(bars: full scale)");
  mvprintw(3, gyro_x, "gyro deg/s");
  attroff(A_BOLD);
  const double accel_fs = imu::accelFullScaleG(cfg_.accel_range);
  const double gyro_fs = imu::gyroFullScaleDps(cfg_.gyro_range);
  for (int i = 0; i < 3; ++i) {
    const int y = 4 + i;
    const double a = last_.accel[i];
    const double gyro = last_.gyro[i] * imu::kRadToDeg;
    mvprintw(y, 1, "  %c %+9.3f %+7.3f", kAxes[i], a, a / imu::kStandardGravity);
    drawBar(y, accel_bar_x, half, a / imu::kStandardGravity / accel_fs, imu::saturated(last_.raw.accel[i]));
    mvprintw(y, gyro_x, "%c %+8.2f", kAxes[i], gyro);
    drawBar(y, gyro_bar_x, half, gyro / gyro_fs, imu::saturated(last_.raw.gyro[i]));
  }
  const double mag = imu::norm(last_.accel);
  mvprintw(7, 1, " |a|%9.3f %7.3f", mag, mag / imu::kStandardGravity);
  mvprintw(7, accel_bar_x, "(1 g at rest)");
  mvprintw(7, gyro_x, "temp %6.2f C", last_.temperature);

  mvprintw(9, 1, "tilt (accel)  roll %+7.2f  pitch %+7.2f deg     yaw (gyro) %+8.2f deg",
    imu::rollFromAccel(last_.accel) * imu::kRadToDeg, imu::pitchFromAccel(last_.accel) * imu::kRadToDeg,
    yaw_ * imu::kRadToDeg);
  const imu::Vec3 & b = mpu_.gyroBias();
  if (have_bias_) {
    mvprintw(10, 1, "gyro bias  X %+7.3f  Y %+7.3f  Z %+7.3f deg/s   motion %.3f ",
      b[0] * imu::kRadToDeg, b[1] * imu::kRadToDeg, b[2] * imu::kRadToDeg,
      *std::max_element(calib_std_.begin(), calib_std_.end()) * imu::kRadToDeg);
    attron(COLOR_PAIR(calib_still_ ? 1 : 3));
    printw("%s", calib_still_ ? "still" : "MOVED");
    attroff(COLOR_PAIR(calib_still_ ? 1 : 3));
  } else {
    mvprintw(10, 1, "gyro bias  not calibrated yet (yaw paused)");
  }

  const Summary amag = stats_[kAmag].summary();
  attron(A_BOLD);
  char window[32];
  std::snprintf(window, sizeof(window), "last %.1fs (%zu)", amag.count / mpu_.sampleRateHz(), amag.count);
  mvprintw(12, 1, "%-15s%10s %10s %10s %10s %10s", window, "mean", "std", "min", "max", "p-p");
  attroff(A_BOLD);
  static constexpr std::array<const char *, kChannels> kLabels = {
    "accel X m/s^2", "accel Y m/s^2", "accel Z m/s^2", "|accel| m/s^2",
    "gyro X deg/s", "gyro Y deg/s", "gyro Z deg/s"};
  for (int c = 0; c < kChannels; ++c) {
    drawStat(13 + c, kLabels[c], stats_[c].summary(), c <= kAmag);
  }

  mvprintw(21, 1, "a accel range  g gyro range  f filter  r rate  c calibrate gyro  z zero yaw");
  mvprintw(22, 1, "x clear stats  q / Ctrl-C quit");
  attron(A_DIM);
  mvprintw(23, 1, "raw board axes (mounting unknown); yaw drifts");
  attroff(A_DIM);
  refresh();
}

void Monitor::run()
{
  initscr();
  raw();  // Ctrl-C arrives as a key, so the terminal is always restored
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_RED, -1);
    init_pair(3, COLOR_YELLOW, -1);
    init_pair(4, COLOR_CYAN, -1);
  }
  message("calibrating gyro bias: keep the robot still", 3);

  const auto start = Clock::now();
  last_sample_time_ = start;
  last_reinit_ = start;
  rate_start_ = start;
  next_poll_ = start;
  auto next_draw = start;
  while (!g_quit) {
    auto now = Clock::now();
    poll(now);

    const Seconds rate_span = now - rate_start_;
    if (rate_span.count() >= 1.0) {
      rate_ = rate_count_ / rate_span.count();
      rate_count_ = 0;
      rate_start_ = now;
    }
    if (now >= next_draw) {
      draw(now);
      next_draw = now + kDrawPeriod;
    }

    // getch doubles as the sleep until the next poll or redraw.
    now = Clock::now();
    const double wait = ms(std::min(next_poll_, next_draw) - now);
    timeout(std::clamp(static_cast<int>(wait), 0, 50));
    const int key = getch();
    if (key != ERR) {
      handleKey(key);
    }
  }
  endwin();
}

int usage(const char * argv0)
{
  std::fprintf(stderr, "usage: %s [bus] [address]   (defaults: 1 0x68)\n"
    "  bus 1 is /dev/i2c-1: SDA on header pin 3 (GPIO2), SCL on pin 5 (GPIO3)\n", argv0);
  return 2;
}

}  // namespace

int main(int argc, char ** argv)
{
  int bus = imu::kDefaultBus;
  long address = imu::kDefaultAddress;
  if (argc > 3) {
    return usage(argv[0]);
  }
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      return usage(argv[0]);
    }
    char * end = nullptr;
    const long v = std::strtol(argv[i], &end, 0);
    if (end == argv[i] || *end != '\0') {
      return usage(argv[0]);
    }
    if (i == 1) {
      bus = static_cast<int>(v);
    } else {
      address = v;
    }
  }
  if (bus < 0 || address < 0x03 || address > 0x77) {
    return usage(argv[0]);
  }

  std::signal(SIGHUP, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGINT, onSignal);

  imu::Mpu6050 mpu;
  if (!mpu.open(bus, static_cast<uint8_t>(address))) {
    std::fprintf(stderr, "MPU6050: %s\n", mpu.lastError().c_str());
    return 1;
  }
  if (!mpu.whoAmIOk()) {
    std::fprintf(stderr, "warning: WHO_AM_I is 0x%02X, expected 0x%02X (clone?); continuing\n",
      mpu.whoAmI(), imu::kWhoAmIValue);
  }
  if (!mpu.configure(imu::Config{})) {
    std::fprintf(stderr, "MPU6050 setup failed: %s\n", mpu.lastError().c_str());
    return 1;
  }

  Monitor monitor(mpu);
  monitor.run();
  return 0;
}
