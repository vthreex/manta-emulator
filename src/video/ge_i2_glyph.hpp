#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
// EXPLORATORY, opt-in, NOT Titania4-verified. Executes the one GE request class the original ZUI text path issues for
// anti-aliased glyphs: command 0040, control 0045 (enable + blend + source key, no dither), mode 01 with the stretch bit clear,
// formats 0C01 (source I2, destination C), a 4-entry colour table in registers 35..3C, key range in 14..17.
//
// Hypotheses (every one of them is unverified hardware behaviour):
//   H1  registers 35..3C hold four ARGB8888 words, low halfword first (entry i = reg[36+2i]<<16 | reg[35+2i]);
//   H2  the 2-bit source is packed most-significant pair first, row pitch from register 30 (the dumped glyph reads as a letter this way);
//   H3  the key range [reg 16/17 .. reg 14/15] is compared per RGB channel against the palette entry; a match leaves the destination alone;
//   H4  blend alpha = palette alpha * A / 255, source-over on 8-bit channels (expanded from ARGB1555 by replication), rounded to nearest,
//       then truncated to 5 bits; the destination alpha bit becomes 1. A depends on the blend coefficient in the low byte of register 11
//       (Marlon GE numbering, ASSUMED): 1 = constant alpha, A = register 13; 2 = source alpha, A = 255 (register 13 is not a factor).
//       Evidence for the split is the original code, not hardware: the ZUI font renderer (80520A84) calls the library helper 805200A4 with
//       blend mode 2 and constant alpha 0 for every glyph, bitmap draws (8049D03C) use mode 1 with constant alpha FF. Treating register 13 as a
//       factor in mode 2 made all such text invisible (an earlier version of this model did exactly that);
//   H5  the programmed source height may be 5 less than the destination height (the original's 805241C4 quirk); the model is only accepted
//       when the extra rows are all index 0, so both readings give the same pixels.
struct GeI2Glyph {
  using Reg = std::function<std::pair<uint16_t, uint16_t>(unsigned)>;   // value, known mask
  using Read = std::function<std::vector<uint8_t>(uint32_t, size_t)>;
  uint32_t source = 0, sourcePitch = 0, destination = 0, destinationPitch = 0;
  uint32_t left = 0, top = 0, right = 0, bottom = 0;       // clipped destination rectangle (inclusive)
  uint32_t destLeft = 0, destTop = 0;                       // unclipped destination origin
  uint32_t width = 0, rows = 0, programmedSourceRows = 0;
  std::array<uint32_t, 4> palette{};
  uint32_t keyLow = 0, keyHigh = 0, constantAlpha = 0;
  std::vector<uint8_t> sourceBytes;                          // rows*pitch bytes read at decode time
  static uint32_t field(const Reg& reg, unsigned index) {
    auto [value, known] = reg(index);
    if (known != 0xffff) throw std::runtime_error("GE I2 glyph field contains unknown bits");
    return value;
  }
  static GeI2Glyph decode(uint16_t command, const Reg& reg, const Read& read, bool fullHeight = false) {
    if (command != 0x0040) throw std::runtime_error("GE I2 glyph: not the plain command");
    auto control = field(reg, 0);
    if (control != 0x0045) throw std::runtime_error("GE I2 glyph: control is not enable+blend+key");
    auto mode = reg(1);
    if ((mode.first & mode.second & 0x0010) || (mode.second & 0x0010) != 0x0010) throw std::runtime_error("GE I2 glyph: stretch mode unsupported");
    if (field(reg, 0x34) != 0x0c01) throw std::runtime_error("GE I2 glyph: formats are not I2 -> C");
    if ((field(reg, 0x59) & ~0x40u) != 0) throw std::runtime_error("GE I2 glyph: orientation register non-zero");
    GeI2Glyph g;
    g.source = field(reg, 0x20) | (field(reg, 0x21) << 16);
    g.destination = field(reg, 0x26) | (field(reg, 0x27) << 16);
    g.sourcePitch = field(reg, 0x30); g.destinationPitch = field(reg, 0x33);
    if (g.source >= 0x08000000 || g.destination >= 0x08000000) throw std::runtime_error("GE I2 glyph requires MIU0 byte addressing");
    if (!g.sourcePitch || !g.destinationPitch || (g.destinationPitch & 1)) throw std::runtime_error("GE I2 glyph: pitch invalid");
    g.destLeft = field(reg, 0x68); g.destTop = field(reg, 0x69);
    uint32_t dr = field(reg, 0x6a), db = field(reg, 0x6b);
    uint32_t sx = field(reg, 0x6c), sy = field(reg, 0x6d), sw = field(reg, 0x6e), sh = field(reg, 0x6f);
    if (dr < g.destLeft || db < g.destTop) throw std::runtime_error("GE I2 glyph: destination rectangle invalid");
    g.width = dr - g.destLeft + 1; g.rows = db - g.destTop + 1; g.programmedSourceRows = sh;
    if (sx || sy || sw != g.width) throw std::runtime_error("GE I2 glyph: source origin/width unsupported");
    if (sh != g.rows && sh + 5 != g.rows) throw std::runtime_error("GE I2 glyph: source height is neither the destination height nor height-5");
    if (field(reg, 0x64) != 0x1000 || field(reg, 0x65) != 0x1000) throw std::runtime_error("GE I2 glyph: scale registers are not 1:1");
    for (unsigned i = 0; i < 4; ++i) g.palette[i] = field(reg, 0x35 + 2 * i) | (field(reg, 0x36 + 2 * i) << 16);
    g.keyHigh = field(reg, 0x14) | (field(reg, 0x15) << 16);
    g.keyLow = field(reg, 0x16) | (field(reg, 0x17) << 16);
    auto coefficient = field(reg, 0x11) & 0xff;
    if (coefficient != 1 && coefficient != 2) throw std::runtime_error("GE I2 glyph: blend coefficient is neither constant alpha (1) nor source alpha (2)");
    g.constantAlpha = coefficient == 2 ? 255 : (field(reg, 0x13) & 0xff);
    uint32_t cl = field(reg, 0x55), cr = field(reg, 0x56), ct = field(reg, 0x57), cb = field(reg, 0x58);
    g.left = std::max(g.destLeft, cl); g.right = std::min(dr, cr); g.top = std::max(g.destTop, ct); g.bottom = std::min(db, cb);
    if (cl > cr || ct > cb || g.left > g.right || g.top > g.bottom) { g.left = 1; g.right = 0; return g; }   // empty
    if (uint64_t(g.width + 3) / 4 > g.sourcePitch) throw std::runtime_error("GE I2 glyph: source row exceeds pitch");
    if (uint64_t(g.right + 1) * 2 > g.destinationPitch) throw std::runtime_error("GE I2 glyph exceeds programmed pitch");
    if (uint64_t(g.destination) + uint64_t(g.bottom) * g.destinationPitch + uint64_t(g.right + 1) * 2 > 0x08000000)
      throw std::runtime_error("GE I2 glyph range overflows MIU0");
    // All destination rows must read their source row from guest memory; the rows beyond the programmed height must be index 0.
    g.sourceBytes = read(g.source, size_t(g.sourcePitch) * g.rows);
    if (g.sourceBytes.size() != size_t(g.sourcePitch) * g.rows) throw std::runtime_error("GE I2 glyph: short source read");
    if (!fullHeight && sh + 5 == g.rows)
      for (uint32_t y = sh; y < g.rows; ++y)
        for (uint32_t x = 0; x < g.width; ++x)
          if (g.index(x, y)) throw std::runtime_error("GE I2 glyph: rows hidden by the height-5 quirk are not blank; semantics unestablished");
    return g;
  }
  unsigned index(uint32_t x, uint32_t y) const {
    uint8_t byte = sourceBytes[size_t(y) * sourcePitch + x / 4];
    return (byte >> (6 - 2 * (x % 4))) & 3;   // H2: most significant pair first
  }
  bool keyed(uint32_t argb) const {
    for (unsigned shift : {16u, 8u, 0u}) {   // H3: RGB channels only
      uint32_t c = (argb >> shift) & 0xff, lo = (keyLow >> shift) & 0xff, hi = (keyHigh >> shift) & 0xff;
      if (c < lo || c > hi) return false;
    }
    return true;
  }
  static uint32_t expand(uint32_t five) { return (five << 3) | (five >> 2); }
  // Blends one palette pixel over a destination ARGB1555 word (H4).
  uint16_t blend(uint16_t dest, uint32_t argb) const {
    uint32_t a = ((argb >> 24) & 0xff) * constantAlpha;   // 0..65025
    uint32_t out = 0;
    for (unsigned shift : {10u, 5u, 0u}) {
      uint32_t s = (argb >> (shift == 10 ? 16 : shift == 5 ? 8 : 0)) & 0xff, d = expand((dest >> shift) & 31);
      uint32_t v = (s * a + d * (65025 - a) + 32512) / 65025;
      out |= (v >> 3) << shift;
    }
    return uint16_t(0x8000 | out);
  }
  void execute(const Read& read, const std::function<bool(uint32_t, size_t)>& writable,
               const std::function<void(uint32_t, std::span<const uint8_t>)>& write) const {
    if (left > right) return;
    uint32_t w = right - left + 1, h = bottom - top + 1;
    uint32_t start = destination + top * destinationPitch + left * 2;
    if (!read || !writable || !write || !writable(start, size_t(h - 1) * destinationPitch + w * 2))
      throw std::runtime_error("GE I2 glyph guest backing unavailable; no completion");
    // Read every destination row first (unknown bytes reject before anything is written), then write.
    std::vector<std::vector<uint8_t>> rowsOut;
    for (uint32_t y = 0; y < h; ++y) {
      auto bytes = read(start + y * destinationPitch, size_t(w) * 2);
      if (bytes.size() != size_t(w) * 2) throw std::runtime_error("GE I2 glyph short destination read; no completion");
      for (uint32_t x = 0; x < w; ++x) {
        unsigned i = index(left + x - destLeft, top + y - destTop);
        uint32_t argb = palette[i];
        if (keyed(argb)) continue;
        uint16_t d = uint16_t(bytes[2 * x] | (bytes[2 * x + 1] << 8)), r = blend(d, argb);
        bytes[2 * x] = uint8_t(r); bytes[2 * x + 1] = uint8_t(r >> 8);
      }
      rowsOut.push_back(std::move(bytes));
    }
    for (uint32_t y = 0; y < h; ++y) write(start + y * destinationPitch, rowsOut[y]);
  }
};
}
