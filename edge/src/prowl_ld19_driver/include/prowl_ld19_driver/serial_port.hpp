#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace prowl_ld19_driver
{

// Read-only raw serial port at the LD19's 230400 8N1, no flow control.
// The port is opened for exclusive use, so a second reader gets EBUSY.
class SerialPort
{
public:
  SerialPort() = default;
  ~SerialPort();

  SerialPort(const SerialPort &) = delete;
  SerialPort & operator=(const SerialPort &) = delete;

  // On failure returns false and sets error.
  bool open(const std::string & path, std::string & error);
  void close();
  bool isOpen() const {return fd_ >= 0;}
  int fd() const {return fd_;}

  // Waits up to timeout_ms for data. Returns the bytes read, 0 on timeout,
  // or -1 on error (errno set).
  ssize_t read(uint8_t * buf, size_t len, int timeout_ms);

private:
  int fd_ = -1;
};

}  // namespace prowl_ld19_driver
