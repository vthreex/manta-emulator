#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include "input/remote_queue.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
namespace manta {
// DIAGNOSTIC GOP PREVIEW. Composes the guest memory of ENABLED + ACTIVE-LATCHED GOP windows into one BGRA canvas.
// It draws nothing of its own: every opaque canvas pixel is a guest framebuffer pixel. It is NOT panel scan-out:
// mixer/output routing, window priority, scaler, constant/per-pixel alpha, gamma and panel timing are not modeled.
// Hardware format 6 is decoded as ARGB1555 by INFERENCE (the original GE clears the surface with destination
// format C = ARGB1555_DST); no independent Titania4 GOP format table exists in the repository.
struct GopLayer {
  unsigned bank = 0, window = 0;
  uint32_t x = 0, y = 0, width = 0, height = 0, pitch = 0;
  unsigned format = 0;
  uint32_t physical = 0;
  std::vector<uint8_t> memory;   // private copy of pitch*height guest bytes (captured by the emulator thread)
  uint64_t invalidBytes = 0;     // bytes whose guest value is unknown
  std::string skipReason;        // non-empty: not composed
  std::array<std::optional<uint32_t>,256> palette{}; // mode-3000 guest ARGB entries only
  bool indexedOpaque = false; // original GWIN blend disabled, coefficient 3F
};
struct GopPreviewFrame {
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> bgra;    // 0xAARRGGBB; alpha 0 = guest pixel with ARGB1555 alpha bit clear / no layer
  std::vector<uint8_t> covered;  // 1 where a guest pixel was composed
  std::string label;             // always names the mode
  std::string description;       // layer geometry/format/checksum summary
  // EXPLORATORY presentation only (--exploratory-video-plane): colour shown where no guest graphics pixel exists. The emulator has no video
  // plane; when this is inactive the presenter shows the neutral checker. Never part of the checksum.
  std::vector<uint32_t> planeBgra;   // EXPLORATORY MVOP video plane (same size as bgra), 0xAARRGGBB; empty = none
  bool planeActive = false;
  uint32_t planeRgb = 0;         // 0x00RRGGBB
  uint64_t checksum = 0;         // FNV-1a over the BGRA words (deterministic)
  uint64_t sequence = 0;
  bool empty() const { return !width || !height; }
};
inline uint32_t argb1555ToBgra(uint16_t p) {
  auto expand = [](uint32_t v) { return (v << 3) | (v >> 2); };
  return (p & 0x8000 ? 0xff000000u : 0u) | (expand((p >> 10) & 31) << 16) | (expand((p >> 5) & 31) << 8) | expand(p & 31);
}
// Layers are composed in (bank, window) ascending order: hardware priority is NOT established.
// Opaque pixels (alpha bit set) replace what is below; clear-alpha pixels leave it unchanged.
// blackKey: EXPLORATORY HYPOTHESIS (not Titania4-verified): an opaque pure-black pixel (RGB 0) is treated like a clear-alpha pixel, i.e. a GOP
// transparent-colour key on black. The original ZUI clears its surfaces to alpha FF black; nothing in the firmware shows how the GOP treats that.
inline GopPreviewFrame composeGopPreview(std::vector<GopLayer>& layers, uint32_t maxWidth = 1920, uint32_t maxHeight = 1080, bool blackKey = false, bool blackKeyEdgeOnly = false) {
  GopPreviewFrame f;
  f.label = "DIAGNOSTIC GOP PREVIEW - guest framebuffer pixels of enabled GOP windows; not verified panel scan-out";
  std::stable_sort(layers.begin(), layers.end(), [](const GopLayer& a, const GopLayer& b) {
    return a.bank != b.bank ? a.bank < b.bank : a.window < b.window; });
  uint32_t w = 0, h = 0;
  for (auto& l : layers) {
    if (l.skipReason.empty()) {
      if (l.format != 6 && l.format != 4) l.skipReason = "format not decoded (only 6, inferred ARGB1555)";
      else if (l.invalidBytes) l.skipReason = "window memory contains unknown bytes";
      else if (!l.width || !l.height || uint64_t(l.pitch) < uint64_t(l.width) * (l.format==4?1:2) || uint64_t(l.pitch) * l.height > l.memory.size())
        l.skipReason = "geometry exceeds captured memory";
      else if (l.x >= maxWidth || l.y >= maxHeight) l.skipReason = "outside the panel canvas";
      if(l.skipReason.empty() && l.format==4)
        for(uint32_t y=0;y<l.height && l.skipReason.empty();++y)
          for(uint32_t x=0;x<l.width;++x) if(!l.palette[l.memory[size_t(y)*l.pitch+x]]) {
            l.skipReason="indexed window references an unknown or unsupported palette entry";break;
          }
    }
    if (l.skipReason.empty()) {
      w = std::max(w, std::min(l.x + l.width, maxWidth));
      h = std::max(h, std::min(l.y + l.height, maxHeight));
    }
  }
  f.width = w; f.height = h;
  f.bgra.assign(size_t(w) * h, 0u);
  f.covered.assign(size_t(w) * h, 0);
  for (auto& l : layers) {
    f.description += "bank" + std::to_string(l.bank) + "/win" + std::to_string(l.window) + " " + std::to_string(l.width) + "x" +
                     std::to_string(l.height) + "@(" + std::to_string(l.x) + "," + std::to_string(l.y) + ") fmt" + std::to_string(l.format) +
                     (l.skipReason.empty() ? " composed; " : " SKIPPED: " + l.skipReason + "; ");
    if (!l.skipReason.empty()) continue;
    for (uint32_t yy = 0; yy < l.height && l.y + yy < h; ++yy)
      for (uint32_t xx = 0; xx < l.width && l.x + xx < w; ++xx) {
        size_t src = size_t(yy) * l.pitch + size_t(xx) * (l.format==4?1:2);
        uint32_t color;
        if(l.format==4) {color=*l.palette[l.memory[src]];if(l.indexedOpaque)color|=0xff000000u;}
        else color=argb1555ToBgra(uint16_t(l.memory[src] | (l.memory[src+1]<<8)));
        if (!(color & 0xff000000u) || (blackKey && !blackKeyEdgeOnly && !(color & 0xffffffu))) continue;
        size_t dst = size_t(l.y + yy) * w + l.x + xx;
        f.bgra[dst] = color; f.covered[dst] = 1;
      }
  }
  // blackKeyEdgeOnly (EXPLORATORY HYPOTHESIS): opaque pure-black pixels are see-through only where they connect (4-neighbourhood, through black or uncovered
  // pixels) to the frame border, i.e. the cleared background around the OSD. Black that is enclosed by drawn pixels (menu panel interiors) stays opaque.
  if (blackKey && blackKeyEdgeOnly && w && h) {
    auto passable = [&](size_t i) { return !f.covered[i] || !(f.bgra[i] & 0x00ffffffu); };
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<uint32_t> stack;
    auto push = [&](uint32_t x, uint32_t y) { size_t i = size_t(y) * w + x; if (!seen[i] && passable(i)) { seen[i] = 1; stack.push_back(uint32_t(i)); } };
    for (uint32_t x = 0; x < w; ++x) { push(x, 0); push(x, h - 1); }
    for (uint32_t y = 0; y < h; ++y) { push(0, y); push(w - 1, y); }
    while (!stack.empty()) {
      uint32_t i = stack.back(); stack.pop_back();
      uint32_t x = i % w, y = i / w;
      f.covered[i] = 0;
      if (x > 0) push(x - 1, y);
      if (x + 1 < w) push(x + 1, y);
      if (y > 0) push(x, y - 1);
      if (y + 1 < h) push(x, y + 1);
    }
  }
  uint64_t hash = 1469598103934665603ull;
  for (auto v : f.bgra) { hash ^= v; hash *= 1099511628211ull; }
  f.checksum = hash;
  return f;
}
// Top-down 32-bit BGRA BMP of a composed preview frame (uncovered pixels are fully transparent black). Host-side export only.
inline std::vector<uint8_t> previewFrameToBmp32(const GopPreviewFrame& f) {
  const uint32_t imageBytes = f.width * f.height * 4, header = 54;
  std::vector<uint8_t> out(header + imageBytes, 0);
  auto put16 = [&](size_t at, uint32_t v) { out[at] = uint8_t(v); out[at + 1] = uint8_t(v >> 8); };
  auto put32 = [&](size_t at, uint32_t v) { put16(at, v & 0xffff); put16(at + 2, v >> 16); };
  out[0] = 'B'; out[1] = 'M'; put32(2, header + imageBytes); put32(10, header); put32(14, 40);
  put32(18, f.width); put32(22, uint32_t(-int32_t(f.height))); put16(26, 1); put16(28, 32); put32(34, imageBytes);
  for (size_t i = 0; i < f.bgra.size(); ++i) {
    uint32_t v = f.covered[i] ? f.bgra[i] : (!f.planeBgra.empty() ? f.planeBgra[i] : f.planeActive ? (0xff000000u | f.planeRgb) : 0u);
    out[header + i * 4] = uint8_t(v); out[header + i * 4 + 1] = uint8_t(v >> 8); out[header + i * 4 + 2] = uint8_t(v >> 16); out[header + i * 4 + 3] = uint8_t(v >> 24);
  }
  return out;
}
// Thread-safe hand-off from the emulator thread (owner of guest RAM) to the native window.
class GopPreviewStore {
public:
  std::shared_ptr<RemoteQueue> remote = std::make_shared<RemoteQueue>();   // host key presses (function names) for the emulator thread
  void publish(GopPreviewFrame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame.sequence = ++sequence_;
    frame_ = std::make_shared<const GopPreviewFrame>(std::move(frame));
  }
  void publishOffscreen(GopPreviewFrame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame.sequence = ++offscreenSequence_;
    offscreen_ = std::make_shared<const GopPreviewFrame>(std::move(frame));
  }
  std::shared_ptr<const GopPreviewFrame> latestOffscreen() const { std::lock_guard<std::mutex> lock(mutex_); return offscreen_; }
  void setFinished() { finished_ = true; }
  bool finished() const { return finished_; }
  void setGuestStatus(std::string text) { std::lock_guard<std::mutex> lock(mutex_); status_ = std::move(text); }
  std::shared_ptr<const GopPreviewFrame> latest() const { std::lock_guard<std::mutex> lock(mutex_); return frame_; }
  std::string status() const { std::lock_guard<std::mutex> lock(mutex_); return status_; }
private:
  mutable std::mutex mutex_;
  std::shared_ptr<const GopPreviewFrame> frame_, offscreen_;
  uint64_t offscreenSequence_ = 0;
  std::string status_;
  uint64_t sequence_ = 0;
  std::atomic<bool> finished_{false};
};
}
