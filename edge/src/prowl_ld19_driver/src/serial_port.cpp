#include "prowl_ld19_driver/serial_port.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace prowl_ld19_driver
{

SerialPort::~SerialPort()
{
  close();
}

bool SerialPort::open(const std::string & path, std::string & error)
{
  close();
  const int fd = ::open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    error = path + ": " + std::strerror(errno);
    return false;
  }
  auto fail = [&](const char * what) {
      error = path + ": " + what + ": " + std::strerror(errno);
      ::close(fd);
      return false;
    };
  if (ioctl(fd, TIOCEXCL) < 0) {
    return fail("TIOCEXCL");
  }

  termios tio{};
  if (tcgetattr(fd, &tio) < 0) {
    return fail("tcgetattr");
  }
  cfmakeraw(&tio);
  tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB | CSTOPB | CRTSCTS);
  tio.c_cflag |= CS8 | CLOCAL | CREAD;
  tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  if (cfsetispeed(&tio, B230400) < 0 || cfsetospeed(&tio, B230400) < 0) {
    return fail("cfsetspeed");
  }
  // Succeeds if any setting took; the mini UART can't do some of the others.
  if (tcsetattr(fd, TCSANOW, &tio) < 0) {
    return fail("tcsetattr");
  }
  termios check{};
  if (tcgetattr(fd, &check) < 0) {
    return fail("tcgetattr");
  }
  if (cfgetispeed(&check) != B230400) {
    error = path + ": could not set 230400 baud";
    ::close(fd);
    return false;
  }
  tcflush(fd, TCIFLUSH);  // drop whatever piled up before we opened
  fd_ = fd;
  return true;
}

void SerialPort::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

ssize_t SerialPort::read(uint8_t * buf, size_t len, int timeout_ms)
{
  if (fd_ < 0) {
    errno = EBADF;
    return -1;
  }
  pollfd pfd{fd_, POLLIN, 0};
  const int ready = ::poll(&pfd, 1, timeout_ms);
  if (ready < 0) {
    return errno == EINTR ? 0 : -1;
  }
  if (ready == 0) {
    return 0;
  }
  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    errno = EIO;
    return -1;
  }
  const ssize_t n = ::read(fd_, buf, len);
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
    return 0;
  }
  return n;
}

}  // namespace prowl_ld19_driver
