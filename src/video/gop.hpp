#pragma once
#include "peripherals/riu.hpp"
#include <optional>
namespace manta {
struct GopWindow {
  uint32_t miuRelativeAddress, pitch;
  uint16_t x, y, width, height;
  uint8_t format, bpp;
};
// Original 80538948/A43C window programming, not a host-generated display.
// MIU selection, active shadow latches, palette, scaling and IRQs remain
// unresolved. A decoded descriptor alone never authorizes an authentic frame.
inline std::optional<GopWindow> readGopWindow(const Riu &riu, unsigned bank, unsigned window) {
  if ((bank != 1 && bank != 4) || window >= (bank == 1 ? 4u : 2u)) return {};
  auto word = [&](unsigned index) -> std::optional<uint16_t> {
    auto lo = riu.gopByte(bank, window * 0x80 + index * 4);
    auto hi = riu.gopByte(bank, window * 0x80 + index * 4 + 1);
    if (lo.known != 255 || hi.known != 255) return {};
    return uint16_t(lo.value | (uint16_t(hi.value) << 8));
  };
  auto controlByte = riu.gopByte(bank, window * 0x80);
  std::optional<uint16_t> control;
  if ((controlByte.known & 0xf1) == 0xf1) control = controlByte.value;
  auto low = word(1), high = word(2), pitch = word(9);
  auto hs = word(4), he = word(5), vs = word(6), ve = word(7);
  if (!control || !(*control & 1) || !low || !high || !pitch || !hs || !he || !vs || !ve) return {};
  unsigned format = (*control >> 4) & 15, bpp;
  if (format == 5) bpp = 32;
  else if (format == 4) bpp = 8;
  else if (format <= 3 || format == 6 || format == 8 || format == 9) bpp = 16;
  else return {}; // stock 8053A5C4 rejects other enums
  auto pixelsPerUnit = 64 / bpp;
  auto x = uint32_t(*hs) * pixelsPerUnit, end = uint32_t(*he) * pixelsPerUnit;
  if (*ve <= *vs || end <= x || end > 1920 || *ve > 1080 || uint32_t(*pitch) * 8 < (end-x)*bpp/8)
    return {};
  return GopWindow{(uint32_t(*low) | (uint32_t(*high) << 16)) * 8, uint32_t(*pitch) * 8,
                   uint16_t(x), *vs, uint16_t(end-x), uint16_t(*ve-*vs), uint8_t(format), uint8_t(bpp)};
}
}
