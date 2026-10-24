#include "prowl_ld19_driver/scan_assembler.hpp"

#include <utility>

namespace prowl_ld19_driver
{

namespace
{

// Angles only increase within a rotation, apart from tiny jitter between
// packets, so a drop this large is the wrap past 0 deg.
constexpr double kWrapDrop = 180.0;
// A gap in the device clock this long means the stream stopped and restarted.
constexpr uint64_t kMaxPacketGapMs = 1000;
// Well above the ~450 points of a 10 Hz rotation; guards against never seeing a wrap.
constexpr size_t kMaxPoints = 5000;

}  // namespace

bool ScanAssembler::add(const Packet & packet)
{
  if (have_time_) {
    const uint64_t delta = (packet.timestamp_ms + kTimestampWrap - last_stamp_) % kTimestampWrap;
    time_ms_ += delta;
    if (delta > kMaxPacketGapMs) {
      started_ = false;
    }
  } else {
    have_time_ = true;
    current_.start_time_ms = time_ms_;
  }
  last_stamp_ = packet.timestamp_ms;

  bool completed = false;
  bool counted = false;
  for (size_t i = 0; i < kPointsPerPacket; ++i) {
    const double angle = packet.pointAngle(i);
    if (!current_.points.empty() && angle < last_angle_ - kWrapDrop) {
      if (started_) {
        current_.speed_dps = current_.packets ? speed_sum_ / static_cast<double>(current_.packets) : 0.0;
        current_.end_time_ms = time_ms_;
        done_ = std::move(current_);
        completed = true;
      }
      started_ = true;
      startScan(time_ms_);
      counted = false;
    }
    if (!counted) {
      ++current_.packets;
      speed_sum_ += packet.speed_dps;
      counted = true;
    }
    current_.points.push_back({angle, packet.points[i].distance_mm, packet.points[i].intensity});
    last_angle_ = angle;
  }
  current_.end_time_ms = time_ms_;

  if (current_.points.size() > kMaxPoints) {
    started_ = false;
    startScan(time_ms_);
  }
  return completed;
}

void ScanAssembler::startScan(uint64_t time_ms)
{
  current_ = Scan{};
  current_.start_time_ms = time_ms;
  speed_sum_ = 0.0;
}

void ScanAssembler::reset()
{
  *this = ScanAssembler{};
}

}  // namespace prowl_ld19_driver
