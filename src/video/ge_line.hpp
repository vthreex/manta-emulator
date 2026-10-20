#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// EXPLORATORY, opt-in, NOT Titania4-verified. GE command 0x0010 (draw line) into a format-C (ARGB1555_DST) destination.
//
// Evidence: the original ZUI factory menu issues cmd 0x10 with endpoints in registers 68/69 (x0,y0) and 6A/6B (x1,y1), colour in 70/71 (ARGB8888, low halfword
// first), clip rectangle in 55..58, destination 26/27, pitch 33, formats 34 = 0C09; the original line routines (8052395C / 80523B14) write the same
// endpoint registers before the command register 60.
//
// Hypotheses (unverified hardware behaviour): the line is one pixel wide and includes both endpoints; only horizontal and vertical lines are executed
// (any other slope is refused so it shows up as a stop); the colour maps to ARGB1555 exactly as the exploratory quantized fill (alpha FF -> bit 15 set,
// each channel truncated to 5 bits); blending, patterns and gradients (command bit 13) are not modeled.
struct GeLine {
  using Register = std::function<std::pair<uint16_t, uint16_t>(unsigned)>;   // value, known
  uint32_t destination = 0, pitch = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0, color = 0;
  uint32_t clipLeft = 0, clipRight = 0, clipTop = 0, clipBottom = 0;
  bool empty = false;
  static GeLine decode(uint16_t command, const Register& reg, uint16_t ignoredReg00 = 0) {
    if (command != 0x10) throw std::runtime_error("GE raster primitive/mode unsupported");
    auto field = [&](unsigned index, uint16_t mask = 0xffff) {
      auto [value, known] = reg(index);
      if ((known & mask) != mask) throw std::runtime_error("GE line field contains unknown bits");
      return uint32_t(value & mask);
    };
    if ((field(0) & ~ignoredReg00) != 1 || field(1, 0x372) != 0 || (field(0x59) & ~0x40u) != 0)
      throw std::runtime_error("GE line requires an enabled plain primitive; transforms/blending/keying unsupported");
    if ((field(0x34, 0x1f00) >> 8) != 0xc) throw std::runtime_error("GE line supports only the format-C destination");
    GeLine l;
    l.destination = field(0x26) | (field(0x27) << 16);
    if (l.destination >= 0x08000000) throw std::runtime_error("GE line requires mapped MIU0 byte addressing");
    l.pitch = field(0x33);
    l.color = field(0x70) | (field(0x71) << 16);
    l.x0 = field(0x68); l.y0 = field(0x69); l.x1 = field(0x6a); l.y1 = field(0x6b);
    l.clipLeft = field(0x55); l.clipRight = field(0x56); l.clipTop = field(0x57); l.clipBottom = field(0x58);
    for (auto c : {l.x0, l.y0, l.x1, l.y1, l.clipLeft, l.clipRight, l.clipTop, l.clipBottom})
      if (c > 4095) throw std::runtime_error("GE line coordinate outside corroborated field");
    if (l.x0 != l.x1 && l.y0 != l.y1) throw std::runtime_error("GE line: only horizontal and vertical lines are modeled");
    if (!l.pitch || (l.pitch & 1)) throw std::runtime_error("GE line pitch invalid");
    if (l.x0 > l.x1) std::swap(l.x0, l.x1);
    if (l.y0 > l.y1) std::swap(l.y0, l.y1);
    l.empty = l.clipLeft > l.clipRight || l.clipTop > l.clipBottom || l.x1 < l.clipLeft || l.x0 > l.clipRight || l.y1 < l.clipTop || l.y0 > l.clipBottom;
    if (!l.empty) {
      l.x0 = std::max(l.x0, l.clipLeft); l.x1 = std::min(l.x1, l.clipRight);
      l.y0 = std::max(l.y0, l.clipTop); l.y1 = std::min(l.y1, l.clipBottom);
      if (uint64_t(l.x1 + 1) * 2 > l.pitch) throw std::runtime_error("GE line exceeds programmed destination pitch");
      if (uint64_t(l.destination) + uint64_t(l.y1) * l.pitch + uint64_t(l.x1 + 1) * 2 > 0x08000000) throw std::runtime_error("GE line destination overflows MIU0");
    }
    return l;
  }
  uint16_t pixel() const {
    uint32_t a = color >> 24, r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    return uint16_t((a == 255 ? 0x8000u : 0u) | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
  }
  void execute(const std::function<bool(uint32_t, size_t)>& writable, const std::function<void(uint32_t, std::span<const uint8_t>)>& write) const {
    if (empty) return;
    auto start = destination + y0 * pitch + x0 * 2;
    auto extent = size_t(y1 - y0) * pitch + size_t(x1 - x0 + 1) * 2;
    if (!writable || !write || !writable(start, extent)) throw std::runtime_error("GE line destination backing unavailable; no completion");
    auto px = pixel();
    std::vector<uint8_t> row(size_t(x1 - x0 + 1) * 2);
    for (size_t i = 0; i < row.size(); i += 2) { row[i] = uint8_t(px); row[i + 1] = uint8_t(px >> 8); }
    for (uint32_t y = y0; y <= y1; ++y) write(destination + y * pitch + x0 * 2, row);
  }
};
}
