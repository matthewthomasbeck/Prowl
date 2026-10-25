#include "prowl_gps_driver/receiver_state.hpp"

#include <algorithm>

namespace prowl_gps_driver
{

namespace
{

constexpr double kMetresPerSecondPerKnot = 1852.0 / 3600.0;

}  // namespace

Update ReceiverState::apply(const Sentence & s, double now)
{
  if (s.type == "GGA") {
    Gga gga;
    if (!parseGga(s, gga)) {
      return Update::None;
    }
    gga_ = gga;
    gga_talker_ = s.talker;
    have_gga_ = true;
    return Update::Fix;
  }
  if (s.type == "RMC") {
    Rmc rmc;
    if (!parseRmc(s, rmc)) {
      return Update::None;
    }
    rmc_ = rmc;
    return rmc_.valid && rmc_.speed_knots ? Update::Velocity : Update::None;
  }
  if (s.type == "GSA") {
    Gsa gsa;
    if (parseGsa(s, gsa)) {
      gsa_ = gsa;
      gsa_at_ = now;
    }
    return Update::None;
  }
  if (s.type == "GSV") {
    Gsv gsv;
    if (parseGsv(s, gsv)) {
      applyGsv(s.talker, gsv, now);
    }
  }
  return Update::None;
}

// A set is GSV messages 1 to total, in order. A message out of order drops the set it would belong to.
void ReceiverState::applyGsv(const std::string & talker, const Gsv & gsv, double now)
{
  SatelliteSet & set = sets_[talker];
  if (gsv.message_number == 1) {
    set.pending = gsv.satellites;
    set.next = 2;
  } else if (gsv.message_number == set.next) {
    set.pending.insert(set.pending.end(), gsv.satellites.begin(), gsv.satellites.end());
    ++set.next;
  } else {
    set.pending.clear();
    set.next = 0;
    return;
  }
  if (gsv.message_number == gsv.total_messages) {
    set.complete = std::move(set.pending);
    set.pending.clear();
    set.at = now;
    set.next = 0;
  }
}

GpsFix ReceiverState::fix(double now) const
{
  GpsFix f;
  f.talker = gga_talker_;
  const bool gsa_fresh = gsa_at_ && now - *gsa_at_ <= kSatelliteTimeout;

  if (have_gga_) {
    f.quality = gga_.quality;
    f.satellites_used = gga_.satellites_used;
    f.hdop = gga_.hdop;
    // Quality 6 is a dead-reckoning estimate, which the u-blox 7 also reports for a fix that's out of
    // its limits: not a fix.
    const bool has_fix = (gga_.quality == 1 || gga_.quality == 2) && gga_.latitude && gga_.longitude;
    if (has_fix) {
      f.fix = gsa_fresh && gsa_.nav_mode == 2 ? FixType::Fix2D : FixType::Fix3D;
      f.latitude = gga_.latitude;
      f.longitude = gga_.longitude;
      f.altitude_msl = gga_.altitude_msl;
      f.geoid_separation = gga_.geoid_separation;
    }
    if (gga_.time && rmc_.date) {
      f.utc = unixTime(*rmc_.date, *gga_.time);
    }
  }
  if (f.fix != FixType::None && rmc_.valid) {
    if (rmc_.speed_knots) {
      f.speed_mps = *rmc_.speed_knots * kMetresPerSecondPerKnot;
    }
    f.course_deg = rmc_.course_deg;
  }
  if (gsa_fresh) {
    f.pdop = gsa_.pdop;
    f.vdop = gsa_.vdop;
    if (!f.hdop) {
      f.hdop = gsa_.hdop;
    }
  }

  for (const auto & [talker, set] : sets_) {
    if (set.complete.empty() || now - set.at > kSatelliteTimeout) {
      continue;
    }
    for (const SatelliteInView & in_view : set.complete) {
      Satellite sat;
      sat.id = in_view.id;
      sat.elevation = in_view.elevation;
      sat.azimuth = in_view.azimuth;
      sat.cno = in_view.cno;
      sat.used = f.fix != FixType::None && gsa_fresh &&
        std::find(gsa_.used_ids.begin(), gsa_.used_ids.end(), in_view.id) != gsa_.used_ids.end();
      f.satellites.push_back(sat);
    }
  }
  std::sort(f.satellites.begin(), f.satellites.end(),
    [](const Satellite & a, const Satellite & b) {return a.id < b.id;});
  return f;
}

void ReceiverState::reset()
{
  *this = ReceiverState{};
}

}  // namespace prowl_gps_driver
