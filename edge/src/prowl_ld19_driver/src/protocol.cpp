#include "prowl_ld19_driver/protocol.hpp"

#include <cmath>

namespace prowl_ld19_driver
{

namespace
{

constexpr uint8_t kCrcPoly = 0x4D;

constexpr std::array<uint8_t, 256> makeCrcTable()
{
  std::array<uint8_t, 256> table{};
  for (int byte = 0; byte < 256; ++byte) {
    uint8_t crc = static_cast<uint8_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ kCrcPoly) : static_cast<uint8_t>(crc << 1);
    }
    table[byte] = crc;
  }
  return table;
}

constexpr std::array<uint8_t, 256> kCrcTable = makeCrcTable();

uint16_t readU16(const uint8_t * p)
{
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void writeU16(uint8_t * p, uint16_t v)
{
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

}  // namespace

uint8_t crc8(const uint8_t * data, size_t len)
{
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc = kCrcTable[crc ^ data[i]];
  }
  return crc;
}

double Packet::pointAngle(size_t i) const
{
  const double start = start_angle / 100.0;
  double end = end_angle / 100.0;
  if (end < start) {
    end += 360.0;  // the packet crosses 0 deg
  }
  const double step = (end - start) / (kPointsPerPacket - 1);
  const double angle = std::fmod(start + step * static_cast<double>(i), 360.0);
  return angle < 0.0 ? angle + 360.0 : angle;
}

bool decodePacket(const uint8_t * bytes, Packet & out)
{
  if (bytes[0] != kHeader || bytes[1] != kVerLen) {
    return false;
  }
  if (crc8(bytes, kPacketSize - 1) != bytes[kPacketSize - 1]) {
    return false;
  }
  out.speed_dps = readU16(bytes + 2);
  out.start_angle = readU16(bytes + 4);
  const uint8_t * p = bytes + 6;
  for (Measurement & m : out.points) {
    m.distance_mm = readU16(p);
    m.intensity = p[2];
    p += 3;
  }
  out.end_angle = readU16(p);
  out.timestamp_ms = readU16(p + 2);
  return true;
}

std::array<uint8_t, kPacketSize> encodePacket(const Packet & packet)
{
  std::array<uint8_t, kPacketSize> bytes{};
  bytes[0] = kHeader;
  bytes[1] = kVerLen;
  writeU16(&bytes[2], packet.speed_dps);
  writeU16(&bytes[4], packet.start_angle);
  uint8_t * p = &bytes[6];
  for (const Measurement & m : packet.points) {
    writeU16(p, m.distance_mm);
    p[2] = m.intensity;
    p += 3;
  }
  writeU16(p, packet.end_angle);
  writeU16(p + 2, packet.timestamp_ms);
  bytes[kPacketSize - 1] = crc8(bytes.data(), kPacketSize - 1);
  return bytes;
}

}  // namespace prowl_ld19_driver
