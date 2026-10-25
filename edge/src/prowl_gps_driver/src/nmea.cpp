#include "prowl_gps_driver/nmea.hpp"

#include <charconv>
#include <cmath>
#include <ctime>

namespace prowl_gps_driver
{

namespace
{

std::optional<int> hexDigit(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return std::nullopt;
}

bool allDigits(const std::string & s, size_t begin, size_t end)
{
  for (size_t i = begin; i < end; ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  return true;
}

int twoDigits(const std::string & s, size_t at)
{
  return (s[at] - '0') * 10 + (s[at + 1] - '0');
}

// ddmm.mmmmm (or dddmm.mmmmm): the minutes are always the two digits before the point.
std::optional<double> degreesMinutes(const std::string & value, double max_degrees)
{
  const auto number = parseNumber(value);
  if (!number || *number < 0.0) {
    return std::nullopt;
  }
  const double degrees = std::floor(*number / 100.0);
  const double minutes = *number - degrees * 100.0;
  if (minutes >= 60.0) {
    return std::nullopt;
  }
  const double result = degrees + minutes / 60.0;
  if (result > max_degrees) {
    return std::nullopt;
  }
  return result;
}

const std::string & field(const Sentence & s, size_t i)
{
  static const std::string empty;
  return i < s.fields.size() ? s.fields[i] : empty;
}

}  // namespace

uint8_t checksum(const char * begin, const char * end)
{
  uint8_t sum = 0;
  for (const char * p = begin; p < end; ++p) {
    sum ^= static_cast<uint8_t>(*p);
  }
  return sum;
}

void SentenceSplitter::feed(const uint8_t * data, size_t len, std::vector<std::string> & out)
{
  stats_.bytes += len;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t byte = data[i];
    if (byte == '$') {
      if (in_sentence_) {
        ++stats_.bad_sentences;  // cut off by the next one
      }
      line_.clear();
      in_sentence_ = true;
      continue;
    }
    if (byte == '\r' || byte == '\n') {
      if (in_sentence_) {
        finish(out);
      }
      in_sentence_ = false;
      continue;
    }
    if (!in_sentence_) {
      ++stats_.skipped_bytes;
      continue;
    }
    if (byte < 0x20 || byte > 0x7E || line_.size() >= kMaxSentence) {
      ++stats_.bad_sentences;
      ++stats_.skipped_bytes;
      in_sentence_ = false;
      continue;
    }
    line_.push_back(static_cast<char>(byte));
  }
}

void SentenceSplitter::finish(std::vector<std::string> & out)
{
  // The body, then '*' and two hex digits.
  const size_t n = line_.size();
  if (n < 3 || line_[n - 3] != '*') {
    ++stats_.bad_sentences;
    return;
  }
  const auto high = hexDigit(line_[n - 2]);
  const auto low = hexDigit(line_[n - 1]);
  if (!high || !low) {
    ++stats_.bad_sentences;
    return;
  }
  const char * body = line_.data();
  if (checksum(body, body + n - 3) != static_cast<uint8_t>(*high * 16 + *low)) {
    ++stats_.checksum_errors;
    return;
  }
  ++stats_.sentences;
  out.emplace_back(line_, 0, n - 3);
}

void SentenceSplitter::reset()
{
  line_.clear();
  in_sentence_ = false;
  stats_ = SplitterStats{};
}

bool splitSentence(const std::string & body, Sentence & out)
{
  out.fields.clear();
  size_t start = 0;
  std::string address;
  bool first = true;
  while (true) {
    const size_t comma = body.find(',', start);
    const size_t end = comma == std::string::npos ? body.size() : comma;
    if (first) {
      address = body.substr(start, end - start);
      first = false;
    } else {
      out.fields.emplace_back(body, start, end - start);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (address.size() != 5 || address[0] == 'P') {
    return false;
  }
  for (char c : address) {
    if (c < 'A' || c > 'Z') {
      return false;
    }
  }
  out.talker = address.substr(0, 2);
  out.type = address.substr(2);
  return true;
}

std::optional<double> parseNumber(const std::string & field)
{
  if (field.empty()) {
    return std::nullopt;
  }
  double value = 0.0;
  const char * end = field.data() + field.size();
  const auto result = std::from_chars(field.data(), end, value);
  if (result.ec != std::errc() || result.ptr != end || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

std::optional<int> parseInteger(const std::string & field)
{
  if (field.empty()) {
    return std::nullopt;
  }
  int value = 0;
  const char * end = field.data() + field.size();
  const auto result = std::from_chars(field.data(), end, value);
  if (result.ec != std::errc() || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<double> parseLatitude(const std::string & value, const std::string & hemisphere)
{
  const auto degrees = degreesMinutes(value, 90.0);
  if (!degrees) {
    return std::nullopt;
  }
  if (hemisphere == "N") {
    return *degrees;
  }
  if (hemisphere == "S") {
    return -*degrees;
  }
  return std::nullopt;
}

std::optional<double> parseLongitude(const std::string & value, const std::string & hemisphere)
{
  const auto degrees = degreesMinutes(value, 180.0);
  if (!degrees) {
    return std::nullopt;
  }
  if (hemisphere == "E") {
    return *degrees;
  }
  if (hemisphere == "W") {
    return -*degrees;
  }
  return std::nullopt;
}

std::optional<UtcTime> parseTime(const std::string & field)
{
  if (field.size() < 6 || !allDigits(field, 0, 6)) {
    return std::nullopt;
  }
  UtcTime t;
  t.hour = twoDigits(field, 0);
  t.minute = twoDigits(field, 2);
  const auto seconds = parseNumber(field.substr(4));
  if (!seconds || t.hour > 23 || t.minute > 59 || *seconds < 0.0 || *seconds >= 61.0) {
    return std::nullopt;  // 60.x is a leap second
  }
  t.second = *seconds;
  return t;
}

std::optional<UtcDate> parseDate(const std::string & field)
{
  if (field.size() != 6 || !allDigits(field, 0, 6)) {
    return std::nullopt;
  }
  UtcDate d;
  d.day = twoDigits(field, 0);
  d.month = twoDigits(field, 2);
  const int yy = twoDigits(field, 4);
  d.year = yy < 80 ? 2000 + yy : 1900 + yy;  // GPS dates start in 1980
  if (d.day < 1 || d.day > 31 || d.month < 1 || d.month > 12) {
    return std::nullopt;
  }
  return d;
}

double unixTime(const UtcDate & date, const UtcTime & time)
{
  std::tm tm{};
  tm.tm_year = date.year - 1900;
  tm.tm_mon = date.month - 1;
  tm.tm_mday = date.day;
  tm.tm_hour = time.hour;
  tm.tm_min = time.minute;
  tm.tm_sec = 0;
  return static_cast<double>(timegm(&tm)) + time.second;
}

// $xxGGA,time,lat,NS,long,EW,quality,numSV,HDOP,alt,M,sep,M,diffAge,diffStation
bool parseGga(const Sentence & s, Gga & out)
{
  if (s.type != "GGA" || s.fields.size() < 11) {
    return false;
  }
  out = Gga{};
  out.time = parseTime(field(s, 0));
  out.latitude = parseLatitude(field(s, 1), field(s, 2));
  out.longitude = parseLongitude(field(s, 3), field(s, 4));
  out.quality = parseInteger(field(s, 5)).value_or(0);
  out.satellites_used = parseInteger(field(s, 6)).value_or(0);
  out.hdop = parseNumber(field(s, 7));
  out.altitude_msl = parseNumber(field(s, 8));
  out.geoid_separation = parseNumber(field(s, 10));
  return true;
}

// $xxRMC,time,status,lat,NS,long,EW,spd,cog,date,mv,mvEW,posMode
bool parseRmc(const Sentence & s, Rmc & out)
{
  if (s.type != "RMC" || s.fields.size() < 9) {
    return false;
  }
  out = Rmc{};
  out.time = parseTime(field(s, 0));
  out.valid = field(s, 1) == "A";
  out.latitude = parseLatitude(field(s, 2), field(s, 3));
  out.longitude = parseLongitude(field(s, 4), field(s, 5));
  out.speed_knots = parseNumber(field(s, 6));
  out.course_deg = parseNumber(field(s, 7));
  out.date = parseDate(field(s, 8));
  return true;
}

// $xxGSA,opMode,navMode{,sv}x12,PDOP,HDOP,VDOP
bool parseGsa(const Sentence & s, Gsa & out)
{
  if (s.type != "GSA" || s.fields.size() < 17) {
    return false;
  }
  out = Gsa{};
  out.nav_mode = parseInteger(field(s, 1)).value_or(1);
  for (size_t i = 2; i < 14; ++i) {
    if (const auto id = parseInteger(field(s, i))) {
      out.used_ids.push_back(*id);
    }
  }
  out.pdop = parseNumber(field(s, 14));
  out.hdop = parseNumber(field(s, 15));
  out.vdop = parseNumber(field(s, 16));
  return true;
}

// $xxGSV,numMsg,msgNum,numSV{,sv,elv,az,cno}x1..4
bool parseGsv(const Sentence & s, Gsv & out)
{
  if (s.type != "GSV" || s.fields.size() < 3) {
    return false;
  }
  const auto total = parseInteger(field(s, 0));
  const auto number = parseInteger(field(s, 1));
  const auto in_view = parseInteger(field(s, 2));
  if (!total || !number || !in_view || *number < 1 || *number > *total) {
    return false;
  }
  out = Gsv{};
  out.total_messages = *total;
  out.message_number = *number;
  out.satellites_in_view = *in_view;
  for (size_t i = 3; i < s.fields.size(); i += 4) {
    const auto id = parseInteger(field(s, i));
    if (!id) {
      continue;  // the receiver reports satellites it can't number with an empty ID
    }
    SatelliteInView sat;
    sat.id = *id;
    sat.elevation = parseInteger(field(s, i + 1));
    sat.azimuth = parseInteger(field(s, i + 2));
    sat.cno = parseInteger(field(s, i + 3));
    out.satellites.push_back(sat);
  }
  return true;
}

}  // namespace prowl_gps_driver
