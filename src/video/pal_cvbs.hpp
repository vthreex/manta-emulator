#pragma once
// EXPLORATORY analog TV signal source: a PAL-B/G composite (CVBS) waveform generator and a separate, signal-only measurement front end.
//
// Everything here is derived from the published PAL system (ITU-R BT.470 / BT.1700 timing, EBU colour bars); nothing is a measurement of
// the Manta TV. The ADC model (resolution, input range, clamp) is an ASSUMPTION; the real analog front end of the MSD309 is not documented.
// The generator produces samples only; the measurement block derives what a hardware sync separator / burst detector would measure from
// those samples (it never looks at generator state), so a different waveform (noise, wrong standard, no burst) gives different results.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <numbers>
#include <vector>

namespace manta::cvbs {

struct PalTiming {
  static constexpr double sampleRateHz = 27.0e6;                 // 2 x 13.5 MHz (BT.601 luminance rate x 2); decoder ADC rate is assumed
  static constexpr double lineFrequencyHz = 15625.0;
  static constexpr unsigned samplesPerLine = 1728;               // 27 MHz / 15.625 kHz
  static constexpr unsigned linesPerFrame = 625;
  static constexpr double subcarrierHz = 4433618.75;             // 283.7516 cycles per line
  // times from the leading edge of line sync (0H), in microseconds
  static constexpr double syncUs = 4.7, burstStartUs = 5.6, burstUs = 2.25, activeStartUs = 10.5, activeUs = 52.0;
  static constexpr double equalizingUs = 2.35, broadUs = 27.3;
};

struct AdcModel {
  // ASSUMPTION: 10 bit, input -0.4 V .. +0.8 V (1.2 V span) so sync tip (-0.3 V) and 100% white (+0.7 V) both fit.
  unsigned bits = 10;
  double vMin = -0.4, vMax = 0.8;
  uint16_t code(double volts) const {
    const double span = vMax - vMin, full = double((1u << bits) - 1);
    double c = (volts - vMin) / span * full;
    if (c < 0) c = 0;
    if (c > full) c = full;
    return uint16_t(std::lround(c));
  }
  double volts(uint16_t c) const { return vMin + double(c) / double((1u << bits) - 1) * (vMax - vMin); }
};

struct Rgb { double r, g, b; };

// EBU 100/0/75/0 colour bars (gamma corrected values): white, yellow, cyan, green, magenta, red, blue, black.
inline Rgb ebuBar(unsigned i) {
  static const Rgb bars[8] = {{1, 1, 1}, {.75, .75, 0}, {0, .75, .75}, {0, .75, 0}, {.75, 0, .75}, {.75, 0, 0}, {0, 0, .75}, {0, 0, 0}};
  return bars[i & 7];
}

struct PalSource {
  AdcModel adc;
  double noiseVolts = 0;                // optional deterministic pseudo noise amplitude (peak), 0 = clean
  bool burst = true;                    // false = monochrome signal (no colour burst, no chroma)
  bool sync = true;                     // false = no signal energy at all (open input: flat blanking level)
  uint64_t seed = 1;

  // Frame = 1250 half-line slots of 864 samples (32 us). Vertical groups of 15 slots (5 equalizing, 5 broad, 5 equalizing pulses at half-line
  // spacing) start at slot 620 (line 311, on a line boundary) and slot 1245 (line 623.5, mid line); 625 slots apart -> 2:1 interlace.
  // Outside the groups every even slot starts a normal line with 4.7 us sync, burst and (on active lines) the colour bars.
  double voltsAt(uint64_t absoluteSample) const {
    constexpr uint64_t slotSamples = PalTiming::samplesPerLine / 2, frameSlots = 2ull * PalTiming::linesPerFrame;
    const uint64_t inFrame = absoluteSample % (frameSlots * slotSamples);
    const unsigned slot = unsigned(inFrame / slotSamples);
    const double tSlot = double(inFrame % slotSamples) / PalTiming::sampleRateHz * 1e6;      // us since the start of this slot
    const double tAbs = double(absoluteSample) / PalTiming::sampleRateHz;
    if (!sync) return noise(absoluteSample);
    const unsigned rel1 = (slot + frameSlots - 1245) % unsigned(frameSlots), rel2 = (slot + frameSlots - 620) % unsigned(frameSlots);
    const bool g1 = rel1 < 15, g2 = rel2 < 15;
    if (g1 || g2) {
      const unsigned k = g1 ? rel1 : rel2;
      const bool broad = k >= 5 && k < 10;
      const double width = broad ? PalTiming::broadUs : PalTiming::equalizingUs;
      return (tSlot < width ? -0.3 : 0.0) + noise(absoluteSample);
    }
    const unsigned line = slot / 2 + 1;
    const double t = tSlot + 32.0 * double(slot & 1);                                        // us since 0H of this line
    return blankingAt(t, tAbs, line, absoluteSample);
  }

private:
  double noise(uint64_t n) const {
    if (noiseVolts == 0) return 0;
    uint64_t x = n * 6364136223846793005ull + seed * 1442695040888963407ull;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33;
    return (double(x & 0xffff) / 32768.0 - 1.0) * noiseVolts;
  }
  double blankingAt(double t, double tAbs, unsigned line, uint64_t n) const {
    double v = 0;
    if (t < PalTiming::syncUs) return -0.3 + noise(n);                                    // line sync tip
    const bool pal2 = (line & 1) == 0;                                                    // V-switch phase per line
    const double w = 2.0 * std::numbers::pi * PalTiming::subcarrierHz;
    if (burst && t >= PalTiming::burstStartUs && t < PalTiming::burstStartUs + PalTiming::burstUs) {
      // burst: 10 cycles of subcarrier at +-135 degrees (swinging burst), 300 mV peak-to-peak (+-0.15 V)
      const double ph = pal2 ? -135.0 : 135.0;
      return v + 0.15 * std::sin(w * tAbs + ph * std::numbers::pi / 180.0) + noise(n);
    }
    const bool activeLine = (line >= 23 && line <= 310) || (line >= 336 && line <= 623);
    if (activeLine && t >= PalTiming::activeStartUs && t < PalTiming::activeStartUs + PalTiming::activeUs) {
      const unsigned bar = unsigned((t - PalTiming::activeStartUs) / PalTiming::activeUs * 8.0);
      const Rgb c = ebuBar(bar);
      const double y = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
      const double u = 0.493 * (c.b - y), vv = 0.877 * (c.r - y);
      // 0.7 V luminance range above blanking (0 % setup in PAL); chroma on the sub carrier with V switched every line
      double comp = 0.7 * y;
      if (burst) comp += 0.7 * (u * std::sin(w * tAbs) + (pal2 ? -vv : vv) * std::cos(w * tAbs));
      return comp + noise(n);
    }
    return v + noise(n);
  }
};

// ----------------------------------------------------------------------------------------------------------------------------------
// Measurement front end (sees only ADC codes).
struct Measurement {
  bool hsyncLocked = false;
  unsigned lineClocks = 0;              // average distance between sync leading edges (samples @ 27 MHz)
  unsigned hsyncWidthClocks = 0;
  double syncAmplitudeVolts = 0;        // blanking minus sync tip (tip level measured, blanking level measured on the back porch)
  bool vsyncSeen = false;
  unsigned linesPerFrame = 0;           // between two field-1 vertical syncs
  unsigned linesPerField = 0;           // between consecutive vertical syncs (rounded)
  double vsyncLinePhaseDelta = 0;       // fraction of a line between the vertical sync positions of consecutive fields (0.5 = interlaced)
  bool interlaced = false;              // vertical sync leading edge alternates between line start and mid line
  bool burstPresent = false;
  double burstPeakToPeakVolts = 0;
  bool palSwitch = false;               // burst phase alternates by 90 degrees line to line
  double burstPhaseDegrees = 0;         // phase of the burst of the last line relative to sin(2 pi fsc t)
};

inline Measurement measure(const PalSource& src, uint64_t startSample, uint64_t count) {
  using T = PalTiming;
  const AdcModel& adc = src.adc;
  Measurement m;
  // --- pass 1: slicer. Track tip (min) and blanking (mode near the top of the sync-containing histogram) with simple clamp logic. ---
  std::vector<uint16_t> s(count);
  uint16_t lo = 0xffff, hi = 0;
  for (uint64_t i = 0; i < count; ++i) {
    s[i] = adc.code(src.voltsAt(startSample + i));
    lo = std::min(lo, s[i]);
    hi = std::max(hi, s[i]);
  }
  // histogram with a +-8 code box filter: blanking is the global maximum; the sync tip is the maximum at least 0.1 V (in codes) below it
  std::vector<unsigned> hist(1u << adc.bits, 0), box(hist.size(), 0);
  for (auto c : s) ++hist[c];
  {
    std::vector<uint64_t> pre(hist.size() + 1, 0);
    for (size_t i = 0; i < hist.size(); ++i) pre[i + 1] = pre[i] + hist[i];
    for (size_t i = 0; i < hist.size(); ++i) {
      const size_t l = i >= 8 ? i - 8 : 0, r = std::min(hist.size(), i + 9);
      box[i] = unsigned(pre[r] - pre[l]);
    }
  }
  uint16_t blank = lo, tip = lo;
  unsigned best = 0;
  for (unsigned c = lo; c < box.size(); ++c)
    if (box[c] > best) { best = box[c]; blank = uint16_t(c); }
  const unsigned minGap = unsigned(0.1 / (adc.vMax - adc.vMin) * double((1u << adc.bits) - 1));
  best = 0;
  for (unsigned c = lo; c + minGap < blank; ++c)
    if (box[c] > best) { best = box[c]; tip = uint16_t(c); }
  m.syncAmplitudeVolts = adc.volts(blank) - adc.volts(tip);
  if (m.syncAmplitudeVolts < 0.1) return m;                 // no sync energy: unlocked
  const uint16_t slice = uint16_t((unsigned(tip) + unsigned(blank)) / 2);
  // --- pass 2: sync pulses: falling then rising crossings of the slice level ---
  struct Pulse { uint64_t start, width; };
  std::vector<Pulse> pulses;
  bool low = false;
  uint64_t startLow = 0;
  for (uint64_t i = 0; i < count; ++i) {
    const bool below = s[i] < slice;
    if (below && !low) { low = true; startLow = i; }
    if (!below && low) { low = false; pulses.push_back({startLow, i - startLow}); }
  }
  // line sync pulses are the "normal width" pulses (4.7 us +- 1.2 us); equalizing and broad ones differ
  std::vector<Pulse> normal;
  const double wNom = T::syncUs * 1e-6 * T::sampleRateHz;
  for (auto& p : pulses)
    if (double(p.width) > wNom * 0.75 && double(p.width) < wNom * 1.3) normal.push_back(p);
  if (normal.size() < 8) return m;
  // line period: median distance between consecutive normal pulses that are one line apart
  std::vector<uint64_t> d;
  for (size_t i = 1; i < normal.size(); ++i) {
    const uint64_t dist = normal[i].start - normal[i - 1].start;
    if (dist > T::samplesPerLine * 0.9 && dist < T::samplesPerLine * 1.1) d.push_back(dist);
  }
  if (d.size() < 8) return m;
  uint64_t sum = 0, sw = 0;
  for (auto v : d) sum += v;
  for (auto& p : normal) sw += p.width;
  m.lineClocks = unsigned(std::lround(double(sum) / double(d.size())));
  m.hsyncWidthClocks = unsigned(std::lround(double(sw) / double(normal.size())));
  m.hsyncLocked = true;
  // --- vertical sync: a broad pulse (>20 us low); vertical sync period = distance between the first broad pulse of each group ---
  std::vector<uint64_t> vstarts;
  uint64_t lastBroad = ~0ull;
  for (auto& p : pulses)
    if (double(p.width) > 20e-6 * T::sampleRateHz) {
      if (lastBroad == ~0ull || p.start - lastBroad > T::samplesPerLine * 3) vstarts.push_back(p.start);
      lastBroad = p.start;
    }
  // a vertical group that is cut by the start of the observation window would start at one of its later broad pulses: ignore groups that
  // begin within the first eight lines of the window
  vstarts.erase(std::remove_if(vstarts.begin(), vstarts.end(), [&](uint64_t v) { return v < uint64_t(m.lineClocks) * 8; }), vstarts.end());
  if (vstarts.size() >= 3) {
    m.vsyncSeen = true;
    const double fieldSamples = double(vstarts[2] - vstarts[0]) / 2.0;
    m.linesPerField = unsigned(std::lround(fieldSamples / double(m.lineClocks)));
    m.linesPerFrame = unsigned(std::lround(double(vstarts[2] - vstarts[0]) / double(m.lineClocks)));
    // interlace: the offset of consecutive vertical syncs inside the line alternates by half a line
    const auto phase = [&](uint64_t v) {
      const double ref = double(normal[0].start % m.lineClocks);
      double x = (double(v % m.lineClocks) - ref) / double(m.lineClocks);
      return x - std::floor(x);
    };
    const double dPh = std::fabs(phase(vstarts[1]) - phase(vstarts[0]));
    m.vsyncLinePhaseDelta = dPh;
    m.interlaced = std::fabs(dPh - 0.5) < 0.1;
  }
  // --- burst: correlate the samples 5.6..7.85 us after each normal sync start with sin/cos of the nominal subcarrier ---
  const double w = 2.0 * std::numbers::pi * T::subcarrierHz;
  std::vector<double> phases;
  double ampSum = 0;
  size_t ampN = 0;
  for (auto& p : normal) {
    const uint64_t a = p.start + uint64_t(T::burstStartUs * 1e-6 * T::sampleRateHz + 0.5) + 6;
    const uint64_t b = p.start + uint64_t((T::burstStartUs + T::burstUs) * 1e-6 * T::sampleRateHz) - 6;
    if (b + 1 >= count) break;
    double sc = 0, cc = 0;
    size_t n = 0;
    for (uint64_t i = a; i < b; ++i) {
      const double t = double(startSample + i) / T::sampleRateHz;
      const double v = adc.volts(s[i]) - adc.volts(blank);
      sc += v * std::sin(w * t);
      cc += v * std::cos(w * t);
      ++n;
    }
    if (!n) continue;
    const double amp = 2.0 * std::hypot(sc, cc) / double(n);                              // peak amplitude
    ampSum += amp;
    ++ampN;
    phases.push_back(std::atan2(cc, sc) * 180.0 / std::numbers::pi);
  }
  if (ampN) {
    const double peak = ampSum / double(ampN);
    m.burstPeakToPeakVolts = 2.0 * peak;
    m.burstPresent = m.burstPeakToPeakVolts > 0.1;
    if (m.burstPresent && phases.size() >= 4) {
      auto wrap = [](double a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; };
      double dsum = 0;
      for (size_t i = 1; i < phases.size(); ++i) dsum += std::fabs(wrap(phases[i] - phases[i - 1]));
      const double avg = dsum / double(phases.size() - 1);
      m.palSwitch = avg > 45.0 && avg < 135.0;           // PAL: the burst phase swings by 90 degrees line to line
      m.burstPhaseDegrees = phases.back();
    }
  }
  return m;
}

// ----------------------------------------------------------------------------------------------------------------------------------
// Software PAL decoder (EXPLORATORY model of the analog video decoder block): composite samples -> 720x576 RGB.
// Separates luma and chroma with a box filter one subcarrier period wide, demodulates chroma with a reference locked to the measured burst
// phase (the burst is measured on every line, which also resolves the PAL V-switch), low-pass filters U/V and converts with BT.601. No delay line
// (simple PAL), no comb filter, no AGC: levels follow from the ADC model assumptions. Not a model of the MSD309's decoder, only of what a PAL decoder does.
struct DecodedFrame {
  unsigned width = 0, height = 0;
  std::vector<uint32_t> bgra;   // 0xAARRGGBB
};

inline DecodedFrame decodePalFrame(const PalSource& src, uint64_t startSample) {
  using T = PalTiming;
  const AdcModel& adc = src.adc;
  DecodedFrame f;
  f.width = 720;
  f.height = 576;
  f.bgra.assign(size_t(f.width) * f.height, 0xff000000u);
  const double w = 2.0 * std::numbers::pi * T::subcarrierHz;
  const unsigned spl = T::samplesPerLine;
  const unsigned burstA = unsigned(T::burstStartUs * 1e-6 * T::sampleRateHz + 0.5) + 6, burstB = unsigned((T::burstStartUs + T::burstUs) * 1e-6 * T::sampleRateHz) - 6;
  const unsigned activeA = unsigned(T::activeStartUs * 1e-6 * T::sampleRateHz), activeN = unsigned(T::activeUs * 1e-6 * T::sampleRateHz);
  const unsigned boxN = unsigned(std::lround(T::sampleRateHz / T::subcarrierHz));   // 6 samples ~ one subcarrier period
  std::vector<double> line(spl);
  // field 1 shows lines 23..310 (288), field 2 lines 336..623 (288): interlaced into 576 rows
  for (unsigned row = 0; row < 576; ++row) {
    const unsigned fieldIndex = row & 1, k = row / 2;
    const unsigned lineNo = fieldIndex == 0 ? 23 + k : 336 + k;
    const uint64_t lineStart = startSample + uint64_t(lineNo - 1) * spl;
    for (unsigned x = 0; x < spl; ++x) line[x] = adc.volts(adc.code(src.voltsAt(lineStart + x)));
    // blanking level from the back porch just before the burst, burst phase relative to the nominal reference
    double sc = 0, cc = 0, bl = 0;
    unsigned nb = 0;
    for (unsigned x = burstA; x < burstB; ++x) {
      const double t = double(lineStart + x) / T::sampleRateHz;
      sc += line[x] * std::sin(w * t);
      cc += line[x] * std::cos(w * t);
      bl += line[x];
      ++nb;
    }
    const double burstPhase = std::atan2(cc, sc);                 // burst = A sin(wt + phi): phi = atan2(cc, sc)
    // swing: burst phase is +135 (V positive lines) or -135 degrees (V negative lines) relative to the U axis
    const bool vNeg = std::fabs(burstPhase - (-135.0 * std::numbers::pi / 180.0)) < std::numbers::pi / 4 ||
                      std::fabs(burstPhase - (225.0 * std::numbers::pi / 180.0)) < std::numbers::pi / 4;
    (void)bl;
    const double blank = adc.volts(adc.code(0.0));
    std::vector<double> y(activeN), u(activeN), v(activeN);
    std::vector<double> cU(spl, 0.0), cV(spl, 0.0), lum(spl, 0.0);
    for (unsigned x = 0; x < spl; ++x) {
      const double t = double(lineStart + x) / T::sampleRateHz;
      cU[x] = line[x] * std::sin(w * t);
      cV[x] = line[x] * std::cos(w * t);
    }
    auto box = [&](const std::vector<double>& in, unsigned x, unsigned n) {
      double a = 0;
      for (unsigned i = 0; i < n; ++i) { const unsigned xx = std::min(spl - 1, x + i - n / 2); a += in[xx]; }
      return a / n;
    };
    for (unsigned i = 0; i < activeN; ++i) {
      const unsigned x = activeA + i;
      lum[x] = box(line, x, boxN);
      // low-pass the demodulated chroma over two subcarrier periods
      u[i] = 2.0 * box(cU, x, 2 * boxN) / 0.493;
      v[i] = 2.0 * box(cV, x, 2 * boxN) / 0.877 * (vNeg ? -1.0 : 1.0) / 1.0;
      y[i] = (lum[x] - blank) / 0.7;
    }
    for (unsigned px = 0; px < f.width; ++px) {
      const unsigned i = std::min(activeN - 1, unsigned(uint64_t(px) * activeN / f.width));
      const double Y = y[i];
      const double Uv = u[i] * 0.493 / 0.7 * 1.0 / 1.0, Vv = v[i] * 0.877 / 0.7 * 1.0 / 1.0;
      // colour difference scale: chroma amplitude was encoded as 0.7*U, 0.7*V of the same scale; u,v above divide by 0.493/0.877, undo and normalise by 0.7
      double R = Y + 1.140 * Vv, G = Y - 0.395 * Uv - 0.581 * Vv, B = Y + 2.032 * Uv;
      auto q = [](double d) { return uint32_t(d < 0 ? 0 : d > 1 ? 255 : d * 255.0 + 0.5); };
      f.bgra[size_t(row) * f.width + px] = 0xff000000u | (q(R) << 16) | (q(G) << 8) | q(B);
    }
  }
  return f;
}

}  // namespace manta::cvbs
