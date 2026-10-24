#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace prowl_ld19_driver
{

// LD19 measurement packet (Development Manual V2.3, section 3), little-endian:
//   0x54 | 0x2C | speed u16 (deg/s) | start angle u16 (0.01 deg)
//   | 12 x {distance u16 (mm), intensity u8} | end angle u16 (0.01 deg)
//   | timestamp u16 (ms, wraps at 30000) | CRC-8 of the 46 bytes before it
constexpr uint8_t kHeader = 0x54;
constexpr uint8_t kVerLen = 0x2C;  // packet type 1 (upper 3 bits), 12 points (lower 5)
constexpr size_t kPointsPerPacket = 12;
constexpr size_t kPacketSize = 11 + 3 * kPointsPerPacket;
constexpr uint32_t kTimestampWrap = 30000;
constexpr uint32_t kBaudRate = 230400;

// CRC-8, polynomial 0x4D, initial value 0, MSB first, no reflection, no final XOR.
uint8_t crc8(const uint8_t * data, size_t len);

struct Measurement
{
  uint16_t distance_mm;  // 0 = no return
  uint8_t intensity;
};

struct Packet
{
  uint16_t speed_dps;
  uint16_t start_angle;  // 0.01 deg
  uint16_t end_angle;  // 0.01 deg
  uint16_t timestamp_ms;
  std::array<Measurement, kPointsPerPacket> points;

  // Angle of point i in degrees, [0, 360), increasing clockwise from the
  // sensor's front. Interpolated between the start and end angles.
  double pointAngle(size_t i) const;
};

// Decodes kPacketSize bytes. False if the header, type/length byte or CRC is wrong.
bool decodePacket(const uint8_t * bytes, Packet & out);

// Inverse of decodePacket, CRC included. Used by the tests.
std::array<uint8_t, kPacketSize> encodePacket(const Packet & packet);

}  // namespace prowl_ld19_driver
