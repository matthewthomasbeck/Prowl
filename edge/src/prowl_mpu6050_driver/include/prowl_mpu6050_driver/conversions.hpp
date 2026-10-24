#pragma once

#include <array>
#include <cstdint>

#include "prowl_mpu6050_driver/registers.hpp"

namespace prowl_mpu6050_driver
{

using Vec3 = std::array<double, 3>;

constexpr double kStandardGravity = 9.80665;  // m/s^2 per g
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

// The 14 data registers as read, in board axes.
struct RawSample
{
  std::array<int16_t, 3> accel{};
  int16_t temperature = 0;
  std::array<int16_t, 3> gyro{};
};

// Registers hold two's-complement values, high byte first.
int16_t beInt16(uint8_t high, uint8_t low);

// bytes: ACCEL_XOUT_H (0x3B) through GYRO_ZOUT_L (0x48).
RawSample parseSample(const uint8_t * bytes);

double accelToMps2(int16_t raw, AccelRange range);
double gyroToRadps(int16_t raw, GyroRange range);
double temperatureC(int16_t raw);

// True when a raw reading sits at the end of its range, i.e. may be clipped.
bool saturated(int16_t raw);

double norm(const Vec3 & v);

// Tilt from the gravity vector, radians, in board axes: roll about X, pitch
// about Y. Only meaningful while the board is not accelerating.
double rollFromAccel(const Vec3 & accel);
double pitchFromAccel(const Vec3 & accel);

}  // namespace prowl_mpu6050_driver
