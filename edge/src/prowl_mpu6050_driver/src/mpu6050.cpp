#include "prowl_mpu6050_driver/mpu6050.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace prowl_mpu6050_driver
{

namespace
{

void sleepMs(int ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string hex(uint8_t v)
{
  char buf[8];
  std::snprintf(buf, sizeof(buf), "0x%02X", v);
  return buf;
}

}  // namespace

bool Mpu6050::open(int bus, uint8_t address)
{
  bus_ = bus;
  address_ = address;
  if (!i2c_.open(bus, address)) {
    return fail(i2c_.lastError());
  }
  if (!readBytes(reg::kWhoAmI, &who_am_i_, 1)) {
    i2c_.close();
    last_error_ = "no answer at " + hex(address) + " on i2c-" + std::to_string(bus) + " (" +
      last_error_ + ")";
    return false;
  }
  return true;
}

bool Mpu6050::configure(const Config & config)
{
  // DEVICE_RESET clears itself; the device may not ACK while it resets.
  if (!write(reg::kPwrMgmt1, bits::kDeviceReset)) {
    return false;
  }
  sleepMs(100);
  if (!write(reg::kSignalPathReset, bits::kResetAllPaths)) {
    return false;
  }
  sleepMs(100);
  // Out of sleep, clocked from the X gyro PLL as the register map recommends.
  if (!write(reg::kPwrMgmt1, bits::kClkPllGyroX) || !verify(reg::kPwrMgmt1, bits::kClkPllGyroX)) {
    return false;
  }
  sleepMs(50);
  if (!applyConfig(config)) {
    return false;
  }
  if (!write(reg::kIntEnable, bits::kDataRdy) || !verify(reg::kIntEnable, bits::kDataRdy)) {
    return false;
  }
  uint8_t status = 0;
  return readBytes(reg::kIntStatus, &status, 1);  // clears a stale data-ready flag
}

bool Mpu6050::applyConfig(const Config & config)
{
  const uint8_t divider = prowl_mpu6050_driver::sampleRateDivider(config.dlpf, config.sample_rate_hz);
  const uint8_t writes[][2] = {
    {reg::kConfig, configValue(config.dlpf)},
    {reg::kSmplrtDiv, divider},
    {reg::kGyroConfig, gyroConfigValue(config.gyro_range)},
    {reg::kAccelConfig, accelConfigValue(config.accel_range)},
  };
  for (const auto & w : writes) {
    if (!write(w[0], w[1])) {
      return false;
    }
  }
  for (const auto & w : writes) {
    if (!verify(w[0], w[1])) {
      return false;
    }
  }
  config_ = config;
  divider_ = divider;
  return true;
}

ReadStatus Mpu6050::poll(Sample & out)
{
  uint8_t buf[1 + kDataBytes];
  if (!readBytes(reg::kIntStatus, buf, sizeof(buf))) {
    return ReadStatus::Error;
  }
  if (!(buf[0] & bits::kDataRdy)) {
    return ReadStatus::NoNewSample;
  }
  out = convert(parseSample(buf + 1));
  return ReadStatus::NewSample;
}

bool Mpu6050::read(Sample & out)
{
  uint8_t buf[kDataBytes];
  if (!readBytes(reg::kAccelXoutH, buf, sizeof(buf))) {
    return false;
  }
  out = convert(parseSample(buf));
  return true;
}

Sample Mpu6050::convert(const RawSample & raw) const
{
  Sample s;
  s.raw = raw;
  for (int i = 0; i < 3; ++i) {
    s.accel[i] = accelToMps2(raw.accel[i], config_.accel_range);
    s.gyro_uncalibrated[i] = gyroToRadps(raw.gyro[i], config_.gyro_range);
    s.gyro[i] = s.gyro_uncalibrated[i] - gyro_bias_[i];
  }
  s.temperature = temperatureC(raw.temperature);
  return s;
}

bool Mpu6050::write(uint8_t reg, uint8_t value)
{
  if (!i2c_.writeRegister(reg, value)) {
    return fail("write " + hex(reg) + ": " + i2c_.lastError());
  }
  return true;
}

bool Mpu6050::readBytes(uint8_t reg, uint8_t * data, size_t len)
{
  if (!i2c_.readRegisters(reg, data, len)) {
    return fail("read " + hex(reg) + ": " + i2c_.lastError());
  }
  return true;
}

bool Mpu6050::verify(uint8_t reg, uint8_t expected)
{
  uint8_t value = 0;
  if (!readBytes(reg, &value, 1)) {
    return false;
  }
  if (value != expected) {
    return fail("register " + hex(reg) + " reads " + hex(value) + ", wrote " + hex(expected));
  }
  return true;
}

bool Mpu6050::fail(const std::string & what)
{
  ++errors_;
  last_error_ = what;
  return false;
}

}  // namespace prowl_mpu6050_driver
