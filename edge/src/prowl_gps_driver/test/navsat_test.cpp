// Plain test executable for the GpsFix -> NavSatFix, TwistStamped and DiagnosticStatus conversions: exits
// non-zero if any check fails.

#include <cmath>
#include <cstdio>
#include <string>

#include "prowl_gps_driver/navsat.hpp"

using namespace prowl_gps_driver;
using sensor_msgs::msg::NavSatFix;
using sensor_msgs::msg::NavSatStatus;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char * what, int line)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL line %d: %s\n", line, what);
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

bool near(double a, double b, double tol = 1e-9)
{
  return std::abs(a - b) <= tol;
}

std::string value(const diagnostic_msgs::msg::DiagnosticStatus & msg, const std::string & key)
{
  for (const auto & pair : msg.values) {
    if (pair.key == key) {
      return pair.value;
    }
  }
  return "<missing>";
}

GpsFix threeD()
{
  GpsFix f;
  f.fix = FixType::Fix3D;
  f.quality = 1;
  f.utc = 1039426045.0;
  f.latitude = 47.285233;
  f.longitude = 8.565265;
  f.altitude_msl = 499.6;
  f.geoid_separation = 48.0;
  f.satellites_used = 2;
  f.hdop = 1.2;
  f.vdop = 2.0;
  f.pdop = 2.3;
  f.speed_mps = 2.0;
  f.course_deg = 90.0;
  Satellite a;
  a.id = 7;
  a.elevation = 29;
  a.azimuth = 116;
  a.cno = 41;
  a.used = true;
  Satellite b;
  b.id = 10;
  b.elevation = 7;
  b.azimuth = 189;
  f.satellites = {a, b};
  return f;
}

void testNavSatFix()
{
  std::printf("navsatfix\n");
  NavSatFix msg;
  GpsFix f = threeD();
  fillNavSatFix(f, 2.5, msg);
  CHECK(msg.status.status == NavSatStatus::STATUS_FIX);
  CHECK(msg.status.service == NavSatStatus::SERVICE_GPS);
  CHECK(near(msg.latitude, 47.285233));
  CHECK(near(msg.longitude, 8.565265));
  CHECK(near(msg.altitude, 547.6));  // above the ellipsoid
  CHECK(msg.position_covariance_type == NavSatFix::COVARIANCE_TYPE_APPROXIMATED);
  CHECK(near(msg.position_covariance[0], 9.0));  // (1.2 x 2.5)²
  CHECK(near(msg.position_covariance[4], 9.0));
  CHECK(near(msg.position_covariance[8], 25.0));  // (2.0 x 2.5)²
  CHECK(msg.position_covariance[1] == 0.0);

  // Differential, GLONASS, no VDOP.
  f.quality = 2;
  f.talker = "GL";
  f.vdop.reset();
  fillNavSatFix(f, 2.5, msg);
  CHECK(msg.status.status == NavSatStatus::STATUS_SBAS_FIX);
  CHECK(msg.status.service == NavSatStatus::SERVICE_GLONASS);
  CHECK(near(msg.position_covariance[8], 36.0));  // twice the horizontal

  // No HDOP: covariance unknown.
  f.hdop.reset();
  fillNavSatFix(f, 2.5, msg);
  CHECK(msg.position_covariance_type == NavSatFix::COVARIANCE_TYPE_UNKNOWN);
  CHECK(msg.position_covariance[0] == 0.0);

  // No fix.
  fillNavSatFix(GpsFix{}, 2.5, msg);
  CHECK(msg.status.status == NavSatStatus::STATUS_NO_FIX);
  CHECK(std::isnan(msg.latitude) && std::isnan(msg.longitude) && std::isnan(msg.altitude));
  CHECK(msg.position_covariance_type == NavSatFix::COVARIANCE_TYPE_UNKNOWN);
}

void testVelocity()
{
  std::printf("velocity\n");
  geometry_msgs::msg::TwistStamped msg;
  GpsFix f = threeD();
  CHECK(fillVelocity(f, msg));
  CHECK(near(msg.twist.linear.x, 2.0, 1e-12));  // course 90: due east
  CHECK(near(msg.twist.linear.y, 0.0, 1e-12));
  f.course_deg = 180.0;
  CHECK(fillVelocity(f, msg));
  CHECK(near(msg.twist.linear.y, -2.0, 1e-12));  // due south
  f.course_deg.reset();
  CHECK(!fillVelocity(f, msg));
  CHECK(!fillVelocity(GpsFix{}, msg));
}

void testStatus()
{
  std::printf("status\n");
  using diagnostic_msgs::msg::DiagnosticStatus;
  DiagnosticStatus msg;
  fillStatus(threeD(), "u-blox 7 on /dev/ttyAMA4", msg);
  CHECK(msg.level == DiagnosticStatus::OK);
  CHECK(msg.hardware_id == "u-blox 7 on /dev/ttyAMA4");
  CHECK(msg.message == "3D fix from 2 satellites");
  CHECK(value(msg, "fix") == "3d");
  CHECK(value(msg, "quality") == "1");
  CHECK(value(msg, "utc") == "1039426045.00");
  CHECK(value(msg, "altitude_msl") == "499.6");
  CHECK(value(msg, "satellites_used") == "2");
  CHECK(value(msg, "satellites_in_view") == "2");
  CHECK(value(msg, "hdop") == "1.20");
  CHECK(value(msg, "vdop") == "2.00");
  CHECK(value(msg, "pdop") == "2.30");
  CHECK(value(msg, "satellites") == "[[7,29,116,41,true],[10,7,189,null,false]]");

  // Filling again replaces the values rather than adding to them.
  GpsFix none;
  none.hdop = 99.99;
  fillStatus(none, "u-blox 7", msg);
  CHECK(msg.level == DiagnosticStatus::WARN);
  CHECK(msg.message == "no fix yet: 0 satellites in view");
  CHECK(msg.values.size() == 10);
  CHECK(value(msg, "fix") == "none");
  CHECK(value(msg, "utc").empty());
  CHECK(value(msg, "altitude_msl").empty());
  CHECK(value(msg, "hdop") == "99.99");
  CHECK(value(msg, "satellites") == "[]");

  GpsFix two = threeD();
  two.fix = FixType::Fix2D;
  fillStatus(two, "u-blox 7", msg);
  CHECK(msg.level == DiagnosticStatus::WARN);
  CHECK(value(msg, "fix") == "2d");
}

}  // namespace

int main()
{
  testNavSatFix();
  testVelocity();
  testStatus();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
