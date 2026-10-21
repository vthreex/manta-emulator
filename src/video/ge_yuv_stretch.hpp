#pragma once
#include "video/ge_stretch_request.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
namespace manta {
// EXPLORATORY, opt-in, never Titania4-verified output: a GE stretched BitBlt with source AND destination format 0xE (packed YUV 4:2:2).
// Established by evidence: the request itself (decoded by GeStretchRequest from the original library's register programming) and, for the boot logo, that the
// source is the multimedia coprocessor's JPD output buffer. ASSUMPTIONS (labelled): format 0xE is packed 16 bits/pixel Y0 U Y1 V (the same assumption as the JPD
// output model), nearest-neighbour sampling at (d*ratio + phase + 0.5) in 4.12 (the established nearest hypothesis of --exploratory-ge-nearest-stretch), the chroma
// of a destination pixel pair comes from the nearest source pixel pair of the pair's first destination pixel, no dither/blend/key/alpha effect.
struct GeYuvStretch {
  uint32_t destination = 0, pitch = 0, left = 0, top = 0, right = 0, bottom = 0;
  bool empty = false;
  std::vector<uint16_t> out;   // clipped rectangle, row-major 16-bit words (little endian in memory)
  using Read = std::function<std::vector<uint8_t>(uint32_t, size_t)>;
  static GeYuvStretch decode(const GeStretchRequest& r, const Read& read) {
    // 0xE -> 0xE (packed YUV) or 0xE -> 0xC (ARGB1555 destination; EXPLORATORY BT.601 full-range conversion, 5-bit truncation, alpha bit forced on: all assumptions)
    if (r.sourceFormat != 0x0e || (r.destinationFormat != 0x0e && r.destinationFormat != 0x0c)) throw std::runtime_error("GE YUV stretch: only format 0xE -> 0xE or 0xE -> 0xC");
    const bool toRgb = r.destinationFormat == 0x0c;
    // Blend with GE register 11 low byte 0 ("ONE", the established reading of the blend-copy model) is treated as REPLACE (HYPOTHESIS); any other blend stays refused.
    if (r.sourceKey() || r.dither() || (r.blend() && (r.alpha11 & 0xff) != 0)) throw std::runtime_error("GE YUV stretch: key/dither/blend not modelled");
    if (r.source >= 0x08000000 || r.destination >= 0x08000000) throw std::runtime_error("GE YUV stretch requires MIU0 byte addressing");
    if (!r.destinationPitch || r.destinationPitch % 4 || !r.sourcePitch || r.sourcePitch % 4) throw std::runtime_error("GE YUV stretch: pitch invalid");
    if (!read) throw std::runtime_error("GE YUV stretch: guest source unavailable");
    if (r.ratioX == 0xffffffffu || r.ratioY == 0xffffffffu || !r.srcWidth || !r.srcHeight) throw std::runtime_error("GE YUV stretch: ratio outside the modelled case");
    GeYuvStretch p;
    p.destination = r.destination; p.pitch = r.destinationPitch;
    p.left = std::max(r.destLeft, r.clipLeft); p.right = std::min(r.destRight, r.clipRight);
    p.top = std::max(r.destTop, r.clipTop); p.bottom = std::min(r.destBottom, r.clipBottom);
    p.empty = r.clipLeft > r.clipRight || r.clipTop > r.clipBottom || p.left > p.right || p.top > p.bottom;
    if (p.empty) return p;
    if (uint64_t(p.right + 1) * 2 > p.pitch) throw std::runtime_error("GE YUV stretch exceeds programmed pitch");
    if (uint64_t(p.destination) + uint64_t(p.bottom + 1) * p.pitch > 0x08000000) throw std::runtime_error("GE YUV stretch range overflows MIU0");
    const uint32_t rows = r.srcHeight + (r.heightReducedBy5 ? 5 : 0);
    if (uint64_t(r.srcX + r.srcWidth) * 2 > r.sourcePitch) throw std::runtime_error("GE YUV stretch: source row exceeds pitch");
    std::vector<std::vector<uint8_t>> src(rows);
    for (uint32_t y = 0; y < rows; ++y) {
      src[y] = read(r.source + (r.srcY + y) * r.sourcePitch + r.srcX * 2, size_t(r.srcWidth) * 2);
      if (src[y].size() != size_t(r.srcWidth) * 2) throw std::runtime_error("GE YUV stretch: short source read");
    }
    auto position = [&](uint32_t d, uint32_t origin, uint32_t ratio, int phase, uint32_t limit) {
      int64_t q = std::max<int64_t>(0, int64_t(d - origin) * ratio + phase);
      uint64_t s = uint64_t(q + 2048) >> 12; return uint32_t(std::min<uint64_t>(s, limit - 1));
    };
    for (uint32_t y = p.top; y <= p.bottom; ++y) {
      uint32_t sy = position(y, r.destTop, r.ratioY, r.phaseY, rows);
      const auto& row = src[sy];
      for (uint32_t x = p.left; x <= p.right; ++x) {
        uint32_t sx = position(x, r.destLeft, r.ratioX, r.phaseX, r.srcWidth);
        uint32_t cx = position(x & ~1u, r.destLeft, r.ratioX, r.phaseX, r.srcWidth) & ~1u;   // chroma pair of the pair's first destination pixel
        uint8_t yv = row[2 * sx], u = row[2 * cx + 1], v = (cx + 1 < r.srcWidth) ? row[2 * (cx + 1) + 1] : row[2 * cx + 1];
        if (!toRgb) { p.out.push_back(uint16_t(yv | ((x & 1) ? v : u) << 8)); continue; }
        // chroma shared by the pair: use this pair's U and V
        uint8_t cu = u, cv = (cx + 1 < r.srcWidth) ? row[2 * (cx + 1) + 1] : u;
        auto clamp = [](double d) { return uint32_t(d < 0 ? 0 : d > 255 ? 255 : d + 0.5); };
        uint32_t R = clamp(yv + 1.402 * (cv - 128)), G = clamp(yv - 0.344136 * (cu - 128) - 0.714136 * (cv - 128)), Bc = clamp(yv + 1.772 * (cu - 128));
        p.out.push_back(uint16_t(0x8000 | (R >> 3) << 10 | (G >> 3) << 5 | (Bc >> 3)));
      }
    }
    return p;
  }
  void execute(const std::function<bool(uint32_t, size_t)>& writable, const std::function<void(uint32_t, std::span<const uint8_t>)>& write) const {
    if (empty) return;
    uint32_t w = right - left + 1, h = bottom - top + 1, start = destination + top * pitch + left * 2;
    if (!writable || !write || !writable(start, size_t(h - 1) * pitch + size_t(w) * 2)) throw std::runtime_error("GE YUV stretch guest backing unavailable; no completion");
    std::vector<uint8_t> line(size_t(w) * 2);
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) { line[2 * x] = uint8_t(out[size_t(y) * w + x]); line[2 * x + 1] = uint8_t(out[size_t(y) * w + x] >> 8); }
      write(start + y * pitch, line);
    }
  }
};
}  // namespace manta
