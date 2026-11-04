#pragma once
// EXPLORATORY input picture for the generated HDMI/DVI signal: an image file (uncompressed 24/32-bit BMP) or a built-in generated picture,
// resampled to the input mode's active size. It stands for the video a source device would send; it is shown behind the OSD only where and when
// the original firmware has the main window un-muted (see Msd309Machine::composeInputPicture). Not decoded video, not verified panel output.
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace manta {

struct InputPicture {
  unsigned width = 0, height = 0;
  std::vector<uint32_t> bgra;   // 0xFFRRGGBB
  bool empty() const { return bgra.empty(); }
};

inline InputPicture loadBmpPicture(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open input picture " + path);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u16 = [&](size_t o) { return unsigned(d.at(o) | d.at(o + 1) << 8); };
  auto u32 = [&](size_t o) { return uint32_t(d.at(o) | d.at(o + 1) << 8 | d.at(o + 2) << 16 | uint32_t(d.at(o + 3)) << 24); };
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') throw std::runtime_error("input picture is not a BMP: " + path);
  const uint32_t off = u32(10);
  const int32_t w = int32_t(u32(18)), h = int32_t(u32(22));
  const unsigned bpp = u16(28), comp = u32(30);
  if (w <= 0 || h == 0 || w > 8192 || std::abs(h) > 8192 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3))
    throw std::runtime_error("input picture must be an uncompressed 24- or 32-bit BMP: " + path);
  InputPicture p; p.width = unsigned(w); p.height = unsigned(std::abs(h)); p.bgra.resize(size_t(p.width) * p.height);
  const size_t stride = (size_t(p.width) * (bpp / 8) + 3) & ~size_t(3);
  for (unsigned y = 0; y < p.height; ++y) {
    const size_t row = off + stride * (h > 0 ? p.height - 1 - y : y);
    for (unsigned x = 0; x < p.width; ++x) {
      const size_t q = row + size_t(x) * (bpp / 8);
      p.bgra[size_t(y) * p.width + x] = 0xff000000u | uint32_t(d.at(q + 2)) << 16 | uint32_t(d.at(q + 1)) << 8 | d.at(q);
    }
  }
  return p;
}

// Built-in picture when no file is given: a calm generated scene (sky, sun, hills, water) with a thin colour-bar strip, so scaling, cropping
// and the firmware's colour settings are easy to see. Deterministic; no external image.
inline InputPicture builtinPicture(unsigned w = 1920, unsigned h = 1080) {
  InputPicture p; p.width = w; p.height = h; p.bgra.resize(size_t(w) * h);
  auto put = [&](unsigned x, unsigned y, double r, double g, double b) {
    auto c = [](double v) { return uint32_t(v < 0 ? 0 : v > 255 ? 255 : v + 0.5); };
    p.bgra[size_t(y) * w + x] = 0xff000000u | c(r) << 16 | c(g) << 8 | c(b);
  };
  const double sunX = 0.70 * w, sunY = 0.34 * h, sunR = 0.07 * h;
  for (unsigned y = 0; y < h; ++y) {
    const double fy = double(y) / h;
    for (unsigned x = 0; x < w; ++x) {
      const double fx = double(x) / w;
      double r, g, b;
      const double hill1 = 0.62 + 0.05 * std::sin(fx * 7.0) + 0.03 * std::sin(fx * 17.0 + 1.3);
      const double hill2 = 0.70 + 0.04 * std::sin(fx * 5.0 + 2.0) + 0.02 * std::sin(fx * 23.0);
      if (fy < hill1) {   // sky: deep blue to warm horizon
        const double t = fy / hill1;
        r = 40 + 190 * t * t; g = 90 + 120 * t; b = 200 - 40 * t;
        const double dx = x - sunX, dy = y - sunY, dist = std::sqrt(dx * dx + dy * dy);
        if (dist < sunR) { r = 255; g = 236; b = 170; }
        else if (dist < sunR * 3) { const double k = 1 - (dist - sunR) / (sunR * 2); r += 60 * k * k; g += 40 * k * k; b += 10 * k * k; }
      } else if (fy < hill2) { r = 70 + 40 * fy; g = 110 + 30 * std::sin(fx * 40.0) * 0.3; b = 70; }      // far hills
      else if (fy < 0.86) { const double t = (fy - hill2) / (0.86 - hill2); r = 40 + 30 * t; g = 120 + 40 * t; b = 50; }   // meadow
      else { const double t = (fy - 0.86) / 0.14; r = 30 + 20 * t; g = 70 + 30 * t; b = 120 + 60 * t + 10 * std::sin(fx * 90.0 + fy * 40.0); }   // water
      if (fy > 0.965) {   // colour bars: white yellow cyan green magenta red blue black
        static const uint32_t bars[8] = {0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000};
        const uint32_t c = bars[std::min(7u, unsigned(fx * 8))];
        r = double(c >> 16 & 255); g = double(c >> 8 & 255); b = double(c & 255);
      }
      put(x, y, r, g, b);
    }
  }
  return p;
}

}  // namespace manta
