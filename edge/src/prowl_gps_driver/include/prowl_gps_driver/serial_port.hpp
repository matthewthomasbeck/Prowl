#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace prowl_gps_driver
{

// Read-only raw serial port, 8N1, no flow control (the u-blox 7's UART has none). The port is opened
// for exclusive use, so a second reader gets EBUSY.
class SerialPort
{
public:
  SerialPort() = default;
  ~SerialPort();

  SerialPort(const SerialPort &) = delete;
  SerialPort & operator=(const SerialPort &) = delete;

  // baud: 4800 to 230400. On failure returns false and sets error.
  bool open(const std::string & path, int baud, std::string & error);
  void close();
  bool isOpen() const {return fd_ >= 0;}

  // Waits up to timeout_ms for data. Returns the bytes read, 0 on timeout,
  // or -1 on error (errno set).
  ssize_t read(uint8_t * buf, size_t len, int timeout_ms);

private:
  int fd_ = -1;
};

// Whether open() accepts this baud rate.
bool supportedBaud(int baud);

}  // namespace prowl_gps_driver
