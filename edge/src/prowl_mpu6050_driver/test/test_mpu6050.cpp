// Hardware-free checks of the MPU6050 driver: register encoding, big-endian
// parsing and raw -> SI conversion. Exits non-zero if anything fails.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "prowl_mpu6050_driver/conversions.hpp"
#include "prowl_mpu6050_driver/gyro_bias.hpp"
#include "prowl_mpu6050_driver/mpu6050.hpp"
#include "prowl_mpu6050_driver/registers.hpp"
#include "window_stats.hpp"

using namespace prowl_mpu6050_driver;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char * expr, const char * file, int line)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL %s:%d: %s\n", file, line, expr);
  }
}

void checkNear(double actual, double expected, double tol, const char * expr, const char * file, int line)
{
  ++g_checks;
  if (!(std::abs(actual - expected) <= tol)) {
    ++g_failures;
    std::printf("FAIL %s:%d: %s = %.9g, expected %.9g (+-%g)\n", file, line, expr, actual, expected, tol);
  }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tol) checkNear((actual), (expected), (tol), #actual, __FILE__, __LINE__)

void testBigEndian()
{
  CHECK(beInt16(0x00, 0x00) == 0);
  CHECK(beInt16(0x00, 0x01) == 1);
  CHECK(beInt16(0x01, 0x00) == 256);
  CHECK(beInt16(0x7F, 0xFF) == 32767);
  CHECK(beInt16(0x80, 0x00) == -32768);
  CHECK(beInt16(0xFF, 0xFF) == -1);
  CHECK(beInt16(0xFF, 0x00) == -256);
  CHECK(beInt16(0xC0, 0x00) == -16384);
  CHECK(beInt16(0x12, 0x34) == 0x1234);
  CHECK(beInt16(0xED, 0xCC) == -0x1234);
}

void testParseSample()
{
  // ACCEL X/Y/Z, TEMP, GYRO X/Y/Z, high byte first.
  const uint8_t bytes[kDataBytes] = {
    0x40, 0x00,  // ax  16384
    0xC0, 0x00,  // ay -16384
    0x00, 0x01,  // az  1
    0xF2, 0x5C,  // temp -3492
    0x00, 0x83,  // gx  131
    0xFF, 0x7D,  // gy -131
    0x80, 0x00,  // gz -32768
  };
  const RawSample s = parseSample(bytes);
  CHECK(s.accel[0] == 16384);
  CHECK(s.accel[1] == -16384);
  CHECK(s.accel[2] == 1);
  CHECK(s.temperature == -3492);
  CHECK(s.gyro[0] == 131);
  CHECK(s.gyro[1] == -131);
  CHECK(s.gyro[2] == -32768);
}

void testRegisterEncoding()
{
  CHECK(accelConfigValue(AccelRange::G2) == 0x00);
  CHECK(accelConfigValue(AccelRange::G4) == 0x08);
  CHECK(accelConfigValue(AccelRange::G8) == 0x10);
  CHECK(accelConfigValue(AccelRange::G16) == 0x18);
  CHECK(gyroConfigValue(GyroRange::Dps250) == 0x00);
  CHECK(gyroConfigValue(GyroRange::Dps500) == 0x08);
  CHECK(gyroConfigValue(GyroRange::Dps1000) == 0x10);
  CHECK(gyroConfigValue(GyroRange::Dps2000) == 0x18);
  // Self-test bits (7:5) and reserved bits (2:0) stay clear.
  for (int r = 0; r < 4; ++r) {
    CHECK((accelConfigValue(static_cast<AccelRange>(r)) & ~bits::kFsMask) == 0);
    CHECK((gyroConfigValue(static_cast<GyroRange>(r)) & ~bits::kFsMask) == 0);
  }
  for (int f = 0; f <= 6; ++f) {
    CHECK(configValue(static_cast<Dlpf>(f)) == f);
  }
  CHECK(configValue(Dlpf::Hz44) == 0x03);

  CHECK(accelFullScaleG(AccelRange::G2) == 2);
  CHECK(accelFullScaleG(AccelRange::G4) == 4);
  CHECK(accelFullScaleG(AccelRange::G8) == 8);
  CHECK(accelFullScaleG(AccelRange::G16) == 16);
  CHECK(gyroFullScaleDps(GyroRange::Dps2000) == 2000);
  CHECK(accelBandwidthHz(Dlpf::Hz260) == 260);
  CHECK(accelBandwidthHz(Dlpf::Hz5) == 5);
  CHECK(gyroBandwidthHz(Dlpf::Hz44) == 42);

  // Full scale over sensitivity is the int16 range.
  for (int r = 0; r < 4; ++r) {
    const auto ar = static_cast<AccelRange>(r);
    CHECK_NEAR(accelLsbPerG(ar) * accelFullScaleG(ar), 32768.0, 1.0);
    const auto gr = static_cast<GyroRange>(r);
    CHECK_NEAR(gyroLsbPerDps(gr) * gyroFullScaleDps(gr), 32768.0, 200.0);
  }
}

void testSampleRate()
{
  CHECK(sampleRateDivider(Dlpf::Hz44, 100.0) == 9);
  CHECK(sampleRateDivider(Dlpf::Hz44, 1000.0) == 0);
  CHECK(sampleRateDivider(Dlpf::Hz44, 200.0) == 4);
  CHECK(sampleRateDivider(Dlpf::Hz44, 50.0) == 19);
  CHECK(sampleRateDivider(Dlpf::Hz44, 2000.0) == 0);
  CHECK(sampleRateDivider(Dlpf::Hz44, 1.0) == 255);
  CHECK(sampleRateDivider(Dlpf::Hz5, 0.0) == 255);
  // DLPF off: gyro output rate is 8 kHz.
  CHECK(sampleRateDivider(Dlpf::Hz260, 100.0) == 79);
  CHECK_NEAR(sampleRateHz(Dlpf::Hz44, 9), 100.0, 1e-9);
  CHECK_NEAR(sampleRateHz(Dlpf::Hz260, 79), 100.0, 1e-9);
  CHECK_NEAR(sampleRateHz(Dlpf::Hz184, 0), 1000.0, 1e-9);
  CHECK_NEAR(sampleRateHz(Dlpf::Hz260, 0), 8000.0, 1e-9);
}

void testConversions()
{
  const double g = kStandardGravity;
  CHECK_NEAR(accelToMps2(16384, AccelRange::G2), g, 1e-9);
  CHECK_NEAR(accelToMps2(8192, AccelRange::G4), g, 1e-9);
  CHECK_NEAR(accelToMps2(4096, AccelRange::G8), g, 1e-9);
  CHECK_NEAR(accelToMps2(2048, AccelRange::G16), g, 1e-9);
  CHECK_NEAR(accelToMps2(-8192, AccelRange::G4), -g, 1e-9);
  CHECK_NEAR(accelToMps2(-32768, AccelRange::G2), -2.0 * g, 1e-9);
  CHECK_NEAR(accelToMps2(0, AccelRange::G16), 0.0, 1e-12);

  const double rad = kDegToRad;
  CHECK_NEAR(gyroToRadps(131, GyroRange::Dps250), rad, 1e-12);
  CHECK_NEAR(gyroToRadps(655, GyroRange::Dps500), 10.0 * rad, 1e-12);
  CHECK_NEAR(gyroToRadps(328, GyroRange::Dps1000), 10.0 * rad, 1e-12);
  CHECK_NEAR(gyroToRadps(-164, GyroRange::Dps2000), -10.0 * rad, 1e-12);
  CHECK_NEAR(gyroToRadps(32767, GyroRange::Dps250) * kRadToDeg, 250.1, 0.05);

  CHECK_NEAR(temperatureC(0), 36.53, 1e-12);
  CHECK_NEAR(temperatureC(340), 37.53, 1e-12);
  CHECK_NEAR(temperatureC(-3492), 26.2594, 1e-3);
  CHECK_NEAR(temperatureC(-12420), 0.0, 0.01);

  CHECK(saturated(32767));
  CHECK(saturated(-32768));
  CHECK(!saturated(32766));
  CHECK(!saturated(0));
}

void testDeviceConvert()
{
  // Mpu6050::convert uses the active ranges and removes the gyro bias.
  Mpu6050 mpu;
  CHECK(mpu.config().accel_range == AccelRange::G4);
  CHECK(mpu.config().gyro_range == GyroRange::Dps500);
  mpu.setGyroBias({0.01, -0.02, 0.0});
  RawSample raw;
  raw.accel = {0, 0, 8192};
  raw.gyro = {655, 0, -655};
  raw.temperature = 340;
  const Sample s = mpu.convert(raw);
  CHECK_NEAR(s.accel[2], kStandardGravity, 1e-9);
  CHECK_NEAR(s.gyro_uncalibrated[0], 10.0 * kDegToRad, 1e-12);
  CHECK_NEAR(s.gyro[0], 10.0 * kDegToRad - 0.01, 1e-12);
  CHECK_NEAR(s.gyro[1], 0.02, 1e-12);
  CHECK_NEAR(s.gyro[2], -10.0 * kDegToRad, 1e-12);
  CHECK_NEAR(s.temperature, 37.53, 1e-12);
  CHECK(s.raw.gyro[2] == -655);
}

void testTilt()
{
  const double g = kStandardGravity;
  CHECK_NEAR(rollFromAccel({0, 0, g}), 0.0, 1e-12);
  CHECK_NEAR(pitchFromAccel({0, 0, g}), 0.0, 1e-12);
  // Rolled 90 deg: gravity along +Y.
  CHECK_NEAR(rollFromAccel({0, g, 0}) * kRadToDeg, 90.0, 1e-9);
  CHECK_NEAR(rollFromAccel({0, -g, 0}) * kRadToDeg, -90.0, 1e-9);
  // Nose down 90 deg: gravity along -X reads as +90 pitch.
  CHECK_NEAR(pitchFromAccel({-g, 0, 0}) * kRadToDeg, 90.0, 1e-9);
  CHECK_NEAR(pitchFromAccel({g, 0, 0}) * kRadToDeg, -90.0, 1e-9);
  // 30 deg roll.
  const double r = 30.0 * kDegToRad;
  CHECK_NEAR(rollFromAccel({0, g * std::sin(r), g * std::cos(r)}) * kRadToDeg, 30.0, 1e-9);
  // Upside down.
  CHECK_NEAR(std::abs(rollFromAccel({0, 0, -g})) * kRadToDeg, 180.0, 1e-9);
  CHECK_NEAR(norm({3.0, 4.0, 12.0}), 13.0, 1e-12);
}

void testGyroBias()
{
  GyroBiasEstimator est(4);
  CHECK(!est.done());
  CHECK(!est.add({0.01, -0.02, 0.0}));
  CHECK(!est.add({0.03, -0.02, 0.0}));
  CHECK(!est.add({0.01, -0.02, 0.0}));
  CHECK(est.add({0.03, -0.02, 0.0}));
  CHECK(est.done());
  CHECK(est.count() == 4);
  est.add({100.0, 100.0, 100.0});  // ignored once done
  CHECK(est.count() == 4);
  CHECK_NEAR(est.bias()[0], 0.02, 1e-12);
  CHECK_NEAR(est.bias()[1], -0.02, 1e-12);
  CHECK_NEAR(est.bias()[2], 0.0, 1e-12);
  // Sample stddev of {1,3,1,3} * 0.01.
  CHECK_NEAR(est.stddev()[0], 0.01 * std::sqrt(4.0 / 3.0), 1e-12);
  CHECK_NEAR(est.stddev()[1], 0.0, 1e-12);
  CHECK(est.still(0.02));
  CHECK(!est.still(0.005));
  est.reset();
  CHECK(est.count() == 0);
  CHECK(!est.done());
}

// 2 s at 100 Hz on a still robot with the LiDAR shaking X by +-8 deg/s at its
// 10 Hz rotation rate, as seen on Prowl's first runs.
void testGyroBiasVibration()
{
  const double bias = 1.5 * kDegToRad;
  const double shake = 8.0 * kDegToRad;
  auto run = [&](size_t block, double nudge_dps) {
      GyroBiasEstimator est(200, block);
      for (int i = 0; i < 200; ++i) {
        const double x = bias + shake * std::sin(2.0 * kPi * 10.0 * i / 100.0 + 0.7);
        // A 0.3 s turn in the middle of the window.
        const double z = (i >= 90 && i < 120) ? nudge_dps * kDegToRad : 0.0;
        est.add({x, 0.0, z});
      }
      CHECK(est.done());
      return est;
    };

  const auto raw = run(1, 0.0);
  CHECK(!raw.still(0.3 * kDegToRad));  // the old check: vibration looks like motion

  const auto blocks = run(10, 0.0);
  CHECK(blocks.still(0.3 * kDegToRad));
  CHECK_NEAR(blocks.bias()[0], bias, 1e-3 * kDegToRad);
  CHECK(blocks.stddev()[0] > 5.0 * kDegToRad);  // the vibration still shows as noise

  // Turning 2 deg is still caught.
  CHECK(!run(10, 2.0 / 0.3).still(0.3 * kDegToRad));

  // The window is rounded up to whole blocks, at least two.
  CHECK(GyroBiasEstimator(205, 10).target() == 210);
  CHECK(GyroBiasEstimator(1, 10).target() == 20);
  CHECK(GyroBiasEstimator(200, 0).block() == 1);
}

void testWindowStats()
{
  prowl_mpu6050_tui::WindowStats w(3);
  CHECK(w.summary().count == 0);
  w.add(1.0);
  w.add(2.0);
  w.add(3.0);
  w.add(10.0);  // pushes out 1.0
  const auto s = w.summary();
  CHECK(s.count == 3);
  CHECK_NEAR(s.mean, 5.0, 1e-12);
  CHECK_NEAR(s.min, 2.0, 1e-12);
  CHECK_NEAR(s.max, 10.0, 1e-12);
  CHECK_NEAR(s.stddev, std::sqrt(19.0), 1e-12);
  w.setCapacity(1);
  CHECK(w.size() == 1);
  CHECK_NEAR(w.summary().mean, 10.0, 1e-12);
  CHECK_NEAR(w.summary().stddev, 0.0, 1e-12);
}

}  // namespace

int main()
{
  testBigEndian();
  testParseSample();
  testRegisterEncoding();
  testSampleRate();
  testConversions();
  testDeviceConvert();
  testTilt();
  testGyroBias();
  testGyroBiasVibration();
  testWindowStats();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
