// EXPLORATORY JPEG decoder block ("JPD") of the multimedia coprocessor, modelled from the original driver's own register programming.
// See docs/jpd-registers.md for what is verified and what is an assumption. Anything the model does not understand stops the coprocessor
// (via std::runtime_error) instead of producing a made-up result.
//
//   verified on the logo JPEG:  Huffman/quantisation table-memory format (tools/jpd_table_check.py, 49,776 blocks, 0 mismatches),
//                               register meanings of 2E08, 2E0C/10, 2E2C/30, 2E34..40, 2E44/48 (values equal quantities derived from the JPEG/driver)
//   from the driver code:       SCONFIG fields: [1:0] Y horizontal sampling (1,2; 3 = 4), bit2 = Y vertical sampling - 1, bit3 = colour, [5:4] scale code
//                               (0 = 1/1, 1 = 1/2, 2 = 1/4, 3 = 1/8), bit6 = restart enable, bit7 = START
//   ASSUMPTIONS (labelled):     component -> table selection (Y: Q0/DC0/AC0, Cb and Cr: Q1/DC1/AC1), reduced-size IDCT = N-point IDCT of the top-left
//                               NxN coefficients scaled by N/8, 4:2:0 -> 4:2:2 chroma by vertical replication, packed byte order Y0 U Y1 V,
//                               event bit 0 = decode done, instantaneous completion
#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
class Jpd {
public:
  static constexpr uint32_t kBase = 0x2E00u, kEnd = 0x2F08u;
  uint8_t* ram = nullptr;
  size_t ramSize = 0;
  std::function<void(uint32_t, size_t)> ramWritten;
  uint64_t starts = 0, completed = 0, blocks = 0;
  std::string lastNote, dumpPath;   // dumpPath: diagnostic - write each decoded picture (raw packed 4:2:2) to this file

  void serialize(snap::Ar& a) { a.tag("jpd"); a(r_, table_, tableIndex_, events_, writes_, starts, completed, blocks, lastNote); }
  bool owns(uint32_t off, unsigned n) const { return off >= kBase && off + n <= kEnd; }
  uint32_t read(uint32_t off, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= uint32_t(readByte(off + i)) << (8 * i);
    return v;
  }
  void write(uint32_t off, unsigned n, uint32_t v) {
    if (n == 2 && (off & 1) == 0) { write16(off, uint16_t(v)); return; }
    if (n == 1) {   // byte store: merge into the stored half-word
      uint16_t w = reg(off & ~1u);
      w = (off & 1) ? uint16_t((w & 0x00ff) | (v & 0xff) << 8) : uint16_t((w & 0xff00) | (v & 0xff));
      write16(off & ~1u, w); return;
    }
    throw std::runtime_error("JPD register access width " + std::to_string(n) + " at " + std::to_string(off) + " unsupported");
  }

private:
  std::array<uint16_t, 0x80> r_{};            // registers 0x2E00..0x2EFF as half-words (index = (off - 0x2E00) / 2)
  std::array<uint16_t, 3 * 512> table_{};     // table memory banks at word index 0x000, 0x200, 0x400
  uint32_t tableIndex_ = 0;
  uint16_t events_ = 0;

  uint16_t& reg(uint32_t off) { return r_[(off - kBase) >> 1]; }
  uint8_t readByte(uint32_t off) {
    uint32_t base = off & ~1u; uint16_t w;
    if (base == 0x2E28) w = events_;
    else if (base == 0x2F00) w = uint16_t(tableIndex_);
    else if (base == 0x2F04) throw std::runtime_error("JPD table data port read unsupported");
    else if (base >= 0x2F00) w = 0;
    else w = reg(base);
    return (off & 1) ? uint8_t(w >> 8) : uint8_t(w);
  }
  void write16(uint32_t off, uint16_t v) {
    ++writes_;
    if (off == 0x2F00) { tableIndex_ = v; return; }
    if (off == 0x2F04) {
      if (tableIndex_ >= table_.size()) throw std::runtime_error("JPD table index out of range");
      table_[tableIndex_++] = v; return;
    }
    if (off == 0x2E28) { events_ &= uint16_t(~v); return; }   // event flags: write 1 to clear (the driver writes 0x7F to clear everything)
    if (off == 0x2E04) {
      bool reset = (v & 8) && !(reg(0x2E04) & 8);
      reg(off) = v;
      if (reset) { events_ = 0; }                               // bit 3 pulsed by the driver at start-up: software reset
      return;
    }
    if (off == 0x2E00) {
      uint16_t old = reg(off); reg(off) = v;
      if ((v & 0x80) && !(old & 0x80)) start();
      return;
    }
    if (off >= 0x2E00 && off < 0x2F00) { reg(off) = v; return; }
    throw std::runtime_error("JPD register write outside the modelled window");
  }
  uint64_t writes_ = 0;

  // ---- decode ----
  struct Bits {
    const uint8_t* d; size_t n; size_t p = 0;   // byte-unstuffed segment, bit position
    uint32_t peek16() const {
      uint32_t v = 0;
      for (int k = 0; k < 3; ++k) { size_t i = (p >> 3) + k; v = (v << 8) | (i < n ? d[i] : 0); }
      return (v >> (8 - (p & 7))) & 0xffff;
    }
    uint32_t get(unsigned c) { uint32_t v = 0; for (unsigned k = 0; k < c; ++k) { size_t i = p >> 3; unsigned b = i < n ? (d[i] >> (7 - (p & 7))) & 1 : 0; v = (v << 1) | b; ++p; } return v; }
  };
  struct HRec { unsigned L; uint32_t first16, base; };
  std::vector<HRec> huff(unsigned t) const {   // t: 0 DC0, 1 DC1, 2 AC0, 3 AC1
    std::vector<HRec> out; const bool ac = t >= 2;
    for (unsigned L = 1; L <= 16; ++L) {
      uint16_t lo = table_[t * 32 + 2 * (L - 1)], hi = table_[t * 32 + 2 * (L - 1) + 1];
      if (!hi) continue;
      unsigned n = L > 8 ? L - 8 : 0;
      uint32_t first16 = uint32_t(hi & 0xff) << 8;
      if (n) first16 |= ((uint32_t(lo) >> (16 - n)) << (8 - n)) & 0xff;
      out.push_back({L, first16, ac ? uint32_t(lo & 0xff) : uint32_t((lo & 0xff) >> 4)});
    }
    return out;
  }
  unsigned symbol(Bits& b, const std::vector<HRec>& h, unsigned t) const {
    uint32_t peek = b.peek16(); const HRec* best = nullptr;
    for (auto& r : h) if (r.first16 <= peek) best = &r;
    if (!best) throw std::runtime_error("JPD: invalid Huffman code");
    uint32_t code = peek >> (16 - best->L), first = best->first16 >> (16 - best->L);
    uint32_t idx = best->base + (code - first); b.get(best->L);
    const bool ac = t >= 2; uint16_t w = ac ? table_[512 + idx] : table_[512 + 256 + idx];
    return (t & 1) ? (w >> 8) & 255 : w & 255;
  }
  static int extend(int v, unsigned n) { return (n == 0 || v >= (1 << (n - 1))) ? v : v - (1 << n) + 1; }
  static const std::array<uint8_t, 64>& zigzag() {
    static const std::array<uint8_t, 64> z = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
    return z;
  }
  // reduced-size IDCT of the top-left NxN coefficients (N = 8, 4): orthonormal N-point 2-D IDCT scaled by N/8
  static void idct(const int* coef, unsigned N, int* out) {
    static double c[9][8][8]; static bool init = false;
    if (!init) {
      for (unsigned n : {4u, 8u}) for (unsigned x = 0; x < n; ++x) for (unsigned u = 0; u < n; ++u)
        c[n][x][u] = (u == 0 ? std::sqrt(1.0 / n) : std::sqrt(2.0 / n)) * std::cos((2 * x + 1) * u * 3.14159265358979323846 / (2.0 * n));
      init = true;
    }
    double tmp[8][8];
    for (unsigned y = 0; y < N; ++y) for (unsigned x = 0; x < N; ++x) { double s = 0; for (unsigned u = 0; u < N; ++u) s += c[N][x][u] * coef[y * 8 + u]; tmp[y][x] = s; }
    for (unsigned x = 0; x < N; ++x) for (unsigned y = 0; y < N; ++y) { double s = 0; for (unsigned v = 0; v < N; ++v) s += c[N][y][v] * tmp[v][x]; out[y * N + x] = int(std::lround(s * N / 8.0)); }
  }
  void start() {
    ++starts;
    const uint16_t sc = reg(0x2E00);
    const unsigned h = sc & 3, vert = ((sc >> 2) & 1) + 1, scale = (sc >> 4) & 3;
    if (!(sc & 8)) throw std::runtime_error("JPD: grayscale decode not modelled");
    if (h != 2 || vert != 2) throw std::runtime_error("JPD: only 4:2:0 (Y 2x2) is modelled; SCONFIG=" + std::to_string(sc));
    if (scale > 1) throw std::runtime_error("JPD: only scale codes 0 and 1 are modelled");
    if (sc & 0x200) throw std::runtime_error("JPD: separate U/V quantisation tables (SUVQ) not modelled");
    const unsigned N = scale == 0 ? 8 : 4;
    const unsigned wBlocks = reg(0x2E0C), hBlocks = reg(0x2E10);   // picture size / 8, aligned to the MCU
    if (!wBlocks || !hBlocks || (wBlocks & 1) || (hBlocks & 1)) throw std::runtime_error("JPD: picture size registers not MCU aligned");
    const unsigned mcuW = wBlocks / 2, mcuH = hBlocks / 2, interval = (sc & 0x40) ? unsigned(reg(0x2E08)) + 1 : 0;
    const uint32_t scanAddr = uint32_t(reg(0x2E2C)) | uint32_t(reg(0x2E30)) << 16;
    const uint32_t rdLo = (uint32_t(reg(0x2E34)) | uint32_t(reg(0x2E38)) << 16) * 8, rdHi = (uint32_t(reg(0x2E3C)) | uint32_t(reg(0x2E40)) << 16) * 8 + 7;
    const uint32_t outAddr = (uint32_t(reg(0x2E44)) | uint32_t(reg(0x2E48)) << 16) * 8;
    if (scanAddr < rdLo || scanAddr > rdHi || rdHi >= ramSize) throw std::runtime_error("JPD: scan start outside the read buffer");
    // gather the entropy-coded data (unstuffed), split at restart markers
    std::vector<std::vector<uint8_t>> segs(1);
    for (uint32_t a = scanAddr; a <= rdHi; ++a) {
      uint8_t b = ram[a];
      if (b == 0xff) {
        uint8_t m = a + 1 <= rdHi ? ram[a + 1] : 0;
        if (m == 0) { segs.back().push_back(0xff); ++a; continue; }
        if (m >= 0xd0 && m <= 0xd7) { segs.emplace_back(); ++a; continue; }
        break;   // EOI or other marker ends the scan
      }
      segs.back().push_back(b);
    }
    const auto dc0 = huff(0), dc1 = huff(1), ac0 = huff(2), ac1 = huff(3);
    const unsigned outW = mcuW * 2 * N, outH = mcuH * 2 * N, pitch = outW * 2;
    if (uint64_t(outAddr) + uint64_t(pitch) * outH > ramSize) throw std::runtime_error("JPD: output buffer outside DRAM");
    std::vector<uint8_t> yp(size_t(outW) * outH), up(size_t(outW / 2) * (outH / 2)), vp(up.size());
    unsigned seg = 0; Bits bits{nullptr, 0}; int pred[3] = {0, 0, 0}; unsigned inSeg = 0;
    for (unsigned m = 0; m < mcuW * mcuH; ++m) {
      if (m == 0 || (interval && inSeg == interval)) {
        if (m) ++seg;
        if (seg >= segs.size()) throw std::runtime_error("JPD: entropy data ended before the picture was complete");
        bits = Bits{segs[seg].data(), segs[seg].size()}; pred[0] = pred[1] = pred[2] = 0; inSeg = 0;
      }
      ++inSeg;
      const unsigned mx = m % mcuW, my = m / mcuW;
      for (unsigned c = 0, blk = 0; c < 3; ++c) for (unsigned b = 0; b < (c ? 1u : 4u); ++b, ++blk) {
        const unsigned dct = c ? 1 : 0, act = c ? 3 : 2, qb = c ? 1 : 0;
        int coef[64] = {};
        unsigned s = symbol(bits, c ? dc1 : dc0, dct);
        pred[c] += s ? extend(int(bits.get(s)), s) : 0;
        coef[0] = pred[c] * table_[1024 + qb * 64 + 0];
        for (unsigned k = 1; k < 64;) {
          unsigned a = symbol(bits, c ? ac1 : ac0, act);
          if (a == 0) break;
          if (a == 0xf0) { k += 16; continue; }
          k += a >> 4; if (k > 63) throw std::runtime_error("JPD: AC run beyond the block");
          int val = extend(int(bits.get(a & 15)), a & 15);
          unsigned pos = zigzag()[k]; coef[pos] = val * table_[1024 + qb * 64 + pos]; ++k;
        }
        int px[64]; idct(coef, N, px); ++blocks;
        const unsigned bx = c ? 0 : (b & 1), by = c ? 0 : (b >> 1);
        for (unsigned y = 0; y < N; ++y) for (unsigned x = 0; x < N; ++x) {
          int v = std::min(255, std::max(0, px[y * N + x] + 128));
          if (c == 0) yp[size_t(my * 2 * N + by * N + y) * outW + mx * 2 * N + bx * N + x] = uint8_t(v);
          else (c == 1 ? up : vp)[size_t(my * N + y) * (outW / 2) + mx * N + x] = uint8_t(v);
        }
        (void)blk;
      }
    }
    // pack 4:2:2 (Y0 U Y1 V) with vertical chroma replication
    for (unsigned y = 0; y < outH; ++y) for (unsigned x = 0; x < outW; x += 2) {
      uint8_t* o = ram + outAddr + size_t(y) * pitch + size_t(x) * 2;
      o[0] = yp[size_t(y) * outW + x]; o[1] = up[size_t(y / 2) * (outW / 2) + x / 2]; o[2] = yp[size_t(y) * outW + x + 1]; o[3] = vp[size_t(y / 2) * (outW / 2) + x / 2];
    }
    if (ramWritten) ramWritten(outAddr, size_t(pitch) * outH);
    if (!dumpPath.empty()) { FILE* f = std::fopen(dumpPath.c_str(), "wb"); if (f) { std::fwrite(ram + outAddr, 1, size_t(pitch) * outH, f); std::fclose(f); } }
    events_ |= 1;   // ASSUMPTION: event bit 0 = decode done
    ++completed;
    lastNote = "decoded " + std::to_string(mcuW * mcuH) + " MCUs to " + std::to_string(outW) + "x" + std::to_string(outH) + " packed 4:2:2 at " + std::to_string(outAddr);
  }
};
}  // namespace manta
