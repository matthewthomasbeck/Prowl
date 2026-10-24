#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace prowl_mpu6050_driver
{

// One slave on a Linux I2C bus, through /dev/i2c-N (i2c-dev).
class I2cDevice
{
public:
  I2cDevice() = default;
  ~I2cDevice();

  I2cDevice(const I2cDevice &) = delete;
  I2cDevice & operator=(const I2cDevice &) = delete;

  // On failure returns false and leaves the reason in lastError().
  bool open(int bus, uint8_t address);
  void close();
  bool isOpen() const {return fd_ >= 0;}

  bool writeRegister(uint8_t reg, uint8_t value);
  // Register address write, then a repeated-start read of len bytes. The
  // device auto-increments, so this reads consecutive registers.
  bool readRegisters(uint8_t reg, uint8_t * data, size_t len);
  bool readRegister(uint8_t reg, uint8_t & value) {return readRegisters(reg, &value, 1);}

  const std::string & lastError() const {return last_error_;}

private:
  bool fail(const char * what);

  int fd_ = -1;
  uint8_t address_ = 0;
  bool combined_ = false;  // adapter supports I2C_RDWR with a repeated start
  std::string last_error_;
};

}  // namespace prowl_mpu6050_driver
