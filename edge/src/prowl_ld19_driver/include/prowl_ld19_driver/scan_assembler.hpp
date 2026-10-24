#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "prowl_ld19_driver/protocol.hpp"

namespace prowl_ld19_driver
{

struct ScanPoint
{
  double angle_deg;  // [0, 360), clockwise from the sensor's front
  uint16_t distance_mm;  // 0 = no return
  uint8_t intensity;
};

// One full rotation, 0 deg up to 360 deg.
struct Scan
{
  std::vector<ScanPoint> points;
  double speed_dps = 0.0;  // mean of the packets' speed field
  uint64_t start_time_ms = 0;  // device timestamps, unwrapped to count up forever
  uint64_t end_time_ms = 0;
  size_t packets = 0;
};

// Collects packets into full rotations. A rotation ends where the point angle
// wraps back past 0 deg; the partial rotation before the first wrap is dropped.
class ScanAssembler
{
public:
  // True when this packet completed a rotation, which scan() then holds.
  bool add(const Packet & packet);

  const Scan & scan() const {return done_;}
  void reset();

private:
  void startScan(uint64_t time_ms);

  Scan current_;
  Scan done_;
  bool started_ = false;  // current_ began at a wrap, so it is a whole rotation
  bool have_time_ = false;
  uint16_t last_stamp_ = 0;
  uint64_t time_ms_ = 0;
  double last_angle_ = 0.0;
  double speed_sum_ = 0.0;
};

}  // namespace prowl_ld19_driver
