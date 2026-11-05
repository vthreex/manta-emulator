#pragma once
#include <cstdint>
#include <cstring>
#include <array>

namespace manta {
// EN 300 706 table 18, bytes 6..12: designation 0, page 00, subcode 0000,
// magazine bit M1 in byte 10 bit 4. The MRAG/prefix is not included here.
inline constexpr std::array<uint8_t,7> teletextInitialPage100{0x15,0x15,0x15,0x15,0xd0,0x15,0x15};
// Experimental slicer slot layout; the original consumer copies slot[4..47].
// Reject invalid diagnostic offsets before touching RAM (48-offset may underflow).
inline bool writeVbiSlot(uint8_t* slot, const uint8_t* line, unsigned offset, bool reverse) {
  if (offset > 6) return false; // all 42 packet bytes must fit
  std::memset(slot, 0, 48);
  std::memcpy(slot + offset, line, 42);
  if (reverse) for (unsigned j = offset; j < offset + 42; ++j) {
    uint8_t b = slot[j], r = 0;
    for (unsigned q = 0; q < 8; ++q) if (b & (1u << q)) r |= uint8_t(0x80u >> q);
    slot[j] = r;
  }
  slot[5] = 0x27;
  return true;
}
}
