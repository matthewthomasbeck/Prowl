// Plain test executable for the LD19 driver: exits non-zero if any check fails.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "prowl_ld19_driver/packet_parser.hpp"
#include "prowl_ld19_driver/protocol.hpp"
#include "prowl_ld19_driver/scan_analysis.hpp"
#include "prowl_ld19_driver/scan_assembler.hpp"

using namespace prowl_ld19_driver;

namespace
{

constexpr double kPi = 3.14159265358979323846;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char * what, int line)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL line %d: %s\n", line, what);
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

bool near(double a, double b, double tol = 1e-6)
{
  return std::abs(a - b) <= tol;
}

// Development Manual V2.3, section 3.3.
const std::vector<uint8_t> kExample = {
  0x54, 0x2C, 0x68, 0x08, 0xAB, 0x7E, 0xE0, 0x00, 0xE4, 0xDC, 0x00, 0xE2, 0xD9, 0x00, 0xE5, 0xD5,
  0x00, 0xE3, 0xD3, 0x00, 0xE4, 0xD0, 0x00, 0xE9, 0xCD, 0x00, 0xE4, 0xCA, 0x00, 0xE2, 0xC7, 0x00,
  0xE9, 0xC5, 0x00, 0xE5, 0xC2, 0x00, 0xE5, 0xC0, 0x00, 0xE5, 0xBE, 0x82, 0x3A, 0x1A, 0x50};

Packet makePacket(uint16_t start, uint16_t end, uint16_t stamp, uint16_t distance)
{
  Packet p{};
  p.speed_dps = 3600;
  p.start_angle = start;
  p.end_angle = end;
  p.timestamp_ms = stamp;
  for (Measurement & m : p.points) {
    m = {distance, 200};
  }
  return p;
}

void testCrc()
{
  std::printf("crc\n");
  CHECK(kExample.size() == kPacketSize);
  CHECK(crc8(kExample.data(), kPacketSize - 1) == 0x50);
  // Single bytes: the table entry for that byte. Spot checks against the manual's table.
  const uint8_t one = 0x01, two = 0x02, last = 0xFF, mid = 0x80;
  CHECK(crc8(&one, 1) == 0x4D);
  CHECK(crc8(&two, 1) == 0x9A);
  CHECK(crc8(&mid, 1) == 0x7C);
  CHECK(crc8(&last, 1) == 0xA8);
  CHECK(crc8(nullptr, 0) == 0x00);
}

void testExamplePacket()
{
  std::printf("example packet\n");
  Packet p{};
  CHECK(decodePacket(kExample.data(), p));
  CHECK(p.speed_dps == 2152);
  CHECK(p.start_angle == 32427);
  CHECK(p.end_angle == 33470);
  CHECK(p.timestamp_ms == 6714);
  CHECK(p.points[0].distance_mm == 224);
  CHECK(p.points[0].intensity == 228);
  CHECK(p.points[1].distance_mm == 220);
  CHECK(p.points[11].distance_mm == 192);
  CHECK(p.points[11].intensity == 229);
  CHECK(near(p.pointAngle(0), 324.27));
  CHECK(near(p.pointAngle(11), 334.70));
  CHECK(near(p.pointAngle(1), 324.27 + (334.70 - 324.27) / 11.0));
  const auto bytes = encodePacket(p);
  CHECK(std::equal(bytes.begin(), bytes.end(), kExample.begin()));
}

void testZeroCrossing()
{
  std::printf("0 deg crossing\n");
  // 355.00 -> 10.00 deg: step (370 - 355) / 11.
  const Packet p = makePacket(35500, 1000, 100, 1500);
  const auto bytes = encodePacket(p);
  Packet q{};
  CHECK(decodePacket(bytes.data(), q));
  const double step = 15.0 / 11.0;
  CHECK(near(q.pointAngle(0), 355.0));
  CHECK(near(q.pointAngle(3), 355.0 + 3 * step));
  CHECK(near(q.pointAngle(4), 355.0 + 4 * step - 360.0));
  CHECK(near(q.pointAngle(11), 10.0));
  for (size_t i = 0; i < kPointsPerPacket; ++i) {
    CHECK(q.pointAngle(i) >= 0.0 && q.pointAngle(i) < 360.0);
  }
}

void testCorruptByte()
{
  std::printf("corrupt byte\n");
  for (size_t at = 2; at < kPacketSize; ++at) {
    std::vector<uint8_t> bad = kExample;
    bad[at] ^= 0x10;
    Packet p{};
    if (decodePacket(bad.data(), p)) {
      check(false, "corrupted packet decoded", __LINE__);
      std::printf("    (byte %zu)\n", at);
    }
  }
  std::vector<uint8_t> bad = kExample;
  bad[20] ^= 0x01;
  PacketParser parser;
  std::vector<Packet> out;
  parser.feed(bad.data(), bad.size(), out);
  CHECK(out.empty());
  CHECK(parser.stats().crc_errors == 1);
  CHECK(parser.stats().packets == 0);
}

void testResync()
{
  std::printf("resync\n");
  // Junk, including a fake header, then the tail of a packet, then two good packets.
  std::vector<uint8_t> stream = {0x00, 0xFF, 0x54, 0x13, 0x54, 0x2C, 0x01, 0x02, 0x03};
  stream.insert(stream.end(), kExample.begin() + 20, kExample.end());
  stream.insert(stream.end(), kExample.begin(), kExample.end());
  const auto second = encodePacket(makePacket(33500, 34600, 6740, 800));
  stream.insert(stream.end(), second.begin(), second.end());
  stream.push_back(0x54);  // start of the next packet

  // Fed one byte at a time, and all at once: same result.
  for (size_t chunk : {size_t{1}, size_t{5}, stream.size()}) {
    PacketParser parser;
    std::vector<Packet> out;
    for (size_t i = 0; i < stream.size(); i += chunk) {
      parser.feed(&stream[i], std::min(chunk, stream.size() - i), out);
    }
    CHECK(out.size() == 2);
    CHECK(out.size() == 2 && out[0].timestamp_ms == 6714 && out[1].points[0].distance_mm == 800);
    CHECK(parser.stats().packets == 2);
    CHECK(parser.stats().bytes == stream.size());
    // The fake header is a CRC error; every byte before the first packet is skipped.
    CHECK(parser.stats().crc_errors == 1);
    CHECK(parser.stats().skipped_bytes == stream.size() - 2 * kPacketSize - 1);
  }

  // A packet missing one byte (UART overrun): it fails, the next one is found.
  std::vector<uint8_t> dropped = kExample;
  dropped.erase(dropped.begin() + 30);
  dropped.insert(dropped.end(), kExample.begin(), kExample.end());
  PacketParser parser;
  std::vector<Packet> out;
  parser.feed(dropped.data(), dropped.size(), out);
  CHECK(out.size() == 1);
  CHECK(parser.stats().crc_errors == 1);
}

void testScanAssembly()
{
  std::printf("scan assembly\n");
  // Start mid-rotation at 200 deg, then 2.5 rotations of 30 packets each (12 deg per packet).
  ScanAssembler assembler;
  std::vector<Scan> scans;
  uint16_t stamp = 29950;  // the device clock wraps at 30000 during the run
  for (int k = 0; k < 75; ++k) {
    const double start = std::fmod(200.0 + 12.0 * k, 360.0);
    const double end = std::fmod(start + 11.0, 360.0);
    const Packet p = makePacket(static_cast<uint16_t>(std::lround(start * 100)),
        static_cast<uint16_t>(std::lround(end * 100)), stamp, static_cast<uint16_t>(1000 + k));
    if (assembler.add(p)) {
      scans.push_back(assembler.scan());
    }
    stamp = static_cast<uint16_t>((stamp + 3) % kTimestampWrap);
  }
  // Wraps at k = 13, 43, 73: the part before the first is dropped.
  CHECK(scans.size() == 2);
  for (const Scan & s : scans) {
    CHECK(s.points.size() == 360);
    CHECK(s.packets == 31);  // the packets holding the wraps count toward both rotations
    CHECK(near(s.speed_dps, 3600.0));
    CHECK(s.end_time_ms - s.start_time_ms == 30 * 3);
    bool increasing = true;
    for (size_t i = 1; i < s.points.size(); ++i) {
      increasing = increasing && s.points[i].angle_deg > s.points[i - 1].angle_deg;
    }
    CHECK(increasing);
    CHECK(s.points.front().angle_deg < 12.0 && s.points.back().angle_deg > 348.0);
  }
  CHECK(scans.size() == 2 && scans[1].start_time_ms - scans[0].start_time_ms == 30 * 3);
}

void testAnalysis()
{
  std::printf("analysis\n");
  // A wall 2 m away all around, one 0.5 m object spanning 0 deg, a 1 m object at 90 deg,
  // and a lone stray point that must not count as an object.
  Scan scan;
  for (int i = 0; i < 360; ++i) {
    uint16_t d = 2000;
    if (i <= 5 || i >= 355) {
      d = 500;
    } else if (i >= 85 && i <= 95) {
      d = 1000;
    } else if (i == 200) {
      d = 300;
    } else if (i == 250) {
      d = 0;
    }
    scan.points.push_back({i + 0.5, d, 100});
  }
  AnalysisConfig config;
  Mount mount;

  const Nearest n = nearestPoint(scan, mount, config);
  CHECK(n.found && near(n.distance_m, 0.3) && near(n.bearing_deg, 200.5));

  const auto sectors = sectorNearest(scan, mount, config);
  CHECK(near(sectors[0].distance_m, 0.5));  // F
  CHECK(near(sectors[2].distance_m, 1.0));  // R
  CHECK(near(sectors[4].distance_m, 0.3));  // B
  CHECK(near(sectors[6].distance_m, 2.0));  // L

  const auto objects = findObjects(scan, mount, config);
  // The stray point splits the wall in two: 0.5 m, 1 m, then three 2 m pieces.
  CHECK(objects.size() == 5);
  if (objects.size() == 5) {
    CHECK(near(objects[0].distance_m, 0.5) && objects[0].points == 11);
    CHECK(near(objects[0].width_m, 2 * 0.5 * std::sin(5.0 * kPi / 180.0), 1e-6));  // ends 10 deg apart
    CHECK(near(objects[1].distance_m, 1.0) && objects[1].points == 11);
    CHECK(near(objects[1].bearing_deg, 85.5));
    CHECK(near(objects[2].distance_m, 2.0) && near(objects[4].distance_m, 2.0));
  }

  // Sensor turned to face the robot's right, and mounted upside down.
  mount.yaw_deg = 90.0;
  CHECK(near(mount.bearing(10.0), 100.0));
  CHECK(sectorOf(mount.bearing(0.0)) == 2);
  mount.mirrored = true;
  CHECK(near(mount.bearing(10.0), 80.0));
  CHECK(near(mount.bearing(100.0), 350.0));

  config.min_range_mm = 400;
  CHECK(near(nearestPoint(scan, Mount{}, config).distance_m, 0.5));
}

}  // namespace

int main()
{
  testCrc();
  testExamplePacket();
  testZeroCrossing();
  testCorruptByte();
  testResync();
  testScanAssembly();
  testAnalysis();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
