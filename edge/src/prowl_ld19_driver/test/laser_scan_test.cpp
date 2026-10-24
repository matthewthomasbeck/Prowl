// Plain test executable for the Scan -> sensor_msgs/LaserScan conversion: exits
// non-zero if any check fails.

#include <cmath>
#include <cstdio>

#include "prowl_ld19_driver/laser_scan.hpp"

using namespace prowl_ld19_driver;

namespace
{

constexpr double kPi = 3.14159265358979323846;

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

bool near(double a, double b, double tol = 1e-5)
{
  return std::abs(a - b) <= tol;
}

// Angle of bin i, as a subscriber computes it, wrapped to (-pi, pi].
double binAngle(const sensor_msgs::msg::LaserScan & msg, size_t i)
{
  return std::remainder(msg.angle_min + msg.angle_increment * static_cast<double>(i), 2.0 * kPi);
}

void testLayout()
{
  std::printf("layout\n");
  Scan scan;
  scan.speed_dps = 3600.0;
  sensor_msgs::msg::LaserScan msg;
  fillLaserScan(scan, LaserScanConfig{}, msg);
  CHECK(msg.ranges.size() == 720);
  CHECK(msg.intensities.size() == 720);
  CHECK(near(msg.angle_min, 0.0));
  CHECK(near(msg.angle_increment, -2.0 * kPi / 720.0));
  // REP-compatible: ranges.size() == (angle_max - angle_min) / angle_increment + 1.
  CHECK(std::lround((msg.angle_max - msg.angle_min) / msg.angle_increment) + 1 == 720);
  CHECK(near(msg.scan_time, 0.1));
  CHECK(near(msg.time_increment, 0.1 / 720.0, 1e-9));
  CHECK(near(msg.range_min, 0.02));
  CHECK(near(msg.range_max, 12.0));
  CHECK(std::isinf(msg.ranges[0]) && msg.ranges[0] > 0.0f);
  CHECK(msg.intensities[0] == 0.0f);
}

void testDirections()
{
  std::printf("directions\n");
  // The LD19's angles run clockwise, ROS angles counterclockwise.
  Scan scan;
  scan.speed_dps = 3600.0;
  scan.points = {
    {0.0, 1000, 200},  // front
    {90.0, 2000, 150},  // right
    {270.0, 3000, 100},  // left
    {359.9, 4000, 50},  // just right of front: rounds into bin 0, but further than the front point
  };
  sensor_msgs::msg::LaserScan msg;
  fillLaserScan(scan, LaserScanConfig{}, msg);
  CHECK(near(msg.ranges[0], 1.0));
  CHECK(msg.intensities[0] == 200.0f);
  CHECK(near(binAngle(msg, 0), 0.0));
  CHECK(near(msg.ranges[180], 2.0));
  CHECK(near(binAngle(msg, 180), -kPi / 2.0));
  CHECK(near(msg.ranges[540], 3.0));
  CHECK(near(binAngle(msg, 540), kPi / 2.0));
  CHECK(msg.intensities[540] == 100.0f);
}

void testFiltering()
{
  std::printf("filtering\n");
  Scan scan;
  scan.speed_dps = 3600.0;
  scan.points = {
    {10.0, 0, 200},  // no return
    {20.0, 10, 200},  // closer than range_min
    {30.0, 13000, 200},  // beyond range_max
    {40.0, 1000, 5},  // too weak
    {50.0, 1000, 200},  // kept
  };
  LaserScanConfig config;
  config.min_intensity = 10;
  sensor_msgs::msg::LaserScan msg;
  fillLaserScan(scan, config, msg);
  CHECK(std::isinf(msg.ranges[20]));
  CHECK(std::isinf(msg.ranges[40]));
  CHECK(std::isinf(msg.ranges[60]));
  CHECK(std::isinf(msg.ranges[80]));
  CHECK(near(msg.ranges[100], 1.0));
}

void testBinsAndDuration()
{
  std::printf("bins and duration\n");
  Scan scan;
  scan.speed_dps = 0.0;  // fall back to the device timestamps
  scan.start_time_ms = 1000;
  scan.end_time_ms = 1125;
  scan.points = {{45.0, 500, 1}};
  LaserScanConfig config;
  config.bins = 360;
  sensor_msgs::msg::LaserScan msg;
  fillLaserScan(scan, config, msg);
  CHECK(msg.ranges.size() == 360);
  CHECK(near(msg.scan_time, 0.125));
  CHECK(near(msg.ranges[45], 0.5));
  CHECK(near(binAngle(msg, 45), -kPi / 4.0));
}

}  // namespace

int main()
{
  testLayout();
  testDirections();
  testFiltering();
  testBinsAndDuration();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
