// Plain test executable for prowl_brain's ROS-free core: exits non-zero if any check fails.
// The matcher tests use a made-up rectangular room, so no robot is needed.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

#include "prowl_brain/ceres_scan_matcher.hpp"
#include "prowl_brain/local_map.hpp"
#include "prowl_brain/matcher_thread.hpp"
#include "prowl_brain/motion_predictor.hpp"
#include "prowl_brain/sensor_buffer.hpp"
#include "prowl_brain/state_machine.hpp"

using namespace prowl_brain;

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

#define EXPECT(expr) check((expr), #expr, __LINE__)

bool near(double a, double b, double tol = 1e-9)
{
  return std::abs(a - b) <= tol;
}

ImuSample imuAt(double stamp, double gyro_z)
{
  ImuSample s;
  s.stamp = stamp;
  s.gyro = {0.0, 0.0, gyro_z};
  return s;
}

// A 5 m x 3 m room centered on the origin, with a 0.6 m box near one corner so it
// doesn't look the same turned around. Returns the distance from (x, y) along `angle`
// to the first wall.
double raycast(double x, double y, double angle)
{
  const double dx = std::cos(angle);
  const double dy = std::sin(angle);
  double best = std::numeric_limits<double>::infinity();
  const auto hitX = [&](double wall, double y_lo, double y_hi) {
      if (std::abs(dx) < 1e-12) {
        return;
      }
      const double t = (wall - x) / dx;
      const double hy = y + t * dy;
      if (t > 0.0 && hy >= y_lo && hy <= y_hi) {
        best = std::min(best, t);
      }
    };
  const auto hitY = [&](double wall, double x_lo, double x_hi) {
      if (std::abs(dy) < 1e-12) {
        return;
      }
      const double t = (wall - y) / dy;
      const double hx = x + t * dx;
      if (t > 0.0 && hx >= x_lo && hx <= x_hi) {
        best = std::min(best, t);
      }
    };
  hitX(-2.5, -1.5, 1.5);
  hitX(2.5, -1.5, 1.5);
  hitY(-1.5, -2.5, 2.5);
  hitY(1.5, -2.5, 2.5);
  // The box: x 1.2..1.8, y 0.4..1.0.
  hitX(1.2, 0.4, 1.0);
  hitX(1.8, 0.4, 1.0);
  hitY(0.4, 1.2, 1.8);
  hitY(1.0, 1.2, 1.8);
  return best;
}

// What the LD19 would report from `pose`, laid out like Prowl's driver does it.
LidarFrame scanFrom(const Pose2D & pose, double stamp)
{
  LidarFrame f;
  f.stamp = stamp;
  f.angle_min = 0.0;
  f.angle_increment = -2.0 * kPi / static_cast<double>(kLidarBins);
  f.range_min = 0.02;
  f.range_max = 12.0;
  f.ranges.resize(kLidarBins);
  f.intensities.assign(kLidarBins, 200.0f);
  for (std::size_t i = 0; i < kLidarBins; ++i) {
    const double a = f.angle_min + static_cast<double>(i) * f.angle_increment;
    f.ranges[i] = static_cast<float>(raycast(pose.x, pose.y, pose.yaw + a));
  }
  return f;
}

double cellAt(const LocalMap & map, double x, double y)
{
  double col = 0.0;
  double row = 0.0;
  map.worldToGrid(x, y, col, row);
  const int c = static_cast<int>(std::floor(col + 0.5));
  const int r = static_cast<int>(std::floor(row + 0.5));
  return map.cells()[static_cast<std::size_t>(r * map.cols() + c)];
}

void testSensorBuffer()
{
  std::printf("SensorBuffer\n");
  SensorBuffer b(5);
  EXPECT(!b.latestLidar());
  EXPECT(!b.latestImuStamp());
  EXPECT(!b.takeUnmatchedLidar());

  LidarFrame f;
  f.stamp = 1.0;
  f.ranges.assign(kLidarBins, 1.0f);
  b.pushLidar(f);
  EXPECT(b.latestLidarStamp() && *b.latestLidarStamp() == 1.0);
  const auto taken = b.takeUnmatchedLidar();
  EXPECT(taken && taken->ranges.size() == kLidarBins);
  EXPECT(!b.takeUnmatchedLidar());  // the same spin isn't handed out twice
  EXPECT(b.latestLidar().has_value());  // but thread 1 can still see it

  // Stamps in 1/8 s steps, which doubles hold exactly, so the boundaries are clear-cut.
  for (int i = 1; i <= 8; ++i) {
    b.pushImu(imuAt(0.125 * i, 0.0));
  }
  EXPECT(b.imuCount() == 8);
  EXPECT(b.latestImuStamp() && *b.latestImuStamp() == 1.0);
  const auto between = b.imuBetween(0.5, 0.875);  // capacity 5 kept 0.5..1.0; t0 excluded, t1 included
  EXPECT(between.size() == 3);
  EXPECT(!between.empty() && between.front().stamp == 0.625 && between.back().stamp == 0.875);
  EXPECT(b.imuBetween(0.0, 0.25).empty());  // already forgotten

  b.clear();
  EXPECT(!b.latestLidar() && !b.latestImu());
  EXPECT(b.lidarCount() == 1 && b.imuCount() == 8);  // counters survive a clear
}

FrameInputs healthy(double now)
{
  FrameInputs in;
  in.now = now;
  in.lidar_age = 0.15;
  in.imu_age = 0.01;
  in.motor_status_age = 0.02;
  return in;
}

void testStateMachine()
{
  std::printf("StateMachine\n");
  StateMachine sm(StateMachineConfig{});
  EXPECT(std::string(toString(State::Tracking)) == "Tracking");

  FrameInputs in = healthy(10.0);
  in.have_match = true;  // the matcher can beat thread 1's first frame...
  in.match_age = 0.1;
  EXPECT(sm.step(in) == State::BuildingFirstMap);  // ...but the first frame only enters the first state
  EXPECT(sm.justChanged());
  in.have_match = false;
  in.now = 10.1;
  sm.step(in);
  EXPECT(!sm.justChanged());
  EXPECT(near(sm.timeInState(10.1), 0.1));

  in.have_match = true;
  EXPECT(sm.step(in) == State::Tracking && sm.justChanged());

  in.match_age = 5.0;  // matcher failing: warn only, stay
  EXPECT(sm.step(in) == State::Tracking);

  in.imu_age = 0.6;
  EXPECT(sm.step(in) == State::SensorLost && sm.justChanged());
  EXPECT(sm.step(in) == State::SensorLost && !sm.justChanged());
  in.imu_age = 0.01;
  EXPECT(sm.step(in) == State::BuildingFirstMap);  // starts over, even though have_match is true

  in.lidar_age = 1e9;
  EXPECT(sm.step(in) == State::SensorLost);

  in.shutdown_requested = true;
  EXPECT(sm.step(in) == State::Stopped && sm.justChanged());
  in = healthy(20.0);
  EXPECT(sm.step(in) == State::Stopped && !sm.justChanged());  // no coming back

  StateMachine early(StateMachineConfig{});
  in.shutdown_requested = true;
  EXPECT(early.step(in) == State::Stopped);  // shutdown wins even on the first frame
}

void testMotionPredictor()
{
  std::printf("MotionPredictor\n");
  MotionPredictor p;
  EXPECT(!p.lastPose());
  MotionGuess g = p.predict(1.0, {});
  EXPECT(g.imu_samples == 0 && g.pose.x == 0.0 && g.pose.yaw == 0.0);

  MatchResult m;
  m.stamp = 1.0;
  m.pose = {1.0, 2.0, 0.0};
  p.addMatch(m);
  m.stamp = 1.1;
  m.pose = {1.05, 2.0, 0.0};  // 0.5 m/s along x
  p.addMatch(m);

  // 0.5 rad/s for 0.1 s, in 10 readings.
  std::vector<ImuSample> imu;
  for (int i = 1; i <= 10; ++i) {
    imu.push_back(imuAt(1.1 + 0.01 * i, 0.5));
  }
  g = p.predict(1.2, imu);
  EXPECT(near(g.delta_yaw, 0.05, 1e-9));
  EXPECT(g.imu_samples == 10);
  EXPECT(near(g.pose.x, 1.10, 1e-9) && near(g.pose.y, 2.0, 1e-9));
  EXPECT(near(g.pose.yaw, 0.05, 1e-9));

  // The gap after the last reading reuses its rate.
  imu.resize(5);  // readings up to 1.15
  EXPECT(near(p.predict(1.2, imu).delta_yaw, 0.05, 1e-9));

  // Yaw wraps into -pi..pi.
  m.pose = {0.0, 0.0, kPi - 0.01};
  m.stamp = 5.0;  // a 3.9 s gap: too long to trust the speed
  p.addMatch(m);
  g = p.predict(5.1, {imuAt(5.1, 0.5)});
  EXPECT(near(g.pose.yaw, -kPi + 0.04, 1e-9));
  EXPECT(g.pose.x == 0.0);

  p.reset();
  EXPECT(!p.lastPose());
}

void testLocalMap()
{
  std::printf("LocalMap\n");
  LocalMapConfig cfg;  // 20 m, 5 cm: 400 x 400
  LocalMap map(cfg);
  EXPECT(map.rows() == 400 && map.cols() == 400);
  EXPECT(map.empty());

  double col = 0.0;
  double row = 0.0;
  map.worldToGrid(0.025, 0.025, col, row);  // center of the cell just up-right of the origin
  EXPECT(near(col, 200.0, 1e-9) && near(row, 200.0, 1e-9));

  map.insertScan(scanFrom({0.0, 0.0, 0.0}, 0.0), {0.0, 0.0, 0.0});
  EXPECT(!map.empty() && map.scansInserted() == 1);
  EXPECT(cellAt(map, 2.49, 0.0) > 0.5);  // the +x wall
  EXPECT(cellAt(map, -2.49, 0.3) > 0.5);  // the -x wall
  EXPECT(cellAt(map, 1.0, 0.0) < 0.5);  // floor between us and it
  EXPECT(cellAt(map, 1.5, 0.9) == 0.5);  // inside the box: never seen
  EXPECT(cellAt(map, 4.0, 4.0) == 0.5);  // outside the room

  bool in_range = true;
  for (double v : map.cells()) {
    in_range = in_range && v >= 0.0 && v <= 1.0;
  }
  EXPECT(in_range);

  // The picture: the 5 x 3 m room plus a 1 m margin, at 5 cm a pixel, about 140 x 100.
  const MapImage img = map.image({0.5, -0.25, 0.3}, 7.0, 1.0);
  std::printf("  map image %dx%d\n", img.width, img.height);
  EXPECT(img.width >= 138 && img.width <= 144 && img.height >= 98 && img.height <= 104);
  EXPECT(img.pixels.size() == static_cast<std::size_t>(img.width * img.height));
  EXPECT(img.stamp == 7.0 && img.robot_yaw == 0.3 && img.resolution == 0.05);
  // The image is centered on the room, so the robot sits right of and below the middle.
  EXPECT(near(img.robot_x, img.width / 2.0 + 0.5 / 0.05, 1.5));
  EXPECT(near(img.robot_y, img.height / 2.0 + 0.25 / 0.05, 1.5));
  const auto pixel = [&](double x, double y) {
      // Map meters -> image pixels, using the robot as the reference point.
      const int px = static_cast<int>(std::floor(img.robot_x + (x - 0.5) / 0.05));
      const int py = static_cast<int>(std::floor(img.robot_y - (y + 0.25) / 0.05));
      return img.pixels[static_cast<std::size_t>(py * img.width + px)];
    };
  EXPECT(pixel(1.0, 0.0) > 200);  // floor is light
  EXPECT(pixel(2.49, 0.0) < 100);  // the +x wall is dark
  EXPECT(pixel(0.0, 1.49) < 100);  // the +y wall is at the top...
  EXPECT(pixel(0.0, -1.49) < 100);  // ...and the -y one at the bottom
  EXPECT(pixel(1.5, 0.9) == 128);  // inside the box: unknown grey

  // Nothing seen yet: a small picture around the robot, all unknown.
  LocalMap blank(cfg);
  const MapImage nothing = blank.image({0.0, 0.0, 0.0}, 0.0, 1.0);
  EXPECT(nothing.width == 41 && nothing.height == 41);
  EXPECT(nothing.pixels.front() == 128);
  // A robot that slid off the map still gives a valid picture.
  EXPECT(blank.image({50.0, -50.0, 0.0}, 0.0, 1.0).width > 0);

  map.reset();
  EXPECT(map.empty() && cellAt(map, 2.49, 0.0) == 0.5);
}

void testScanMatcher()
{
  std::printf("CeresScanMatcher\n");
  CeresScanMatcher matcher(CeresScanMatcherOptions{});

  const LidarFrame first = scanFrom({0.0, 0.0, 0.0}, 0.0);
  const auto points = matcher.scanToPoints(first);
  EXPECT(points.size() == kLidarBins);
  // Bin 180 is 90 degrees clockwise: the robot's right, the -y wall 1.5 m away.
  EXPECT(near(points[180].x, 0.0, 1e-5) && near(points[180].y, -1.5, 1e-5));

  LocalMap map(LocalMapConfig{});
  map.insertScan(first, {0.0, 0.0, 0.0});

  // The robot moved; the guess says it didn't. The LiDAR should pull it to the truth.
  const Pose2D truth{0.08, -0.05, 0.06};
  MotionGuess guess;
  const MatchResult r = matcher.match(scanFrom(truth, 0.1), guess, map);
  std::printf("  matched (%.3f, %.3f, %.3f) vs truth (%.3f, %.3f, %.3f), %d iterations, %.1f ms\n",
    r.pose.x, r.pose.y, r.pose.yaw, truth.x, truth.y, truth.yaw, r.iterations, r.solve_time * 1e3);
  EXPECT(r.converged);
  EXPECT(near(r.stamp, 0.1));
  EXPECT(near(r.pose.x, truth.x, 0.02) && near(r.pose.y, truth.y, 0.02));
  EXPECT(near(r.pose.yaw, truth.yaw, 0.01));
  EXPECT(r.solve_time < 0.1);

  // A gyro guess that's right helps; with the LiDAR switched off it's all that's left.
  CeresScanMatcherOptions imu_only;
  imu_only.weights.lidar = 0.0;
  imu_only.weights.motion_translation = 0.0;
  matcher.setOptions(imu_only);
  guess.pose = {0.0, 0.0, 0.3};
  guess.imu_samples = 10;
  EXPECT(near(matcher.match(scanFrom(truth, 0.1), guess, map).pose.yaw, 0.3, 1e-6));

  // A blind LiDAR (everything +inf) can't be matched.
  matcher.setOptions(CeresScanMatcherOptions{});
  LidarFrame blind = first;
  blind.ranges.assign(kLidarBins, std::numeric_limits<float>::infinity());
  EXPECT(!matcher.match(blind, MotionGuess{}, map).converged);
}

void testMatcherThread()
{
  std::printf("MatcherThread\n");
  SensorBuffer sensors;
  MatcherThread thread(sensors, CeresScanMatcherOptions{}, LocalMapConfig{}, 20.0);
  thread.start();

  const auto waitFor = [&](auto done) {
      for (int i = 0; i < 200 && !done(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return done();
    };

  // The first scan defines the origin.
  sensors.pushLidar(scanFrom({0.0, 0.0, 0.0}, 1.0));
  EXPECT(waitFor([&] {return thread.latestResult().has_value();}));
  const auto first = thread.latestResult();
  EXPECT(first && near(first->pose.x, 0.0) && near(first->pose.yaw, 0.0));

  // Drive forward and turn left a little, with the gyro reporting the turn.
  const Pose2D truth{0.05, 0.0, 0.04};
  for (int i = 1; i <= 10; ++i) {
    sensors.pushImu(imuAt(1.0 + 0.01 * i, 0.4));
  }
  sensors.pushLidar(scanFrom(truth, 1.1));
  EXPECT(waitFor([&] {auto r = thread.latestResult(); return r && r->stamp == 1.1;}));
  const auto second = thread.latestResult();
  EXPECT(second && near(second->pose.x, truth.x, 0.02) && near(second->pose.yaw, truth.yaw, 0.01));

  thread.requestReset();
  EXPECT(!thread.latestResult());  // cleared at once, not on the next run
  sensors.pushLidar(scanFrom(truth, 1.2));
  EXPECT(waitFor([&] {return thread.latestResult().has_value();}));
  const auto after_reset = thread.latestResult();
  EXPECT(after_reset && near(after_reset->pose.x, 0.0) && after_reset->stamp == 1.2);  // a fresh origin

  thread.stop();
  const MatcherStats s = thread.stats();
  EXPECT(s.matches == 3 && s.failures == 0 && s.skipped > 0);
  EXPECT(!thread.latestMapImage());  // no pictures unless asked for

  // With a picture period, thread 2 hands pictures over too, and a reset clears them.
  SensorBuffer sensors2;
  MatcherThread pictures(sensors2, CeresScanMatcherOptions{}, LocalMapConfig{}, 20.0, 0.01);
  pictures.start();
  sensors2.pushLidar(scanFrom({0.0, 0.0, 0.0}, 3.0));
  EXPECT(waitFor([&] {return pictures.latestMapImage() != nullptr;}));
  const auto pic = pictures.latestMapImage();
  EXPECT(pic && pic->stamp == 3.0 && pic->width > 100);
  pictures.requestReset();
  EXPECT(!pictures.latestMapImage());
  pictures.stop();
}

}  // namespace

int main()
{
  testSensorBuffer();
  testStateMachine();
  testMotionPredictor();
  testLocalMap();
  testScanMatcher();
  testMatcherThread();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
