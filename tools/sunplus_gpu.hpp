// EXPLORATORY model of the SUNPLUS GPU command FIFO (Sunplus SPHE8600). Derived from the DirectFB "spgraphic" driver in the kernel blob
// (0x8016c400..0x80171800) and recorded FIFO traces; every semantic below is a hypothesis to be checked against rendered surfaces.
//
// NOTE: for blits regs 0x50/0x51/0x56 describe the SOURCE surface and 0x60/0x61/0x66 the DESTINATION (fill: destination = 0x60..).
// Command word = (reg << 24) | data24. Words with reg 0xA0 execute: low nibble = command, bits 8.. = DirectFB blit/draw flags.
//   cmd 0 fill rectangle   cmd 3 blit   cmd 5 stretch blit   (cmd 1/2/4: line / triangle / tile, not modelled)
// Surfaces: format byte 0x0c (the only one seen) is treated as 8 bits per pixel palette indices (LUT8), tightly packed rows of `width` bytes
// (the source data in RAM shows gradient bitmaps made of repeated index bytes, and the OSD windows carry CLUT registers).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct SunplusGpu {
  uint32_t reg[256] = {};
  uint8_t* ram = nullptr; size_t ramSize = 0;
  uint64_t executed[8] = {}, unsupported = 0, clipped = 0;
  static int sx12(uint32_t v) { v &= 0xfff; return v & 0x800 ? int(v) - 0x1000 : int(v); }
  struct Surf { uint32_t base; int w, h; bool ok; };
  Surf surface(int baseLo, int dimsReg) const {
    Surf s; const uint32_t b = ((reg[baseLo + 1] & 0xffff) << 16 | (reg[baseLo] & 0xffff)) & 0x0fffffff;
    s.base = b; s.w = int((reg[dimsReg] >> 12) & 0xfff) + 1; s.h = int(reg[dimsReg] & 0xfff) + 1;
    s.ok = ram && b + size_t(s.w) * s.h <= ramSize; return s;
  }
  uint8_t* px(const Surf& s, int x, int y) const { return ram + s.base + size_t(y) * s.w + x; }
  uint8_t color() const { return uint8_t(reg[0x73] & 0xff); }   // index colour = low byte of reg 0x73 (hypothesis)
  void push(uint32_t w) {
    const uint32_t r = w >> 24;
    if (r != 0xA0) { reg[r] = w & 0xffffff; return; }
    const uint32_t cmd = w & 0xf, flags = (w >> 8) & 0xff; if (cmd < 8) ++executed[cmd];
    if (cmd == 0) fill(flags); else if (cmd == 3) blit(flags, false); else if (cmd == 5) blit(flags, true); else ++unsupported;
  }
  void fill(uint32_t flags) {
    (void)flags; Surf d = surface(0x60, 0x66); if (!d.ok) { ++unsupported; return; }
    const int x = sx12(reg[0x80] >> 12), y = sx12(reg[0x80]), w = sx12(reg[0x81] >> 12), h = sx12(reg[0x81]);
    const uint8_t c = color();
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) { const int px_ = x + i, py = y + j; if (px_ < 0 || py < 0 || px_ >= d.w || py >= d.h) { ++clipped; continue; } *px(d, px_, py) = c; }
  }
  void blit(uint32_t flags, bool stretch) {
    Surf s = surface(0x50, 0x56), d = surface(0x60, 0x66); if (!d.ok || !s.ok)   /* regs 0x50/51/56 = SOURCE, 0x60/61/66 = DESTINATION (positions only fit this way) */ { ++unsupported; return; }
    const int sxp = sx12(reg[0x90] >> 12), syp = sx12(reg[0x90]), sw = sx12(reg[0x91] >> 12), sh = sx12(reg[0x91]);
    const int dx = sx12(reg[0x92] >> 12), dy = sx12(reg[0x92]);
    const int dw = stretch ? sx12(reg[0x94] >> 12) : sw, dh = stretch ? sx12(reg[0x94]) : sh;
    const bool key = flags & 0x08; const uint8_t ck = uint8_t(reg[0x71] & 0xff);
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int j = 0; j < dh; ++j) for (int i = 0; i < dw; ++i) {
      const int ox = dx + i, oy = dy + j, ix = sxp + (stretch ? i * sw / dw : i), iy = syp + (stretch ? j * sh / dh : j);
      if (ox < 0 || oy < 0 || ox >= d.w || oy >= d.h || ix < 0 || iy < 0 || ix >= s.w || iy >= s.h) { ++clipped; continue; }
      const uint8_t p = *px(s, ix, iy); if (key && p == ck) continue; *px(d, ox, oy) = p;
    }
  }
};
