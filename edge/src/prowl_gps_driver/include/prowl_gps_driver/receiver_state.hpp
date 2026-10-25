#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "prowl_gps_driver/nmea.hpp"

namespace prowl_gps_driver
{

// Satellite lists and GSA's used satellites older than this are dropped: the receiver stops sending GSV
// when it sees no satellites, rather than sending an empty list.
constexpr double kSatelliteTimeout = 3.0;  // s

enum class FixType {None, Fix2D, Fix3D};

struct Satellite
{
  int id = 0;  // NMEA numbering: GPS 1-32, SBAS 33-64, GLONASS 65-96
  std::optional<int> elevation;  // degrees
  std::optional<int> azimuth;  // degrees, clockwise from true north
  std::optional<int> cno;  // dBHz; empty when it isn't being tracked
  bool used = false;  // in the current fix
};

// Everything the receiver has said, as of its newest GGA.
struct GpsFix
{
  std::string talker = "GP";  // of the newest GGA: GP = GPS, GL = GLONASS, GN = several
  FixType fix = FixType::None;
  int quality = 0;  // GGA's: 0 no fix, 1 GPS, 2 differential (SBAS), 6 estimated
  std::optional<double> utc;  // the fix's time, s since 1970, once the receiver knows the date and time
  // Only with a fix:
  std::optional<double> latitude;  // degrees, north positive
  std::optional<double> longitude;  // degrees, east positive
  std::optional<double> altitude_msl;  // m above mean sea level; also kept for a 2D fix, where it's assumed
  std::optional<double> geoid_separation;  // m, mean sea level above the WGS84 ellipsoid
  std::optional<double> speed_mps;  // over ground
  std::optional<double> course_deg;  // over ground, clockwise from true north
  // Always (the receiver sends 99.99 for the DOPs while it has no fix):
  int satellites_used = 0;
  std::optional<double> hdop;
  std::optional<double> vdop;
  std::optional<double> pdop;
  std::vector<Satellite> satellites;  // in view, sorted by ID
};

// What a sentence brought in.
enum class Update {None, Fix, Velocity};

// Collects what the receiver sends each second (by default RMC, VTG, GGA, GSA, GSV and GLL, in that
// order) into one GpsFix.
class ReceiverState
{
public:
  // now: seconds on a steady clock, to age out old satellite lists. Returns Fix for each GGA (the
  // position fix itself) and Velocity for each RMC with a valid fix.
  Update apply(const Sentence & s, double now);

  GpsFix fix(double now) const;
  void reset();

private:
  struct SatelliteSet
  {
    std::vector<SatelliteInView> complete;
    double at = 0.0;  // when the complete set arrived
    std::vector<SatelliteInView> pending;  // a set still coming in
    int next = 0;  // the message number expected next, 0 = none
  };

  void applyGsv(const std::string & talker, const Gsv & gsv, double now);

  bool have_gga_ = false;
  std::string gga_talker_ = "GP";
  Gga gga_;
  Rmc rmc_;
  Gsa gsa_;
  std::optional<double> gsa_at_;
  std::map<std::string, SatelliteSet> sets_;  // one per GSV talker
};

}  // namespace prowl_gps_driver
