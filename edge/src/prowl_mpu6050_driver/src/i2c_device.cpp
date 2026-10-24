#include "prowl_mpu6050_driver/i2c_device.hpp"

#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace prowl_mpu6050_driver
{

I2cDevice::~I2cDevice()
{
  close();
}

bool I2cDevice::open(int bus, uint8_t address)
{
  close();
  const std::string path = "/dev/i2c-" + std::to_string(bus);
  fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    return fail(("open " + path).c_str());
  }
  address_ = address;

  unsigned long funcs = 0;
  combined_ = ioctl(fd_, I2C_FUNCS, &funcs) == 0 && (funcs & I2C_FUNC_I2C);
  // Plain read()/write() need the slave address bound to the descriptor.
  if (ioctl(fd_, I2C_SLAVE, static_cast<unsigned long>(address)) < 0) {
    fail("I2C_SLAVE");
    ::close(fd_);
    fd_ = -1;
    return false;
  }
  last_error_.clear();
  return true;
}

void I2cDevice::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool I2cDevice::writeRegister(uint8_t reg, uint8_t value)
{
  if (fd_ < 0) {
    last_error_ = "device not open";
    return false;
  }
  const uint8_t buf[2] = {reg, value};
  const ssize_t n = ::write(fd_, buf, sizeof(buf));
  if (n != static_cast<ssize_t>(sizeof(buf))) {
    return fail("write");
  }
  return true;
}

bool I2cDevice::readRegisters(uint8_t reg, uint8_t * data, size_t len)
{
  if (fd_ < 0) {
    last_error_ = "device not open";
    return false;
  }
  if (combined_) {
    i2c_msg msgs[2] = {};
    msgs[0].addr = address_;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;
    msgs[1].addr = address_;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = static_cast<uint16_t>(len);
    msgs[1].buf = data;
    i2c_rdwr_ioctl_data xfer = {msgs, 2};
    if (ioctl(fd_, I2C_RDWR, &xfer) != 2) {
      return fail("I2C_RDWR");
    }
    return true;
  }
  // Fallback: a stop between the address write and the read.
  if (::write(fd_, &reg, 1) != 1) {
    return fail("write");
  }
  if (::read(fd_, data, len) != static_cast<ssize_t>(len)) {
    return fail("read");
  }
  return true;
}

bool I2cDevice::fail(const char * what)
{
  last_error_ = std::string(what) + ": " + std::strerror(errno);
  return false;
}

}  // namespace prowl_mpu6050_driver
