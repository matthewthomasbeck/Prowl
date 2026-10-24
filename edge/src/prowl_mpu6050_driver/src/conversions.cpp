#include "prowl_mpu6050_driver/conversions.hpp"

#include <cmath>
#include <limits>

namespace prowl_mpu6050_driver
{

int16_t beInt16(uint8_t high, uint8_t low)
{
  const uint16_t u = static_cast<uint16_t>((high << 8) | low);
  // Well defined for every value, unlike a narrowing cast before C++20.
  return u < 0x8000 ? static_cast<int16_t>(u) : static_cast<int16_t>(static_cast<int>(u) - 0x10000);
}

RawSample parseSample(const uint8_t * bytes)
{
  RawSample s;
  for (int i = 0; i < 3; ++i) {
    s.accel[i] = beInt16(bytes[2 * i], bytes[2 * i + 1]);
    s.gyro[i] = beInt16(bytes[8 + 2 * i], bytes[9 + 2 * i]);
  }
  s.temperature = beInt16(bytes[6], bytes[7]);
  return s;
}

double accelToMps2(int16_t raw, AccelRange range)
{
  return raw / accelLsbPerG(range) * kStandardGravity;
}

double gyroToRadps(int16_t raw, GyroRange range)
{
  return raw / gyroLsbPerDps(range) * kDegToRad;
}

double temperatureC(int16_t raw)
{
  return raw / 340.0 + 36.53;
}

bool saturated(int16_t raw)
{
  return raw == std::numeric_limits<int16_t>::max() || raw == std::numeric_limits<int16_t>::min();
}

double norm(const Vec3 & v)
{
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double rollFromAccel(const Vec3 & a)
{
  return std::atan2(a[1], a[2]);
}

double pitchFromAccel(const Vec3 & a)
{
  return std::atan2(-a[0], std::hypot(a[1], a[2]));
}

}  // namespace prowl_mpu6050_driver
