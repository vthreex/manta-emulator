#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// EXPLORATORY, opt-in, NOT Titania4-verified: the ZUI's offscreen-to-GOP surface copy.
// Request class: command 0040, control 0005 (enable + blend, no key, no dither), mode 01 stretch bit clear, formats 0C09
// (ARGB1555 -> destination C), 1:1 scale registers, register 11 low byte 0.
//
// Hypotheses: (B1) register 11's low byte selects the blend coefficient with Marlon's numbering (0 = ONE, 1 = CONST, 2 = ASRC), so 0 means
// the source replaces the destination; (B2) the 0x09 -> 0x0C conversion keeps the 16-bit word unchanged; (B3) the original's height-5
// programming (register 6F = destination height - 5, see 805241C4) leaves the last 5 destination rows unread: the model is accepted only
// when copying those rows would not change the destination, so both readings give identical bytes.
struct GeBlendCopy {
  using Reg = std::function<std::pair<uint16_t, uint16_t>(unsigned)>;
  using Read = std::function<std::vector<uint8_t>(uint32_t, size_t)>;
  uint32_t source = 0, sourcePitch = 0, destination = 0, destinationPitch = 0;
  uint32_t left = 0, top = 0, right = 0, bottom = 0;      // clipped destination rectangle (inclusive)
  uint32_t destLeft = 0, destTop = 0, srcX = 0, srcY = 0;
  uint32_t rows = 0, programmedRows = 0;
  std::vector<std::vector<uint8_t>> sourceRows;           // bytes for every clipped destination row
  static uint32_t field(const Reg& reg, unsigned index) {
    auto [value, known] = reg(index);
    if (known != 0xffff) throw std::runtime_error("GE blend copy field contains unknown bits");
    return value;
  }
  // fullHeight = HYPOTHESIS B3': the hardware copies every destination row; register 6F = height-5 is only an encoding artefact
  // (a real TV shows the full banner). false keeps the strict B3 acceptance rule.
  // allowSourceSnapshot = HYPOTHESIS B4: when the source range overlaps the destination, the copy behaves as if every source row were read before any destination write
  // (this decoder always reads all source rows first). The original ZUI's page-flip copy of the 1360x768 surface programs a 769-row clip (bottom 0x300 inclusive), so its last
  // source row aliases the first destination row. Real hardware may stream rows in order; unverified.
  static GeBlendCopy decode(uint16_t command, const Reg& reg, const Read& read, bool fullHeight = false, bool allowSourceSnapshot = false) {
    if (command != 0x0040) throw std::runtime_error("GE blend copy: not the plain command");
    if (field(reg, 0) != 0x0005) throw std::runtime_error("GE blend copy: control is not enable+blend");
    auto mode = reg(1);
    if ((mode.second & 0x0010) != 0x0010 || (mode.first & 0x0010)) throw std::runtime_error("GE blend copy: stretch mode unsupported");
    if (field(reg, 0x34) != 0x0c09) throw std::runtime_error("GE blend copy: formats are not ARGB1555 -> C");
    if ((field(reg, 0x59) & ~0x40u) != 0) throw std::runtime_error("GE blend copy: orientation register non-zero");
    if ((field(reg, 0x11) & 0xff) != 0) throw std::runtime_error("GE blend copy: blend coefficient is not ONE");
    if (field(reg, 0x64) != 0x1000 || field(reg, 0x65) != 0x1000) throw std::runtime_error("GE blend copy: scale registers are not 1:1");
    GeBlendCopy g;
    g.source = field(reg, 0x20) | (field(reg, 0x21) << 16);
    g.destination = field(reg, 0x26) | (field(reg, 0x27) << 16);
    g.sourcePitch = field(reg, 0x30); g.destinationPitch = field(reg, 0x33);
    if (g.source >= 0x08000000 || g.destination >= 0x08000000) throw std::runtime_error("GE blend copy requires MIU0 byte addressing");
    if (!g.sourcePitch || !g.destinationPitch || (g.sourcePitch & 1) || (g.destinationPitch & 1)) throw std::runtime_error("GE blend copy: pitch invalid");
    g.destLeft = field(reg, 0x68); g.destTop = field(reg, 0x69);
    uint32_t dr = field(reg, 0x6a), db = field(reg, 0x6b);
    g.srcX = field(reg, 0x6c); g.srcY = field(reg, 0x6d);
    uint32_t sw = field(reg, 0x6e), sh = field(reg, 0x6f);
    if (dr < g.destLeft || db < g.destTop) throw std::runtime_error("GE blend copy: destination rectangle invalid");
    uint32_t width = dr - g.destLeft + 1; g.rows = db - g.destTop + 1; g.programmedRows = sh;
    if (sw != width) throw std::runtime_error("GE blend copy: source width differs from destination width");
    if (sh != g.rows && sh + 5 != g.rows) throw std::runtime_error("GE blend copy: source height is neither the destination height nor height-5");
    uint32_t cl = field(reg, 0x55), cr = field(reg, 0x56), ct = field(reg, 0x57), cb = field(reg, 0x58);
    g.left = std::max(g.destLeft, cl); g.right = std::min(dr, cr); g.top = std::max(g.destTop, ct); g.bottom = std::min(db, cb);
    if (cl > cr || ct > cb || g.left > g.right || g.top > g.bottom) { g.left = 1; g.right = 0; return g; }
    uint64_t sEnd = uint64_t(g.source) + uint64_t(g.srcY + g.rows - 1) * g.sourcePitch + uint64_t(g.srcX + width) * 2;   // one past the last byte read
    uint64_t dEnd = uint64_t(g.destination) + uint64_t(g.bottom + 1) * g.destinationPitch + uint64_t(g.right + 1) * 2;
    if (uint64_t(g.right + 1) * 2 > g.destinationPitch || uint64_t(g.srcX + width) * 2 > g.sourcePitch) throw std::runtime_error("GE blend copy exceeds programmed pitch");
    if (sEnd > 0x08000000 || dEnd > 0x08000000) throw std::runtime_error("GE blend copy range overflows MIU0");
    uint64_t ss = g.source, de = g.destination + uint64_t(g.bottom + 1) * g.destinationPitch;
    uint64_t ds = g.destination + uint64_t(g.top) * g.destinationPitch, se = sEnd;
    if (ss < de && ds < se && !allowSourceSnapshot) throw std::runtime_error("GE blend copy overlapping ranges unsupported");
    uint32_t w = g.right - g.left + 1;
    for (uint32_t y = g.top; y <= g.bottom; ++y) {
        uint32_t row = y - g.destTop;
        auto bytes = read(g.source + (g.srcY + row) * g.sourcePitch + (g.srcX + g.left - g.destLeft) * 2, size_t(w) * 2);
        if (bytes.size() != size_t(w) * 2) throw std::runtime_error("GE blend copy: short source read");
        if (!fullHeight && sh + 5 == g.rows && row >= sh) {   // B3: copying a hidden row must not change the destination
          auto current = read(g.destination + y * g.destinationPitch + g.left * 2, size_t(w) * 2);
          if (current != bytes) throw std::runtime_error("GE blend copy: rows hidden by the height-5 quirk would change the destination; semantics unestablished");
        }
        g.sourceRows.push_back(std::move(bytes));
    }
    return g;
  }
  void execute(const std::function<bool(uint32_t, size_t)>& writable,
               const std::function<void(uint32_t, std::span<const uint8_t>)>& write) const {
    if (left > right) return;
    uint32_t w = right - left + 1, h = bottom - top + 1;
    if (!writable || !write || !writable(destination + top * destinationPitch + left * 2, size_t(h - 1) * destinationPitch + w * 2))
      throw std::runtime_error("GE blend copy guest backing unavailable; no completion");
    for (uint32_t y = 0; y < h; ++y) write(destination + (top + y) * destinationPitch + left * 2, sourceRows[y]);
  }
};
}
