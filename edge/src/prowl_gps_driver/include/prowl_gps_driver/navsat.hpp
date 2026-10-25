#pragma once

#include <string>

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "prowl_gps_driver/receiver_state.hpp"

namespace prowl_gps_driver
{

// Fills everything but the header.
// - status: NO_FIX without a fix, SBAS_FIX for a differential fix (the u-blox 7's corrections come from
//   SBAS), FIX otherwise. service: GLONASS for a GL talker, else GPS.
// - latitude, longitude, altitude: NaN without a fix. altitude is above the WGS84 ellipsoid, as
//   NavSatFix asks: GGA's mean-sea-level altitude plus its geoid separation.
// - position_covariance: approximated from the DOPs (east and north (HDOP x uere)², up (VDOP x uere)²,
//   or twice the horizontal without a VDOP); unknown without a fix.
void fillNavSatFix(const GpsFix & fix, double uere, sensor_msgs::msg::NavSatFix & msg);

// Velocity over ground in east-north-up: linear.x east, linear.y north, in m/s. False (and msg left
// alone) without a speed and a course.
bool fillVelocity(const GpsFix & fix, geometry_msgs::msg::TwistStamped & msg);

// The receiver's state for people: the fix type, the satellites and the DOPs, as key/value pairs (see
// docs/drivers.md). Level OK with a 3D fix, WARN otherwise.
void fillStatus(
  const GpsFix & fix, const std::string & hardware_id, diagnostic_msgs::msg::DiagnosticStatus & msg);

// "none", "2d" or "3d".
const char * fixName(FixType fix);

}  // namespace prowl_gps_driver
