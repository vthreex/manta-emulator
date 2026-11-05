#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace manta {
inline constexpr size_t teletextFileLimit = 16 * 1024 * 1024;
// VBIT2 t42: two MRAG bytes + 40 coded payload bytes. Its raw format
// prepends clock run-in 55 55 and framing code 27 to each packet.
// Keep parity, Hamming codes, enhancement packets and packet order intact.
inline std::vector<uint8_t> teletextFilePackets(const std::vector<uint8_t>& bytes) {
  if (bytes.empty() || bytes.size() > teletextFileLimit)
    throw std::runtime_error("Teletext file must contain 1 byte to 16 MiB of packet data");
  const bool framed = bytes.size() >= 3 && bytes[0] == 0x55 && bytes[1] == 0x55 && bytes[2] == 0x27;
  const size_t stride = framed ? 45 : 42;
  if (bytes.size() % stride)
    throw std::runtime_error("Unsupported teletext file: expected complete 42-byte packets or 45-byte packets with 55 55 27 framing");
  std::vector<uint8_t> packets;
  packets.reserve(bytes.size() / stride * 42);
  for (size_t at = 0; at < bytes.size(); at += stride) {
    if (framed && (bytes[at] != 0x55 || bytes[at+1] != 0x55 || bytes[at+2] != 0x27))
      throw std::runtime_error("Teletext file has inconsistent 45-byte framing");
    const size_t start = at + (framed ? 3 : 0);
    packets.insert(packets.end(), bytes.begin()+start, bytes.begin()+start+42);
  }
  return packets;
}
}
