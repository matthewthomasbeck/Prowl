#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "prowl_ld19_driver/protocol.hpp"

namespace prowl_ld19_driver
{

struct ParserStats
{
  uint64_t bytes = 0;  // everything fed in
  uint64_t packets = 0;  // valid packets decoded
  uint64_t crc_errors = 0;  // header found but the CRC didn't match
  uint64_t skipped_bytes = 0;  // bytes thrown away while looking for a header
};

// Splits the raw UART byte stream into packets. It can start anywhere in the
// stream: bytes are skipped until a header, and a header whose packet fails
// the CRC is skipped too, so it resyncs on the next real packet.
class PacketParser
{
public:
  // Appends every complete, valid packet found to out.
  void feed(const uint8_t * data, size_t len, std::vector<Packet> & out);

  const ParserStats & stats() const {return stats_;}
  void reset();

private:
  std::vector<uint8_t> buf_;
  ParserStats stats_;
};

}  // namespace prowl_ld19_driver
