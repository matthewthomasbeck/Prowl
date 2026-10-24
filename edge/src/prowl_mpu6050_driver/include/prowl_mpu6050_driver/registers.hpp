#pragma once

#include <cstdint>

// MPU-6050 registers, bit fields and scale factors, from the InvenSense
// register map RM-MPU-6000A-00 rev 4.0.
namespace prowl_mpu6050_driver
{

namespace reg
{
constexpr uint8_t kSmplrtDiv = 0x19;
constexpr uint8_t kConfig = 0x1A;
constexpr uint8_t kGyroConfig = 0x1B;
constexpr uint8_t kAccelConfig = 0x1C;
constexpr uint8_t kIntEnable = 0x38;
constexpr uint8_t kIntStatus = 0x3A;
constexpr uint8_t kAccelXoutH = 0x3B;  // first of the 14 data bytes, through GYRO_ZOUT_L 0x48
constexpr uint8_t kSignalPathReset = 0x68;
constexpr uint8_t kPwrMgmt1 = 0x6B;
constexpr uint8_t kWhoAmI = 0x75;
}  // namespace reg

namespace bits
{
constexpr uint8_t kDeviceReset = 0x80;     // PWR_MGMT_1
constexpr uint8_t kSleep = 0x40;           // PWR_MGMT_1
constexpr uint8_t kClkPllGyroX = 0x01;     // PWR_MGMT_1 CLKSEL = 1
constexpr uint8_t kResetAllPaths = 0x07;   // SIGNAL_PATH_RESET gyro | accel | temp
constexpr uint8_t kDataRdy = 0x01;         // INT_ENABLE DATA_RDY_EN, INT_STATUS DATA_RDY_INT
constexpr uint8_t kFsShift = 3;            // FS_SEL / AFS_SEL live in bits 4:3
constexpr uint8_t kFsMask = 0x18;
constexpr uint8_t kDlpfMask = 0x07;        // CONFIG DLPF_CFG
}  // namespace bits

constexpr uint8_t kWhoAmIValue = 0x68;
constexpr uint8_t kDefaultAddress = 0x68;  // AD0 low; 0x69 with AD0 high
// On Prowl: I2C bus 1 (/dev/i2c-1), SDA on header pin 3 (GPIO2), SCL on pin 5 (GPIO3).
constexpr int kDefaultBus = 1;
constexpr int kDataBytes = 14;

enum class AccelRange : uint8_t {G2 = 0, G4 = 1, G8 = 2, G16 = 3};
enum class GyroRange : uint8_t {Dps250 = 0, Dps500 = 1, Dps1000 = 2, Dps2000 = 3};

// DLPF_CFG 0-6. The value names the accelerometer bandwidth; the gyro's is
// within a few Hz of it. 7 is reserved and not offered.
enum class Dlpf : uint8_t {Hz260 = 0, Hz184, Hz94, Hz44, Hz21, Hz10, Hz5};

constexpr uint8_t accelConfigValue(AccelRange r)
{
  return static_cast<uint8_t>(static_cast<uint8_t>(r) << bits::kFsShift);
}

constexpr uint8_t gyroConfigValue(GyroRange r)
{
  return static_cast<uint8_t>(static_cast<uint8_t>(r) << bits::kFsShift);
}

// EXT_SYNC_SET stays 0 (FSYNC unused).
constexpr uint8_t configValue(Dlpf f)
{
  return static_cast<uint8_t>(f);
}

constexpr double accelLsbPerG(AccelRange r)
{
  constexpr double kLsb[] = {16384.0, 8192.0, 4096.0, 2048.0};
  return kLsb[static_cast<int>(r)];
}

constexpr double gyroLsbPerDps(GyroRange r)
{
  constexpr double kLsb[] = {131.0, 65.5, 32.8, 16.4};
  return kLsb[static_cast<int>(r)];
}

constexpr int accelFullScaleG(AccelRange r)
{
  return 2 << static_cast<int>(r);
}

constexpr int gyroFullScaleDps(GyroRange r)
{
  constexpr int kDps[] = {250, 500, 1000, 2000};
  return kDps[static_cast<int>(r)];
}

constexpr int accelBandwidthHz(Dlpf f)
{
  constexpr int kHz[] = {260, 184, 94, 44, 21, 10, 5};
  return kHz[static_cast<int>(f)];
}

constexpr int gyroBandwidthHz(Dlpf f)
{
  constexpr int kHz[] = {256, 188, 98, 42, 20, 10, 5};
  return kHz[static_cast<int>(f)];
}

// The gyro output rate the sample rate divider divides down.
constexpr double gyroOutputRateHz(Dlpf f)
{
  return f == Dlpf::Hz260 ? 8000.0 : 1000.0;
}

constexpr double sampleRateHz(Dlpf f, uint8_t divider)
{
  return gyroOutputRateHz(f) / (1.0 + divider);
}

// SMPLRT_DIV giving the closest achievable rate to rate_hz.
constexpr uint8_t sampleRateDivider(Dlpf f, double rate_hz)
{
  if (rate_hz <= 0.0) {
    return 255;
  }
  const double div = gyroOutputRateHz(f) / rate_hz - 1.0;
  if (div <= 0.0) {
    return 0;
  }
  if (div >= 255.0) {
    return 255;
  }
  return static_cast<uint8_t>(div + 0.5);
}

}  // namespace prowl_mpu6050_driver
