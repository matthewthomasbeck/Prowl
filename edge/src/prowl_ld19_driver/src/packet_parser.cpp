#include "prowl_ld19_driver/packet_parser.hpp"

namespace prowl_ld19_driver
{

void PacketParser::feed(const uint8_t * data, size_t len, std::vector<Packet> & out)
{
  stats_.bytes += len;
  buf_.insert(buf_.end(), data, data + len);

  size_t i = 0;
  while (buf_.size() - i >= 2) {
    if (buf_[i] != kHeader || buf_[i + 1] != kVerLen) {
      ++i;
      ++stats_.skipped_bytes;
      continue;
    }
    if (buf_.size() - i < kPacketSize) {
      break;  // wait for the rest of this packet
    }
    Packet packet;
    if (decodePacket(&buf_[i], packet)) {
      out.push_back(packet);
      ++stats_.packets;
      i += kPacketSize;
    } else {
      // Drop only the header byte: the real next packet may start inside these 47 bytes.
      ++stats_.crc_errors;
      ++stats_.skipped_bytes;
      ++i;
    }
  }
  // A lone trailing byte is kept only if it could be the start of a header.
  if (buf_.size() - i == 1 && buf_[i] != kHeader) {
    ++i;
    ++stats_.skipped_bytes;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
}

void PacketParser::reset()
{
  buf_.clear();
  stats_ = ParserStats{};
}

}  // namespace prowl_ld19_driver
