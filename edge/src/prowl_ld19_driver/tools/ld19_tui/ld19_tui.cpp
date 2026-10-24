// Terminal tool for the LD19 LiDAR: link health, the nearest objects and a
// top-down map of the live scan. Reads the sensor directly through the driver
// library with no ROS2 involved, and works over SSH.

#include <curses.h>
#include <langinfo.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "prowl_ld19_driver/packet_parser.hpp"
#include "prowl_ld19_driver/protocol.hpp"
#include "prowl_ld19_driver/scan_analysis.hpp"
#include "prowl_ld19_driver/scan_assembler.hpp"
#include "prowl_ld19_driver/serial_port.hpp"

using namespace prowl_ld19_driver;

namespace
{

using Clock = std::chrono::steady_clock;

// The LD19's Tx is wired to Pi header pin 10 (GPIO15, UART RXD): the mini UART.
constexpr const char * kDefaultPort = "/dev/ttyS0";
constexpr int kCtrlC = 3;
constexpr int kPanelWidth = 40;
constexpr int kReadTimeoutMs = 20;
constexpr auto kRedrawEvery = std::chrono::milliseconds(100);
constexpr auto kRatesEvery = std::chrono::seconds(1);
constexpr auto kNoDataAfter = std::chrono::seconds(1);
constexpr auto kReopenEvery = std::chrono::seconds(1);
constexpr auto kMessageTime = std::chrono::seconds(3);
constexpr std::array<double, 9> kZoomLevels = {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0};  // m to map edge
constexpr size_t kDefaultZoom = 3;
constexpr size_t kHistoryScans = 10;
constexpr size_t kRateScans = 10;
constexpr int kMinRangeStepMm = 50;
constexpr int kMaxMinRangeMm = 1000;
constexpr double kPi = 3.14159265358979323846;

enum Pair { kGreen = 1, kRed, kYellow, kCyan, kBlue };

volatile std::sig_atomic_t g_quit = 0;

void onSignal(int)
{
  g_quit = 1;
}

// Terminal columns taken by UTF-8 text: continuation bytes take none.
int columns(const std::string & s)
{
  return static_cast<int>(std::count_if(s.begin(), s.end(), [](char c) {return (c & 0xC0) != 0x80;}));
}

std::string clip(const std::string & s, int max_cols)
{
  int used = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((s[i] & 0xC0) != 0x80 && used++ == max_cols) {
      return s.substr(0, i);
    }
  }
  return s;
}

// Unicode braille: each terminal cell holds 2 x 4 dots.
constexpr int kBrailleBit[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};

std::string brailleGlyph(int bits)
{
  const int code = 0x2800 + bits;
  return {static_cast<char>(0xE0 | (code >> 12)), static_cast<char>(0x80 | ((code >> 6) & 0x3F)),
    static_cast<char>(0x80 | (code & 0x3F))};
}

// Dots of one map layer, as bits per terminal cell.
class Layer
{
public:
  Layer(int cols, int rows) : cols_(cols), rows_(rows), cells_(static_cast<size_t>(cols * rows), 0) {}

  void set(int cell_x, int cell_y, int bit)
  {
    if (cell_x >= 0 && cell_x < cols_ && cell_y >= 0 && cell_y < rows_) {
      cells_[static_cast<size_t>(cell_y * cols_ + cell_x)] |= static_cast<uint8_t>(bit);
    }
  }

  int at(int cell_x, int cell_y) const {return cells_[static_cast<size_t>(cell_y * cols_ + cell_x)];}

private:
  int cols_;
  int rows_;
  std::vector<uint8_t> cells_;
};

// Maps robot-frame metres onto the cells of the map area. A terminal cell is
// about twice as tall as it is wide, so lengths are worked out in half-cell
// units: a cell is 1 x 2 of them, a braille dot 0.5 x 0.5.
class Projection
{
public:
  Projection(int cols, int rows, double edge_m, bool braille)
  : cols_(cols), rows_(rows), braille_(braille),
    m_per_unit_(edge_m / (std::min(cols, 2 * rows) / 2.0)) {}

  // Position in units from the map's top-left corner.
  void units(double bearing_deg, double range_m, double & ux, double & uy) const
  {
    const double b = bearing_deg * kPi / 180.0;
    ux = cols_ / 2.0 + range_m * std::sin(b) / m_per_unit_;
    uy = rows_ - range_m * std::cos(b) / m_per_unit_;
  }

  // Marks the dot (or, without braille, the cell) under a point.
  void plot(Layer & layer, double bearing_deg, double range_m) const
  {
    double ux, uy;
    units(bearing_deg, range_m, ux, uy);
    if (ux < 0.0 || uy < 0.0) {
      return;
    }
    const int cx = static_cast<int>(ux);
    const int cy = static_cast<int>(uy / 2.0);
    if (braille_) {
      const int dx = static_cast<int>((ux - cx) * 2.0);
      const int dy = static_cast<int>((uy / 2.0 - cy) * 4.0);
      layer.set(cx, cy, kBrailleBit[std::min(dy, 3)][std::min(dx, 1)]);
    } else {
      layer.set(cx, cy, 1);
    }
  }

  bool cell(double bearing_deg, double range_m, int & cx, int & cy) const
  {
    double ux, uy;
    units(bearing_deg, range_m, ux, uy);
    cx = static_cast<int>(std::floor(ux));
    cy = static_cast<int>(std::floor(uy / 2.0));
    return cx >= 0 && cx < cols_ && cy >= 0 && cy < rows_;
  }

  // Smallest dot size, so circles can be drawn without gaps.
  double dotMetres() const {return m_per_unit_ * (braille_ ? 0.5 : 1.0);}

private:
  int cols_;
  int rows_;
  bool braille_;
  double m_per_unit_;
};

// Ring spacing that gives 2-4 rings out to the map edge.
double ringStep(double edge_m)
{
  for (double step : {0.1, 0.25, 0.5, 1.0, 2.0, 2.5, 5.0}) {
    if (edge_m / step <= 4.0) {
      return step;
    }
  }
  return 5.0;
}

struct Rates
{
  double packets = 0.0;
  double crc_errors = 0.0;
  double bytes = 0.0;
};

class Tool
{
public:
  Tool(std::string port, bool utf8) : port_name_(std::move(port)), utf8_(utf8), braille_(utf8) {}

  int run()
  {
    initscr();
    raw();  // Ctrl-C arrives as a key, so the terminal is always restored
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(0);
    if (has_colors()) {
      start_color();
      use_default_colors();
      init_pair(kGreen, COLOR_GREEN, -1);
      init_pair(kRed, COLOR_RED, -1);
      init_pair(kYellow, COLOR_YELLOW, -1);
      init_pair(kCyan, COLOR_CYAN, -1);
      init_pair(kBlue, COLOR_BLUE, -1);
    }

    last_rates_ = Clock::now();
    auto last_draw = Clock::time_point{};
    while (!g_quit) {
      pollSensor();
      bool dirty = false;
      for (int key = getch(); key != ERR; key = getch()) {
        handleKey(key);
        dirty = true;
      }
      const auto now = Clock::now();
      if (now - last_rates_ >= kRatesEvery) {
        updateRates(now);
      }
      if (dirty || now - last_draw >= kRedrawEvery) {
        draw();
        last_draw = now;
      }
    }
    endwin();
    return 0;
  }

private:
  void pollSensor()
  {
    const auto now = Clock::now();
    if (!port_.isOpen()) {
      if (now < next_open_) {
        napms(kReadTimeoutMs);
        return;
      }
      std::string error;
      if (!port_.open(port_name_, error)) {
        port_error_ = error;
        next_open_ = now + kReopenEvery;
        return;
      }
      port_error_.clear();
      assembler_.reset();
    }

    uint8_t buf[4096];
    const ssize_t n = port_.read(buf, sizeof(buf), kReadTimeoutMs);
    if (n < 0) {
      port_error_ = port_name_ + ": read failed: " + std::strerror(errno);
      port_.close();
      next_open_ = now + kReopenEvery;
      return;
    }
    packets_.clear();
    parser_.feed(buf, static_cast<size_t>(n), packets_);
    for (const Packet & p : packets_) {
      last_packet_ = now;
      if (assembler_.add(p)) {
        onScan(assembler_.scan());
      }
    }
  }

  void onScan(const Scan & scan)
  {
    scan_starts_.push_back(scan.start_time_ms);
    if (scan_starts_.size() > kRateScans + 1) {
      scan_starts_.pop_front();
    }
    have_scan_ = true;
    if (paused_) {
      return;
    }
    scan_ = scan;
    history_.push_back(scan);
    if (history_.size() > kHistoryScans) {
      history_.pop_front();
    }
  }

  void updateRates(Clock::time_point now)
  {
    const double dt = std::chrono::duration<double>(now - last_rates_).count();
    const ParserStats & s = parser_.stats();
    rates_.packets = (s.packets - prev_stats_.packets) / dt;
    rates_.crc_errors = (s.crc_errors - prev_stats_.crc_errors) / dt;
    rates_.bytes = (s.bytes - prev_stats_.bytes) / dt;
    prev_stats_ = s;
    last_rates_ = now;
  }

  void handleKey(int key)
  {
    switch (key) {
      case '+': case '=':
        zoom_ = zoom_ > 0 ? zoom_ - 1 : 0;
        break;
      case '-': case '_':
        zoom_ = std::min(zoom_ + 1, kZoomLevels.size() - 1);
        break;
      case 'r':
        mount_.yaw_deg = std::fmod(mount_.yaw_deg + 90.0, 360.0);
        break;
      case 'R':
        mount_.yaw_deg = std::fmod(mount_.yaw_deg + 270.0, 360.0);
        break;
      case 'm': case 'M':
        mount_.mirrored = !mount_.mirrored;
        break;
      case 'h': case 'H':
        show_history_ = !show_history_;
        break;
      case ' ':
        paused_ = !paused_;
        break;
      case '[':
        config_.min_range_mm = static_cast<uint16_t>(std::max(config_.min_range_mm - kMinRangeStepMm, 1));
        break;
      case ']':
        config_.min_range_mm = static_cast<uint16_t>(
          std::min((config_.min_range_mm / kMinRangeStepMm + 1) * kMinRangeStepMm, kMaxMinRangeMm));
        break;
      case 'a': case 'A':
        if (utf8_) {
          braille_ = !braille_;
        } else {
          say("braille needs a UTF-8 locale (e.g. LANG=C.UTF-8)");
        }
        break;
      case 'q': case 'Q': case kCtrlC:
        g_quit = 1;
        break;
    }
  }

  void say(const std::string & text)
  {
    message_ = text;
    message_until_ = Clock::now() + kMessageTime;
  }

  const char * deg() const {return utf8_ ? "\xC2\xB0" : "d";}

  // "123° FR": bearing and its sector name.
  std::string bearingText(double bearing_deg) const
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%3.0f%s %-2s", std::round(bearing_deg) == 360.0 ? 0.0 : bearing_deg, deg(),
      kSectorNames[sectorOf(bearing_deg)]);
    return buf;
  }

  void text(int row, int col, const std::string & s, attr_t attr = A_NORMAL, int max_cols = -1)
  {
    if (row < 0 || row >= LINES || col >= COLS) {
      return;
    }
    const int room = max_cols < 0 ? COLS - col : std::min(max_cols, COLS - col);
    attron(attr);
    mvaddstr(row, col, clip(s, std::max(room, 0)).c_str());
    attroff(attr);
  }

  std::string statusText(attr_t & attr) const
  {
    if (!port_error_.empty()) {
      attr = COLOR_PAIR(kRed) | A_BOLD;
      return "ERROR";
    }
    if (!port_.isOpen()) {
      attr = COLOR_PAIR(kYellow) | A_BOLD;
      return "OPENING";
    }
    if (last_packet_ == Clock::time_point{} || Clock::now() - last_packet_ > kNoDataAfter) {
      attr = COLOR_PAIR(kRed) | A_BOLD;
      return "NO DATA";
    }
    if (paused_) {
      attr = COLOR_PAIR(kYellow) | A_BOLD;
      return "PAUSED";
    }
    attr = COLOR_PAIR(kGreen) | A_BOLD;
    return "LIVE";
  }

  void draw()
  {
    erase();
    if (COLS < 60 || LINES < 16) {
      text(0, 0, "terminal too small for ld19_tui (80x24 or more), q quits");
      refresh();
      return;
    }

    // Title bar.
    char buf[200];
    attr_t status_attr = A_NORMAL;
    const std::string status = statusText(status_attr);
    std::snprintf(buf, sizeof(buf), " Prowl LD19 lidar   %s   ", port_name_.c_str());
    text(0, 0, std::string(static_cast<size_t>(COLS), ' '), A_REVERSE);
    text(0, 0, buf, A_REVERSE | A_BOLD);
    text(0, static_cast<int>(std::strlen(buf)), status, status_attr | A_REVERSE);
    // Mirrored means the sensor's angles run counterclockwise on the robot.
    std::snprintf(buf, sizeof(buf), "sensor 0%s = robot %.0f%s %s, %s ", deg(), mount_.yaw_deg, deg(),
      kSectorNames[sectorOf(mount_.yaw_deg)], mount_.mirrored ? "mirrored" : "cw");
    const int title_right = COLS - columns(buf);
    if (title_right > 41) {
      text(0, title_right, buf, A_REVERSE);
    }

    const int body_rows = LINES - 3;
    drawPanel(1, body_rows);
    mvvline(1, kPanelWidth, ACS_VLINE, body_rows);
    drawMap(1, kPanelWidth + 1, body_rows, COLS - kPanelWidth - 1);

    // Help.
    text(LINES - 2, 0, " q/^C quit  +/- zoom  r/R rotate 90  m mirror  h history  space pause  a ascii",
      A_DIM);
    std::snprintf(buf, sizeof(buf), " [/] ignore closer than %.2f m   history %s   %s", config_.min_range_mm / 1000.0,
      show_history_ ? "on" : "off", braille_ ? "braille" : "ascii");
    text(LINES - 1, 0, buf, A_DIM);
    if (Clock::now() < message_until_) {
      text(LINES - 1, 0, std::string(static_cast<size_t>(COLS - 1), ' '));
      text(LINES - 1, 1, "! " + message_, A_BOLD);
    }
    refresh();
  }

  void drawPanel(int top, int rows)
  {
    char buf[128];
    const int w = kPanelWidth - 1;
    int row = top;
    const int end = top + rows;

    if (!port_error_.empty()) {
      text(row++, 1, port_error_, COLOR_PAIR(kRed) | A_BOLD, w);
      if (port_error_.find("Device or resource busy") != std::string::npos) {
        text(row++, 1, "another reader has the port", COLOR_PAIR(kRed), w);
      }
    }

    text(row++, 1, "HEALTH", A_BOLD);
    const ParserStats & s = parser_.stats();
    double rotation_hz = 0.0;
    if (scan_starts_.size() >= 2 && scan_starts_.back() > scan_starts_.front()) {
      rotation_hz = 1000.0 * static_cast<double>(scan_starts_.size() - 1) /
        static_cast<double>(scan_starts_.back() - scan_starts_.front());
    }
    if (have_scan_) {
      std::snprintf(buf, sizeof(buf), "rotation   %5.2f Hz  (speed %4.0f%s/s)", rotation_hz, scan_.speed_dps, deg());
    } else {
      std::snprintf(buf, sizeof(buf), "rotation      -- Hz");
    }
    text(row++, 1, buf, A_NORMAL, w);
    std::snprintf(buf, sizeof(buf), "packets    %5.0f /s  %5.1f kB/s", rates_.packets, rates_.bytes / 1000.0);
    text(row++, 1, buf, A_NORMAL, w);
    std::snprintf(buf, sizeof(buf), "CRC errors %5llu     %5.1f /s", static_cast<unsigned long long>(s.crc_errors),
      rates_.crc_errors);
    text(row++, 1, buf, s.crc_errors ? COLOR_PAIR(kYellow) : A_NORMAL, w);
    std::snprintf(buf, sizeof(buf), "skipped    %5llu B   %5llu packets",
      static_cast<unsigned long long>(s.skipped_bytes), static_cast<unsigned long long>(s.packets));
    text(row++, 1, buf, A_NORMAL, w);

    size_t valid = 0;
    double intensity = 0.0;
    for (const ScanPoint & p : scan_.points) {
      if (p.distance_mm > 0) {
        ++valid;
        intensity += p.intensity;
      }
    }
    const size_t total = scan_.points.size();
    std::snprintf(buf, sizeof(buf), "points     %5zu /rot valid %5.1f %%", total, total ? 100.0 * valid / total : 0.0);
    text(row++, 1, buf, A_NORMAL, w);
    std::snprintf(buf, sizeof(buf), "intensity  %5.0f mean", valid ? intensity / valid : 0.0);
    text(row++, 1, buf, A_NORMAL, w);
    ++row;

    const Nearest nearest = nearestPoint(scan_, mount_, config_);
    if (nearest.found) {
      std::snprintf(buf, sizeof(buf), "NEAREST  %5.2f m at %s", nearest.distance_m,
        bearingText(nearest.bearing_deg).c_str());
    } else {
      std::snprintf(buf, sizeof(buf), "NEAREST     --");
    }
    text(row++, 1, buf, A_BOLD, w);
    ++row;

    text(row++, 1, "NEAREST BY SECTOR (m)", A_BOLD);
    const auto sectors = sectorNearest(scan_, mount_, config_);
    auto cell = [&](size_t i) {
        char c[16];
        if (sectors[i].found) {
          std::snprintf(c, sizeof(c), "%2s %5.2f", kSectorNames[i], sectors[i].distance_m);
        } else {
          std::snprintf(c, sizeof(c), "%2s    --", kSectorNames[i]);
        }
        return std::string(c);
      };
    // Laid out like the robot seen from above, forward up.
    text(row++, 2, cell(7) + "   " + cell(0) + "   " + cell(1));
    text(row++, 2, cell(6) + std::string(14, ' ') + cell(2));
    text(row++, 2, cell(5) + "   " + cell(4) + "   " + cell(3));
    ++row;

    objects_ = findObjects(scan_, mount_, config_);
    text(row++, 1, "OBJECTS   dist     bearing   width", A_BOLD);
    for (size_t i = 0; i < objects_.size() && row < end; ++i) {
      const Object & o = objects_[i];
      std::snprintf(buf, sizeof(buf), "%2zu     %5.2f m    %s  %5.2f m", i + 1, o.distance_m,
        bearingText(o.bearing_deg).c_str(), o.width_m);
      text(row++, 1, buf, i == 0 ? COLOR_PAIR(kYellow) | A_BOLD : COLOR_PAIR(kYellow), w);
    }
    if (objects_.empty() && row < end) {
      text(row++, 1, have_scan_ ? "  none in range" : "  waiting for a full rotation", A_DIM, w);
    }
  }

  void drawMap(int top, int left, int rows, int cols)
  {
    if (rows < 4 || cols < 8) {
      return;
    }
    const double edge = kZoomLevels[zoom_];
    const Projection proj(cols, rows, edge, braille_);
    Layer current(cols, rows), ignored(cols, rows), history(cols, rows), rings(cols, rows);

    const double step = ringStep(edge);
    for (double r = step; r <= edge * 2.0; r += step) {
      // Dotted, so the rings can't be mistaken for walls.
      const int n = static_cast<int>(std::ceil(2.0 * kPi * r / (3.0 * proj.dotMetres())));
      for (int i = 0; i < n; ++i) {
        proj.plot(rings, 360.0 * i / n, r);
      }
    }
    if (show_history_) {
      for (const Scan & scan : history_) {
        for (const ScanPoint & p : scan.points) {
          if (p.distance_mm >= config_.min_range_mm && p.distance_mm > 0) {
            proj.plot(history, mount_.bearing(p.angle_deg), p.distance_mm / 1000.0);
          }
        }
      }
    }
    for (const ScanPoint & p : scan_.points) {
      if (p.distance_mm == 0) {
        continue;
      }
      proj.plot(p.distance_mm >= config_.min_range_mm ? current : ignored, mount_.bearing(p.angle_deg),
        p.distance_mm / 1000.0);
    }

    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < cols; ++x) {
        int bits = current.at(x, y);
        attr_t attr = COLOR_PAIR(kGreen) | A_BOLD;
        char ascii = '#';
        if (!bits && (bits = ignored.at(x, y))) {
          attr = COLOR_PAIR(kRed);
          ascii = 'x';
        } else if (!bits && (bits = history.at(x, y))) {
          attr = COLOR_PAIR(kGreen) | A_DIM;
          ascii = '+';
        } else if (!bits && (bits = rings.at(x, y))) {
          attr = COLOR_PAIR(kBlue) | A_DIM;
          ascii = '.';
        }
        if (!bits) {
          continue;
        }
        attron(attr);
        if (braille_) {
          mvaddstr(top + y, left + x, brailleGlyph(bits).c_str());
        } else {
          mvaddch(top + y, left + x, static_cast<chtype>(ascii));
        }
        attroff(attr);
      }
    }

    // Sensor angle markers on the map edge, so the mounting can be checked by eye.
    auto edgeMark = [&](double sensor_deg, const std::string & label, attr_t attr) {
        const double b = mount_.bearing(sensor_deg) * kPi / 180.0;
        const double dx = std::sin(b), dy = -std::cos(b);
        const double hx = cols / 2.0 - 1.0, hy = rows - 1.0;  // half extents in units
        const double t = std::min(std::abs(dx) > 1e-9 ? hx / std::abs(dx) : 1e9,
          std::abs(dy) > 1e-9 ? hy / std::abs(dy) : 1e9);
        const int len = columns(label);
        int cx = static_cast<int>(std::floor(cols / 2.0 + dx * t)) - len / 2;
        const int cy = static_cast<int>(std::floor((rows + dy * t) / 2.0));
        cx = std::clamp(cx, 0, cols - len);
        text(top + std::clamp(cy, 0, rows - 1), left + cx, label, attr);
      };
    edgeMark(90.0, std::string("90") + deg(), COLOR_PAIR(kCyan));
    edgeMark(0.0, std::string("0") + deg(), COLOR_PAIR(kCyan) | A_BOLD);

    // Objects, numbered as in the list, at their nearest point.
    for (size_t i = 0; i < objects_.size() && i < 9; ++i) {
      int cx, cy;
      if (proj.cell(objects_[i].bearing_deg, objects_[i].distance_m, cx, cy)) {
        text(top + cy, left + cx, std::string(1, static_cast<char>('1' + i)),
          COLOR_PAIR(kYellow) | A_BOLD | A_REVERSE);
      }
    }

    int cx, cy;
    proj.cell(0.0, 0.0, cx, cy);
    text(top + cy, left + cx, "^", A_BOLD | A_REVERSE);

    char buf[64];
    text(top, left + 1, "forward up", A_DIM);
    std::snprintf(buf, sizeof(buf), "edge %.1f m  rings %.2g m", edge, step);
    text(top + rows - 1, left + 1, buf, A_DIM);
  }

  std::string port_name_;
  SerialPort port_;
  std::string port_error_;
  Clock::time_point next_open_{};
  PacketParser parser_;
  ScanAssembler assembler_;
  std::vector<Packet> packets_;
  Clock::time_point last_packet_{};

  ParserStats prev_stats_;
  Clock::time_point last_rates_{};
  Rates rates_;
  std::deque<uint64_t> scan_starts_;

  bool have_scan_ = false;
  Scan scan_;
  std::deque<Scan> history_;
  std::vector<Object> objects_;

  Mount mount_;
  AnalysisConfig config_;
  size_t zoom_ = kDefaultZoom;
  bool utf8_;
  bool braille_;
  bool show_history_ = false;
  bool paused_ = false;
  std::string message_;
  Clock::time_point message_until_{};
};

}  // namespace

int main(int argc, char ** argv)
{
  std::string port = kDefaultPort;
  if (argc > 2 || (argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0))) {
    std::fprintf(stderr, "usage: %s [port]\n  default %s: the mini UART, LD19 Tx on Pi pin 10 (GPIO15 RXD)\n",
      argv[0], kDefaultPort);
    return 2;
  }
  if (argc == 2) {
    port = argv[1];
  }

  std::setlocale(LC_ALL, "");
  const bool utf8 = std::strcmp(nl_langinfo(CODESET), "UTF-8") == 0;

  // Leave the terminal clean if the SSH session drops or we're asked to exit.
  std::signal(SIGHUP, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGINT, onSignal);

  Tool tool(port, utf8);
  return tool.run();
}
