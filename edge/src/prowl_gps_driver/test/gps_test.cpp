// Plain test executable for the GPS driver's NMEA parsing and receiver state: exits non-zero if any check
// fails. The example sentences are from the u-blox 7 protocol specification (GPS.G7-SW-12001), and the
// "no fix" ones were captured from Prowl's receiver indoors.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "prowl_gps_driver/nmea.hpp"
#include "prowl_gps_driver/receiver_state.hpp"

using namespace prowl_gps_driver;

namespace
{

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

// Spec examples, section 24.
const char * kGga = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n";
const char * kRmc = "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57\r\n";
const char * kGsa = "$GPGSA,A,3,23,29,07,08,09,18,26,28,,,,,1.94,1.18,1.54*0D\r\n";
const char * kGsv[] = {
  "$GPGSV,3,1,10,23,38,230,44,29,71,156,47,07,29,116,41,08,09,081,36*7F\r\n",
  "$GPGSV,3,2,10,10,07,189,,05,05,220,,09,34,274,42,18,25,309,44*72\r\n",
  "$GPGSV,3,3,10,26,82,187,47,28,43,056,46*77\r\n",
};

// Captured from Prowl's receiver with no satellites.
const char * kNoFix =
  "$GPRMC,,V,,,,,,,,,,N*53\r\n"
  "$GPVTG,,,,,,,,,N*30\r\n"
  "$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n"
  "$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30\r\n"
  "$GPGLL,,,,,,V,N*64\r\n";

std::vector<std::string> split(const std::string & bytes, SentenceSplitter & splitter)
{
  std::vector<std::string> out;
  splitter.feed(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), out);
  return out;
}

Sentence sentence(const char * text)
{
  SentenceSplitter splitter;
  const auto bodies = split(text, splitter);
  Sentence s;
  if (bodies.size() != 1 || !splitSentence(bodies[0], s)) {
    std::printf("  could not split %s", text);
  }
  return s;
}

void testChecksum()
{
  std::printf("checksum\n");
  const std::string body = "GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,";
  CHECK(checksum(body.data(), body.data() + body.size()) == 0x5B);
  const std::string empty_rmc = "GPRMC,,V,,,,,,,,,,N";
  CHECK(checksum(empty_rmc.data(), empty_rmc.data() + empty_rmc.size()) == 0x53);
  CHECK(checksum(nullptr, nullptr) == 0);
}

void testSplitter()
{
  std::printf("splitter\n");
  SentenceSplitter splitter;
  auto out = split(kNoFix, splitter);
  CHECK(out.size() == 5);
  CHECK(out[0] == "GPRMC,,V,,,,,,,,,,N");
  CHECK(splitter.stats().sentences == 5);
  CHECK(splitter.stats().checksum_errors == 0);

  // Starting mid-sentence: the tail is skipped and the next sentence is found.
  splitter.reset();
  out = split(std::string("117,E,1,08*5B\r\n") + kGga, splitter);
  CHECK(out.size() == 1);
  CHECK(splitter.stats().skipped_bytes > 0);

  // A sentence fed one byte at a time.
  splitter.reset();
  std::vector<std::string> bodies;
  for (const char * p = kRmc; *p; ++p) {
    splitter.feed(reinterpret_cast<const uint8_t *>(p), 1, bodies);
  }
  CHECK(bodies.size() == 1);

  // A corrupted byte fails the checksum; the next sentence is still read.
  splitter.reset();
  std::string corrupted = kGga;
  corrupted[20] = '9';
  out = split(corrupted + kRmc, splitter);
  CHECK(out.size() == 1);
  CHECK(splitter.stats().checksum_errors == 1);

  // A sentence cut off by the next '$', binary bytes (a UBX message), no checksum, too long.
  splitter.reset();
  const std::string ubx = "\xB5\x62\x01\x07\x00\x00";
  out = split(std::string("$GPGGA,0927") + kRmc + ubx + "$GPTXT,01*\r\n$GPGGA," + std::string(200, '1') + "\r\n" + kGsa,
    splitter);
  CHECK(out.size() == 2);
  CHECK(splitter.stats().bad_sentences == 3);

  // Bad hex in the checksum.
  splitter.reset();
  out = split("$GPGLL,,,,,,V,N*6G\r\n", splitter);
  CHECK(out.empty());
}

void testSplitSentence()
{
  std::printf("split sentence\n");
  Sentence s;
  CHECK(splitSentence("GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99", s));
  CHECK(s.talker == "GP");
  CHECK(s.type == "GSA");
  CHECK(s.fields.size() == 17);
  CHECK(s.fields[0] == "A");
  CHECK(s.fields[2].empty());

  CHECK(splitSentence("GNRMC,", s));  // a trailing empty field is kept
  CHECK(s.fields.size() == 1);
  CHECK(!splitSentence("PUBX,00,081350.00", s));
  CHECK(!splitSentence("GPGG,1", s));
  CHECK(!splitSentence("gpgga,1", s));
}

void testFields()
{
  std::printf("fields\n");
  // Spec section 20: 4717.112671 N, 00833.914843 E.
  CHECK(near(*parseLatitude("4717.112671", "N"), 47.28521118, 1e-8));
  CHECK(near(*parseLongitude("00833.914843", "E"), 8.56524738, 1e-8));
  CHECK(near(*parseLatitude("4717.112671", "S"), -47.28521118, 1e-8));
  CHECK(near(*parseLongitude("12230.0", "W"), -122.5));
  CHECK(!parseLatitude("", "N"));
  CHECK(!parseLatitude("4717.1", ""));
  CHECK(!parseLatitude("4760.0", "N"));  // 60 minutes
  CHECK(!parseLatitude("9100.0", "N"));
  CHECK(!parseLongitude("18100.0", "E"));

  const auto t = parseTime("092725.00");
  CHECK(t && t->hour == 9 && t->minute == 27 && near(t->second, 25.0));
  CHECK(!parseTime(""));
  CHECK(!parseTime("2460"));
  CHECK(!parseTime("246000.00"));

  const auto d = parseDate("091202");
  CHECK(d && d->day == 9 && d->month == 12 && d->year == 2002);
  CHECK(parseDate("010185")->year == 1985);
  CHECK(!parseDate("320126"));
  CHECK(!parseDate("0912"));

  // 2002-12-09 08:35:59.5 UTC
  CHECK(near(unixTime(*d, UtcTime{8, 35, 59.5}), 1039422959.5));

  CHECK(!parseNumber(""));
  CHECK(!parseNumber("1.2x"));
  CHECK(near(*parseNumber("-3.25"), -3.25));
  CHECK(*parseInteger("08") == 8);
  CHECK(!parseInteger("8.0"));
}

void testMessages()
{
  std::printf("messages\n");
  Gga gga;
  CHECK(parseGga(sentence(kGga), gga));
  CHECK(gga.time && gga.time->hour == 9);
  CHECK(near(*gga.latitude, 47.0 + 17.11399 / 60.0));
  CHECK(near(*gga.longitude, 8.0 + 33.91590 / 60.0));
  CHECK(gga.quality == 1);
  CHECK(gga.satellites_used == 8);
  CHECK(near(*gga.hdop, 1.01));
  CHECK(near(*gga.altitude_msl, 499.6));
  CHECK(near(*gga.geoid_separation, 48.0));

  Rmc rmc;
  CHECK(parseRmc(sentence(kRmc), rmc));
  CHECK(rmc.valid);
  CHECK(near(*rmc.speed_knots, 0.004));
  CHECK(near(*rmc.course_deg, 77.52));
  CHECK(rmc.date && rmc.date->year == 2002);
  CHECK(!parseGga(sentence(kRmc), gga));

  Gsa gsa;
  CHECK(parseGsa(sentence(kGsa), gsa));
  CHECK(gsa.nav_mode == 3);
  CHECK((gsa.used_ids == std::vector<int>{23, 29, 7, 8, 9, 18, 26, 28}));
  CHECK(near(*gsa.pdop, 1.94) && near(*gsa.hdop, 1.18) && near(*gsa.vdop, 1.54));

  Gsv gsv;
  CHECK(parseGsv(sentence(kGsv[1]), gsv));
  CHECK(gsv.total_messages == 3 && gsv.message_number == 2 && gsv.satellites_in_view == 10);
  CHECK(gsv.satellites.size() == 4);
  CHECK(gsv.satellites[0].id == 10 && *gsv.satellites[0].elevation == 7 && *gsv.satellites[0].azimuth == 189);
  CHECK(!gsv.satellites[0].cno);  // not tracked
  CHECK(*gsv.satellites[3].cno == 44);

  // The receiver with no fix: everything empty.
  SentenceSplitter splitter;
  const auto bodies = split(kNoFix, splitter);
  Sentence s;
  CHECK(splitSentence(bodies[0], s) && parseRmc(s, rmc));
  CHECK(!rmc.valid && !rmc.time && !rmc.latitude && !rmc.date);
  CHECK(splitSentence(bodies[2], s) && parseGga(s, gga));
  CHECK(gga.quality == 0 && gga.satellites_used == 0 && !gga.latitude && near(*gga.hdop, 99.99));
  CHECK(splitSentence(bodies[3], s) && parseGsa(s, gsa));
  CHECK(gsa.nav_mode == 1 && gsa.used_ids.empty());
}

Update feed(ReceiverState & state, const char * text, double now)
{
  return state.apply(sentence(text), now);
}

void testReceiverState()
{
  std::printf("receiver state\n");
  ReceiverState state;

  // Nothing yet.
  GpsFix f = state.fix(0.0);
  CHECK(f.fix == FixType::None && !f.latitude && f.satellites.empty() && !f.utc);

  // No fix, from the captured burst.
  SentenceSplitter splitter;
  Sentence s;
  for (const std::string & body : split(kNoFix, splitter)) {
    CHECK(splitSentence(body, s));
    const Update u = state.apply(s, 0.0);
    CHECK(u == (s.type == "GGA" ? Update::Fix : Update::None));
  }
  f = state.fix(0.0);
  CHECK(f.fix == FixType::None && !f.latitude && !f.speed_mps && f.satellites_used == 0);
  CHECK(near(*f.hdop, 99.99));

  // A 3D fix: RMC, GGA, GSA, then the GSV set, as the receiver orders them.
  CHECK(feed(state, kRmc, 1.0) == Update::Velocity);
  CHECK(feed(state, kGga, 1.0) == Update::Fix);
  CHECK(feed(state, kGsa, 1.0) == Update::None);
  for (const char * gsv : kGsv) {
    CHECK(feed(state, gsv, 1.0) == Update::None);
  }
  f = state.fix(1.0);
  CHECK(f.fix == FixType::Fix3D);
  CHECK(f.quality == 1);
  CHECK(near(*f.latitude, 47.0 + 17.11399 / 60.0));
  CHECK(near(*f.altitude_msl, 499.6));
  CHECK(near(*f.speed_mps, 0.004 * 1852.0 / 3600.0));
  CHECK(near(*f.course_deg, 77.52));
  CHECK(near(*f.hdop, 1.01));  // GGA's
  CHECK(near(*f.vdop, 1.54) && near(*f.pdop, 1.94));
  // The GGA's time on the RMC's date: 2002-12-09 09:27:25 UTC.
  CHECK(f.utc && near(*f.utc, 1039426045.0));
  CHECK(f.satellites.size() == 10);
  CHECK(f.satellites.front().id == 5 && f.satellites.back().id == 29);  // sorted
  int used = 0;
  for (const Satellite & sat : f.satellites) {
    used += sat.used ? 1 : 0;
    if (sat.id == 5) {
      CHECK(!sat.used && !sat.cno);
    }
  }
  CHECK(used == 8);

  // A 2D fix, from GSA's navigation mode.
  feed(state, "$GPGSA,A,2,23,29,07,,,,,,,,,,2.50,1.80,1.73*05\r\n", 2.0);
  CHECK(state.fix(2.0).fix == FixType::Fix2D);

  // Without GSA for a while, the satellites, the used flags and the 2D/3D call age out.
  f = state.fix(2.0 + kSatelliteTimeout + 0.5);
  CHECK(f.satellites.empty());
  CHECK(f.fix == FixType::Fix3D);
  CHECK(!f.vdop);

  // An out-of-order GSV message drops its set; the last complete one stays.
  feed(state, kGsv[0], 3.0);
  feed(state, kGsv[2], 3.0);
  CHECK(state.fix(3.0).satellites.size() == 10);
  CHECK(state.fix(1.0 + kSatelliteTimeout + 0.5).satellites.empty());

  // A single-message set, and one with no satellites.
  feed(state, "$GPGSV,1,1,01,12,45,090,30*40\r\n", 10.0);
  CHECK(state.fix(10.0).satellites.size() == 1);

  // Losing the fix: position and velocity go, the receiver's time stays.
  feed(state, "$GPRMC,092726.00,V,,,,,,,091202,,,N*7D\r\n", 11.0);
  CHECK(feed(state, "$GPGGA,092726.00,,,,,0,00,99.99,,,,,,*6E\r\n", 11.0) == Update::Fix);
  f = state.fix(11.0);
  CHECK(f.fix == FixType::None && !f.latitude && !f.speed_mps && !f.course_deg);
  CHECK(f.utc && near(*f.utc, 1039426046.0));

  state.reset();
  CHECK(state.fix(11.0).fix == FixType::None && !state.fix(11.0).utc);
}

}  // namespace

int main()
{
  testChecksum();
  testSplitter();
  testSplitSentence();
  testFields();
  testMessages();
  testReceiverState();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
