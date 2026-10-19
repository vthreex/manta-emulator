#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// Host-side debugging export of guest memory that an ENABLED, LATCHED GOP window points at.
// It decodes the window's bytes exactly as stored; it never draws, blends, scales or substitutes
// anything. Format handling is deliberately minimal: hardware format 6 is exported as ARGB1555
// (A in bit 15, 5-bit channels replicated to 8 bits), which is an ASSUMPTION documented in
// docs/gop-scanout-export.md (basis: the original GE clears this surface with destination format C
// = ARGB1555_DST). Panel composition, alpha blending, mixer order and scaling are NOT modeled, so an
// exported file is never a claim of the television's output.
inline std::vector<uint8_t> argb1555WindowToBmp32(std::span<const uint8_t> memory, uint32_t pitch,
                                                    uint32_t width, uint32_t height) {
  if (!width || !height || uint64_t(pitch) < uint64_t(width) * 2 || uint64_t(pitch) * height > memory.size())
    throw std::runtime_error("GOP export: window geometry exceeds the observed memory");
  const uint32_t imageBytes = width * height * 4, header = 14 + 40;
  std::vector<uint8_t> out(header + imageBytes);
  auto put16 = [&](size_t at, uint32_t v) { out[at] = uint8_t(v); out[at + 1] = uint8_t(v >> 8); };
  auto put32 = [&](size_t at, uint32_t v) { put16(at, v & 0xffff); put16(at + 2, v >> 16); };
  out[0] = 'B'; out[1] = 'M';
  put32(2, header + imageBytes); put32(10, header);
  put32(14, 40); put32(18, width); put32(22, uint32_t(-int32_t(height))); // negative height: top-down rows
  put16(26, 1); put16(28, 32); put32(34, imageBytes);
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t p = memory[size_t(y) * pitch + x * 2] | (uint32_t(memory[size_t(y) * pitch + x * 2 + 1]) << 8);
      auto expand = [](uint32_t v) { return uint8_t((v << 3) | (v >> 2)); };
      size_t o = header + (size_t(y) * width + x) * 4;
      out[o] = expand(p & 31); out[o + 1] = expand((p >> 5) & 31); out[o + 2] = expand((p >> 10) & 31);
      out[o + 3] = (p & 0x8000) ? 255 : 0;
    }
  return out;
}
}
