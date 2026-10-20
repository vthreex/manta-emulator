#pragma once
#include "video/ge_stretch_request.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// EXPLORATORY, opt-in, never Titania4-verified output.
//
// A stretched GE BitBlt whose whole sampled source region is ONE colour cannot depend on the unknown sampling
// position, phase rounding or (normalized) filter taps, so the only unestablished semantics left are the alpha
// handling and the dither. This class executes exactly that case under an explicitly selected alpha hypothesis:
//   OpaqueSource - the ARGB1555 alpha bit is ignored; constant alpha 0xFF (GE register 13) makes the source opaque and the
//                  destination pixel becomes 0x8000 | rgb555.
//   AlphaBit     - alpha bit 0 means transparent: nothing is written; alpha bit 1 writes the source word.
// Dither is assumed to leave a 5-bit-to-5-bit conversion unchanged. Non-uniform sources, source keys and other formats stay guarded.
struct GeUniformStretch {
  enum class Alpha { OpaqueSource, AlphaBit };
  uint32_t destination = 0, pitch = 0;
  uint32_t left = 0, top = 0, right = 0, bottom = 0;   // clipped destination rectangle (inclusive)
  uint16_t pixel = 0;
  bool empty = false;
  uint32_t sourceWords = 0;
  // EXPLORATORY non-uniform sampling (nearestNonUniform): position = d*ratio + signed phase (4.12; the original's phase is the half-pixel centring term (src-dst)/(2*dst)), clamped at 0, then ROUNDED to the nearest source pixel; no filter taps,
  // opaque-source alpha, dither/blend assumed to leave the 5-bit word unchanged. Every one of these is an ASSUMPTION; hardware sampling position and filtering are unestablished.
  bool sampled = false; std::vector<uint16_t> samples;   // clipped rectangle, row-major
  // EXPLORATORY source key (hypothesis K1): a sampled pixel is transparent (not written) when each of its R, G, B channels, expanded to 8 bits by bit replication, lies inside
  // [low, high] of the key registers (16/17 = low, 14/15 = high, per channel, alpha ignored). Evidence: the original programs 00F800F8..00FF00FF for the magenta key of bitmap 0, i.e.
  // the 5-bit value 31 expanded with zero fill and with one fill. Whether the hardware compares before or after filtering is unknown (here: the nearest sample).
  std::vector<uint8_t> keep;
  using Read = std::function<std::vector<uint8_t>(uint32_t, size_t)>;
  static GeUniformStretch decode(const GeStretchRequest& r, Alpha model, const Read& read, bool nearestNonUniform = false) {
    if (r.sourceFormat != 9 || r.destinationFormat != 0x0c) throw std::runtime_error("GE uniform stretch: only ARGB1555 -> format C");
    if (r.sourceKey() && !(nearestNonUniform && model == Alpha::OpaqueSource)) throw std::runtime_error("GE uniform stretch: source key unsupported");
    if (r.sourceKey() && r.keyModeBit) throw std::runtime_error("GE source key: register 1C bit 0 set; semantics unknown");
    if (r.source >= 0x08000000 || r.destination >= 0x08000000) throw std::runtime_error("GE uniform stretch requires MIU0 byte addressing");
    if (!r.destinationPitch || r.destinationPitch % 2 || !r.sourcePitch || r.sourcePitch % 2) throw std::runtime_error("GE uniform stretch: pitch invalid");
    // Every source row the scale arithmetic or the programmed rectangle can touch must hold the same word.
    uint32_t rows = std::max(r.srcHeight, r.scaleSourceHeight);
    if (uint64_t(r.srcX + r.srcWidth) * 2 > r.sourcePitch) throw std::runtime_error("GE uniform stretch: source row exceeds pitch");
    if (!read) throw std::runtime_error("GE uniform stretch: guest source unavailable");
    bool have = false, uniform = true; uint16_t word = 0; uint32_t count = 0;
    std::vector<uint16_t> grid;
    for (uint32_t y = 0; y < rows; ++y) {
      auto bytes = read(r.source + (r.srcY + y) * r.sourcePitch + r.srcX * 2, size_t(r.srcWidth) * 2);
      if (bytes.size() != size_t(r.srcWidth) * 2) throw std::runtime_error("GE uniform stretch: short source read");
      for (uint32_t x = 0; x < r.srcWidth; ++x) {
        uint16_t w = uint16_t(bytes[2 * x] | (bytes[2 * x + 1] << 8));
        if (!have) { word = w; have = true; }
        else if (w != word) uniform = false;
        grid.push_back(w);
        ++count;
      }
    }
    if (r.sourceKey()) uniform = false;   // keyed sources always use the per-pixel path
    if (!uniform && !(nearestNonUniform && model == Alpha::OpaqueSource))
      throw std::runtime_error("GE uniform stretch: source is not a single colour; sampling/filter semantics unestablished");
    GeUniformStretch p;
    p.sourceWords = count;
    p.destination = r.destination; p.pitch = r.destinationPitch;
    uint32_t cl = r.clipLeft, cr = r.clipRight, ct = r.clipTop, cb = r.clipBottom;
    p.left = std::max(r.destLeft, cl); p.right = std::min(r.destRight, cr);
    p.top = std::max(r.destTop, ct); p.bottom = std::min(r.destBottom, cb);
    p.empty = cl > cr || ct > cb || p.left > p.right || p.top > p.bottom;
    if (model == Alpha::AlphaBit && !(word & 0x8000)) p.empty = true;   // transparent source: no write
    p.pixel = model == Alpha::OpaqueSource ? uint16_t(0x8000 | (word & 0x7fff)) : word;
    if (!p.empty && nearestNonUniform && uint64_t(p.right + 1) * 2 > p.pitch && p.pitch >= 2 * (p.left + 1)) {
      // HYPOTHESIS P1 (nearest model only): columns beyond the destination pitch are not written (the rectangle is clamped to the pitch). The original ZUI's
      // selection-bar draw programs destination x 65..189 into a 188-pixel (376-byte) pitch; wrap-around into the next row is not assumed.
      p.right = p.pitch / 2 - 1;
    }
    if (!p.empty) {
      if (uint64_t(p.right + 1) * 2 > p.pitch) throw std::runtime_error("GE uniform stretch exceeds programmed pitch");
      uint64_t end = uint64_t(p.destination) + uint64_t(p.bottom) * p.pitch + uint64_t(p.right + 1) * 2;
      if (end > 0x08000000) throw std::runtime_error("GE uniform stretch range overflows MIU0");
    }
    if (!uniform && !p.empty) {
      if (r.ratioX == 0xffffffffu || r.ratioY == 0xffffffffu) throw std::runtime_error("GE nearest stretch: ratio outside the modelled case");
      p.sampled = true;
      for (uint32_t y = p.top; y <= p.bottom; ++y)
        for (uint32_t x = p.left; x <= p.right; ++x) {
          int64_t px = std::max<int64_t>(0, int64_t(x - r.destLeft) * r.ratioX + r.phaseX), py = std::max<int64_t>(0, int64_t(y - r.destTop) * r.ratioY + r.phaseY);
          uint64_t sx = uint64_t(px + 2048) >> 12, sy = uint64_t(py + 2048) >> 12;
          if (sx >= r.srcWidth) sx = r.srcWidth - 1;
          if (sy >= r.srcHeight + (r.heightReducedBy5 ? 5 : 0)) sy = r.srcHeight + (r.heightReducedBy5 ? 5 : 0) - 1;
          if (sx >= r.srcWidth || sy >= rows) throw std::runtime_error("GE nearest stretch: sample outside the source rectangle");
          uint16_t w = grid[sy * r.srcWidth + sx];
          bool visible = true;
          if (r.sourceKey()) {
            auto ex = [](uint32_t v) { return (v << 3) | (v >> 2); };
            uint32_t c[3] = {ex((w >> 10) & 31), ex((w >> 5) & 31), ex(w & 31)};
            uint32_t lo[3] = {(r.keyLow >> 16) & 255, (r.keyLow >> 8) & 255, r.keyLow & 255}, hi[3] = {(r.keyHigh >> 16) & 255, (r.keyHigh >> 8) & 255, r.keyHigh & 255};
            bool inside = true;
            for (int k = 0; k < 3; ++k) inside = inside && c[k] >= lo[k] && c[k] <= hi[k];
            visible = !inside;
          }
          p.keep.push_back(visible ? 1 : 0);
          p.samples.push_back(uint16_t(0x8000 | (w & 0x7fff)));
        }
    }
    return p;
  }
  void execute(const std::function<bool(uint32_t, size_t)>& writable,
               const std::function<void(uint32_t, std::span<const uint8_t>)>& write) const {
    if (empty) return;
    uint32_t width = (right - left + 1) * 2, height = bottom - top + 1;
    uint32_t start = destination + top * pitch + left * 2;
    if (!writable || !write || !writable(start, size_t(height - 1) * pitch + width))
      throw std::runtime_error("GE uniform stretch guest backing unavailable; no completion");
    if (sampled) {
      std::vector<uint8_t> line(width);
      for (uint32_t y = 0; y < height; ++y) {
        uint32_t x = 0, w2 = width / 2;
        while (x < w2) {
          while (x < w2 && !keep[y * w2 + x]) ++x;      // keyed pixels leave the destination untouched
          uint32_t run = x;
          while (x < w2 && keep[y * w2 + x]) { auto s = samples[y * w2 + x]; line[2 * (x - run)] = uint8_t(s); line[2 * (x - run) + 1] = uint8_t(s >> 8); ++x; }
          if (x > run) write(start + y * pitch + run * 2, std::span<const uint8_t>(line.data(), 2 * (x - run)));
        }
      }
      return;
    }
    std::vector<uint8_t> row(width);
    for (uint32_t x = 0; x < width / 2; ++x) { row[2 * x] = uint8_t(pixel); row[2 * x + 1] = uint8_t(pixel >> 8); }
    for (uint32_t y = 0; y < height; ++y) write(start + y * pitch, row);
  }
};
}
