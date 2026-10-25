#pragma once

// NMEA 0183 (version 2.3) as the u-blox 7 sends it: the u-blox 7 Receiver Description and Protocol
// Specification (GPS.G7-SW-12001), sections 18-24.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace prowl_gps_driver
{

// NMEA 0183 caps a sentence at 82 characters, from '$' to the line end. A little slack for receivers
// that go over; anything longer is noise.
constexpr size_t kMaxSentence = 100;

struct SplitterStats
{
  uint64_t bytes = 0;  // everything fed in
  uint64_t sentences = 0;  // complete sentences whose checksum matched
  uint64_t checksum_errors = 0;  // complete sentences whose checksum didn't match
  uint64_t bad_sentences = 0;  // too long, no checksum, or bytes that aren't text
  uint64_t skipped_bytes = 0;  // bytes outside any sentence (e.g. UBX binary messages)
};

// The checksum of everything between '$' and '*': all those bytes XORed together.
uint8_t checksum(const char * begin, const char * end);

// Splits the UART byte stream into sentences. It can start anywhere in the stream: bytes before the
// next '$' are skipped, and a sentence that's cut off, too long or fails its checksum is dropped.
class SentenceSplitter
{
public:
  // Appends the body of each valid sentence found to out: what's between '$' and '*', e.g.
  // "GPGGA,092725.00,4717.11399,N,...".
  void feed(const uint8_t * data, size_t len, std::vector<std::string> & out);

  const SplitterStats & stats() const {return stats_;}
  void reset();

private:
  void finish(std::vector<std::string> & out);

  std::string line_;  // since the last '$'
  bool in_sentence_ = false;
  SplitterStats stats_;
};

// One sentence split into its parts. "GPGGA,1,2" is talker "GP", type "GGA", fields {"1", "2"}.
struct Sentence
{
  std::string talker;  // GP = GPS (and SBAS, QZSS), GL = GLONASS, GN = several systems, ...
  std::string type;
  std::vector<std::string> fields;  // fields[0] is the first one after the address
};

// False for anything without a standard 5-letter address, such as u-blox's own $PUBX.
bool splitSentence(const std::string & body, Sentence & out);

struct UtcTime
{
  int hour = 0;
  int minute = 0;
  double second = 0.0;
};

struct UtcDate
{
  int year = 0;  // four digits
  int month = 0;
  int day = 0;
};

// Each returns nothing for an empty field (the receiver doesn't know yet) or one that doesn't parse.
std::optional<double> parseNumber(const std::string & field);
std::optional<int> parseInteger(const std::string & field);
// ddmm.mmmmm with N or S, and dddmm.mmmmm with E or W, to degrees: north and east positive.
std::optional<double> parseLatitude(const std::string & value, const std::string & hemisphere);
std::optional<double> parseLongitude(const std::string & value, const std::string & hemisphere);
std::optional<UtcTime> parseTime(const std::string & field);  // hhmmss.ss
std::optional<UtcDate> parseDate(const std::string & field);  // ddmmyy

// Seconds since 1970-01-01 00:00 UTC.
double unixTime(const UtcDate & date, const UtcTime & time);

// GGA: the position fix.
struct Gga
{
  std::optional<UtcTime> time;
  std::optional<double> latitude;  // degrees
  std::optional<double> longitude;  // degrees
  int quality = 0;  // 0 no fix, 1 GPS fix, 2 differential (SBAS) fix, 6 estimated
  int satellites_used = 0;
  std::optional<double> hdop;
  std::optional<double> altitude_msl;  // m above mean sea level
  std::optional<double> geoid_separation;  // m, the geoid (mean sea level) above the WGS84 ellipsoid
};

// RMC: the recommended minimum: position, speed, course and the date.
struct Rmc
{
  std::optional<UtcTime> time;
  bool valid = false;  // status A; V means the receiver has no usable fix
  std::optional<double> latitude;
  std::optional<double> longitude;
  std::optional<double> speed_knots;
  std::optional<double> course_deg;  // over ground, clockwise from true north
  std::optional<UtcDate> date;
};

// GSA: the fix type, the satellites used, and the dilutions of precision.
struct Gsa
{
  int nav_mode = 1;  // 1 no fix, 2 2D fix, 3 3D fix
  std::vector<int> used_ids;  // at most 12
  std::optional<double> pdop;
  std::optional<double> hdop;
  std::optional<double> vdop;
};

struct SatelliteInView
{
  int id = 0;
  std::optional<int> elevation;  // degrees, 0-90
  std::optional<int> azimuth;  // degrees, 0-359, clockwise from true north
  std::optional<int> cno;  // signal strength, dBHz; empty when it isn't being tracked
};

// GSV: one message of a set listing the satellites in view, four per message.
struct Gsv
{
  int total_messages = 0;
  int message_number = 0;
  int satellites_in_view = 0;
  std::vector<SatelliteInView> satellites;  // satellites with an unknown ID are left out
};

// Each returns false if the sentence isn't that type or is too short.
bool parseGga(const Sentence & s, Gga & out);
bool parseRmc(const Sentence & s, Rmc & out);
bool parseGsa(const Sentence & s, Gsa & out);
bool parseGsv(const Sentence & s, Gsv & out);

}  // namespace prowl_gps_driver
