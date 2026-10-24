#pragma once

#include <cstdint>
#include <string>

#include "prowl_mpu6050_driver/conversions.hpp"
#include "prowl_mpu6050_driver/i2c_device.hpp"
#include "prowl_mpu6050_driver/registers.hpp"

namespace prowl_mpu6050_driver
{

struct Config
{
  AccelRange accel_range = AccelRange::G4;
  GyroRange gyro_range = GyroRange::Dps500;
  Dlpf dlpf = Dlpf::Hz44;
  double sample_rate_hz = 100.0;  // rounded to what SMPLRT_DIV can give
};

// One reading in board axes and SI units.
struct Sample
{
  Vec3 accel{};              // m/s^2
  Vec3 gyro{};               // rad/s, gyro bias removed
  Vec3 gyro_uncalibrated{};  // rad/s as measured
  double temperature = 0.0;  // deg C
  RawSample raw;
};

enum class ReadStatus {NewSample, NoNewSample, Error};

// MPU-6050 over I2C. Not thread safe; use from one thread.
class Mpu6050
{
public:
  // Opens the bus and reads WHO_AM_I. Clones may report something other than
  // 0x68, so a mismatch is reported by whoAmIOk() rather than failing here.
  bool open(int bus = kDefaultBus, uint8_t address = kDefaultAddress);
  void close() {i2c_.close();}

  // Resets the device, wakes it on the X gyro PLL, applies config and enables
  // the data-ready flag. Takes ~250 ms.
  bool configure(const Config & config);
  // Changes ranges, filter and rate on a running device (no reset) and reads
  // the registers back to check they took.
  bool applyConfig(const Config & config);

  // Burst-reads INT_STATUS and the 14 data registers. NewSample only when the
  // device has produced a sample since the previous poll, so polling faster
  // than the sample rate never returns a reading twice.
  ReadStatus poll(Sample & out);
  // Reads the current data registers regardless of the data-ready flag.
  bool read(Sample & out);

  int bus() const {return bus_;}
  uint8_t address() const {return address_;}
  uint8_t whoAmI() const {return who_am_i_;}
  bool whoAmIOk() const {return who_am_i_ == kWhoAmIValue;}
  const Config & config() const {return config_;}
  uint8_t sampleRateDivider() const {return divider_;}
  double sampleRateHz() const {return prowl_mpu6050_driver::sampleRateHz(config_.dlpf, divider_);}

  void setGyroBias(const Vec3 & bias) {gyro_bias_ = bias;}
  const Vec3 & gyroBias() const {return gyro_bias_;}

  uint64_t errorCount() const {return errors_;}
  const std::string & lastError() const {return last_error_;}

  Sample convert(const RawSample & raw) const;

private:
  bool write(uint8_t reg, uint8_t value);
  bool readBytes(uint8_t reg, uint8_t * data, size_t len);
  bool verify(uint8_t reg, uint8_t expected);
  bool fail(const std::string & what);

  I2cDevice i2c_;
  int bus_ = -1;
  uint8_t address_ = kDefaultAddress;
  uint8_t who_am_i_ = 0;
  Config config_;
  uint8_t divider_ = 0;
  Vec3 gyro_bias_{};
  uint64_t errors_ = 0;
  std::string last_error_;
};

}  // namespace prowl_mpu6050_driver
