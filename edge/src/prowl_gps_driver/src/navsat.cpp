#include "prowl_gps_driver/navsat.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>

namespace prowl_gps_driver
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

std::string number(const std::optional<double> & value, const char * format)
{
  if (!value) {
    return "";
  }
  char buf[48];
  std::snprintf(buf, sizeof(buf), format, *value);
  return buf;
}

std::string jsonInt(const std::optional<int> & value)
{
  return value ? std::to_string(*value) : "null";
}

// [[id, elevation, azimuth, cno, used], ...], with null for what the receiver didn't say.
std::string satellitesJson(const std::vector<Satellite> & satellites)
{
  std::string out = "[";
  for (size_t i = 0; i < satellites.size(); ++i) {
    const Satellite & s = satellites[i];
    if (i > 0) {
      out += ",";
    }
    out += "[" + std::to_string(s.id) + "," + jsonInt(s.elevation) + "," + jsonInt(s.azimuth) + "," +
      jsonInt(s.cno) + "," + (s.used ? "true" : "false") + "]";
  }
  return out + "]";
}

void add(diagnostic_msgs::msg::DiagnosticStatus & msg, const std::string & key, const std::string & value)
{
  diagnostic_msgs::msg::KeyValue pair;
  pair.key = key;
  pair.value = value;
  msg.values.push_back(pair);
}

}  // namespace

const char * fixName(FixType fix)
{
  switch (fix) {
    case FixType::Fix2D: return "2d";
    case FixType::Fix3D: return "3d";
    default: return "none";
  }
}

void fillNavSatFix(const GpsFix & fix, double uere, sensor_msgs::msg::NavSatFix & msg)
{
  using sensor_msgs::msg::NavSatFix;
  using sensor_msgs::msg::NavSatStatus;
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();

  msg.status.service = fix.talker == "GL" ? NavSatStatus::SERVICE_GLONASS : NavSatStatus::SERVICE_GPS;
  msg.position_covariance.fill(0.0);
  if (fix.fix == FixType::None) {
    msg.status.status = NavSatStatus::STATUS_NO_FIX;
    msg.latitude = nan;
    msg.longitude = nan;
    msg.altitude = nan;
    msg.position_covariance_type = NavSatFix::COVARIANCE_TYPE_UNKNOWN;
    return;
  }

  msg.status.status = fix.quality == 2 ? NavSatStatus::STATUS_SBAS_FIX : NavSatStatus::STATUS_FIX;
  msg.latitude = *fix.latitude;
  msg.longitude = *fix.longitude;
  msg.altitude = fix.altitude_msl ? *fix.altitude_msl + fix.geoid_separation.value_or(0.0) : nan;

  if (fix.hdop) {
    const double horizontal = *fix.hdop * uere;
    const double vertical = fix.vdop ? *fix.vdop * uere : 2.0 * horizontal;
    msg.position_covariance[0] = horizontal * horizontal;
    msg.position_covariance[4] = horizontal * horizontal;
    msg.position_covariance[8] = vertical * vertical;
    msg.position_covariance_type = NavSatFix::COVARIANCE_TYPE_APPROXIMATED;
  } else {
    msg.position_covariance_type = NavSatFix::COVARIANCE_TYPE_UNKNOWN;
  }
}

bool fillVelocity(const GpsFix & fix, geometry_msgs::msg::TwistStamped & msg)
{
  if (!fix.speed_mps || !fix.course_deg) {
    return false;
  }
  const double course = *fix.course_deg * kPi / 180.0;
  msg.twist.linear.x = *fix.speed_mps * std::sin(course);
  msg.twist.linear.y = *fix.speed_mps * std::cos(course);
  msg.twist.linear.z = 0.0;
  msg.twist.angular.x = 0.0;
  msg.twist.angular.y = 0.0;
  msg.twist.angular.z = 0.0;
  return true;
}

void fillStatus(
  const GpsFix & fix, const std::string & hardware_id, diagnostic_msgs::msg::DiagnosticStatus & msg)
{
  using diagnostic_msgs::msg::DiagnosticStatus;
  msg.name = "gps_driver: receiver";
  msg.hardware_id = hardware_id;

  const size_t in_view = fix.satellites.size();
  char text[96];
  if (fix.fix == FixType::None) {
    msg.level = DiagnosticStatus::WARN;
    std::snprintf(text, sizeof(text), "no fix yet: %zu satellites in view", in_view);
  } else {
    msg.level = fix.fix == FixType::Fix3D ? DiagnosticStatus::OK : DiagnosticStatus::WARN;
    std::snprintf(text, sizeof(text), "%s fix from %d satellites", fix.fix == FixType::Fix3D ? "3D" : "2D",
      fix.satellites_used);
  }
  msg.message = text;

  msg.values.clear();
  add(msg, "fix", fixName(fix.fix));
  add(msg, "quality", std::to_string(fix.quality));
  add(msg, "utc", number(fix.utc, "%.2f"));
  add(msg, "altitude_msl", number(fix.altitude_msl, "%.1f"));
  add(msg, "satellites_used", std::to_string(fix.satellites_used));
  add(msg, "satellites_in_view", std::to_string(in_view));
  add(msg, "hdop", number(fix.hdop, "%.2f"));
  add(msg, "vdop", number(fix.vdop, "%.2f"));
  add(msg, "pdop", number(fix.pdop, "%.2f"));
  add(msg, "satellites", satellitesJson(fix.satellites));
}

}  // namespace prowl_gps_driver
