#pragma once
// EXPLORATORY HDMI / DVI input signal: standard video timings (CEA-861 and VESA DMT) turned into what the MSD309 scaler's IP1 mode detector
// would measure for the main window. This is a generated input, not a measurement of real hardware.
//
// What the original firmware reads (gatherers 805F2EA0 and 805F2FE0, scaler bank 1):
//   reg 0x20 (5E80) & 0x3FFF  H period: crystal clocks per line. The firmware computes H freq x10 = (XTAL / Hperiod) / 100 (805F1B94) and
//                             V freq x10 = (H freq x10 * 1000) / V total (805F1BD8). XTAL = 12 MHz (ASSUMPTION; the firmware then reports 60 Hz
//                             for 1080p60, i.e. its own arithmetic agrees).
//   reg 0x1F (5E7C) & 0x1FFF  V total: lines per frame (also for interlaced modes, see hdmiModeTiming).
//   reg 0x1E high byte (5E79) bit0 / bit1 sync polarity (which bit is H and which sense is positive is an ASSUMPTION), bit3 interlace.
//   regs 0x12..0x15           V start, H start, V end, H end of the active (data-enable) window; width = Hend - Hstart + 1.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace manta {

struct VideoMode {
  const char* name;      // e.g. "1920x1080@60"
  unsigned width, height;
  double pixelClockHz;
  unsigned htotal, vtotal;   // full frame (interlaced: total lines of both fields)
  unsigned hstart, vstart;   // sync width + back porch (first active pixel / line), per field for interlaced
  bool hpos, vpos, interlaced;
};

inline const VideoMode* videoModes(size_t& n) {
  static const VideoMode m[] = {
      {"1920x1080@60", 1920, 1080, 148.5e6, 2200, 1125, 192, 41, true, true, false},
      {"1920x1080@50", 1920, 1080, 148.5e6, 2640, 1125, 192, 41, true, true, false},
      {"1920x1080@30", 1920, 1080, 74.25e6, 2200, 1125, 192, 41, true, true, false},
      {"1920x1080@25", 1920, 1080, 74.25e6, 2640, 1125, 192, 41, true, true, false},
      {"1920x1080@24", 1920, 1080, 74.25e6, 2750, 1125, 192, 41, true, true, false},
      {"1920x1080i@60", 1920, 1080, 74.25e6, 2200, 1125, 192, 20, true, true, true},
      {"1920x1080i@50", 1920, 1080, 74.25e6, 2640, 1125, 192, 20, true, true, true},
      {"1280x720@60", 1280, 720, 74.25e6, 1650, 750, 260, 25, true, true, false},
      {"1280x720@50", 1280, 720, 74.25e6, 1980, 750, 260, 25, true, true, false},
      {"1360x768@60", 1360, 768, 85.5e6, 1792, 795, 368, 24, true, true, false},
      {"1024x768@60", 1024, 768, 65.0e6, 1344, 806, 296, 35, false, false, false},
      {"800x600@60", 800, 600, 40.0e6, 1056, 628, 216, 27, true, true, false},
      {"720x576@50", 720, 576, 27.0e6, 864, 625, 132, 44, false, false, false},
      {"720x480@60", 720, 480, 27.0e6, 858, 525, 122, 36, false, false, false},
      {"640x480@60", 640, 480, 25.175e6, 800, 525, 144, 35, false, false, false},
  };
  n = sizeof m / sizeof m[0];
  return m;
}

inline std::string hdmiModeList() {
  size_t n = 0; auto* m = videoModes(n); std::string s;
  for (size_t i = 0; i < n; ++i) { if (i) s += ", "; s += m[i].name; }
  return s;
}

inline const VideoMode* findVideoMode(const std::string& name) {
  size_t n = 0; auto* m = videoModes(n);
  for (size_t i = 0; i < n; ++i) if (name == m[i].name) return &m[i];
  return nullptr;
}

// HPERIOD, VTOTAL, SYNC, VSTART, HSTART, VEND, HEND as the --exploratory-input-timing fields.
inline std::optional<std::array<uint16_t, 7>> hdmiModeTiming(const std::string& name, double xtalHz = 12e6) {
  const VideoMode* v = findVideoMode(name);
  if (!v) return std::nullopt;
  const double lineHz = v->pixelClockHz / v->htotal;
  // Interlaced: the firmware names the mode (1080i@60Hz in its INFO banner) only with V total = lines of the whole frame and the active window
  // covering all lines of both fields, plus the interlace bit; per-field values give 1920x540 (tested).
  const unsigned fieldLines = v->vtotal;
  const unsigned activeLines = v->height;
  std::array<uint16_t, 7> t{};
  t[0] = uint16_t(std::lround(xtalHz / lineHz));
  t[1] = uint16_t(fieldLines);
  t[2] = uint16_t((v->vpos ? 1u : 0u) | (v->hpos ? 2u : 0u) | (v->interlaced ? 8u : 0u));
  t[3] = uint16_t(v->vstart);
  t[4] = uint16_t(v->hstart);
  t[5] = uint16_t(v->vstart + activeLines - 1);
  t[6] = uint16_t(v->hstart + v->width - 1);
  return t;
}

}  // namespace manta
