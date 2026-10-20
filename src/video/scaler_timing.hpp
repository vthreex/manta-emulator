#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace manta {
struct PanelTiming {
  uint16_t htotal=0, vtotal=0, hstart=0, hend=0, vstart=0, vend=0;
  uint16_t vsyncStart=0, vsyncEnd=0;
  bool invert=false;
  bool operator==(const PanelTiming &) const = default;
};
// Digital output-sync experiment. Register geometry comes from stock
// 805E5AB8; clock rate and the relation of the bank-1 monitor to polarity are
// explicit assumptions. Never active in strict mode. One dispatch is one
// tick of the existing 312 MHz virtual timebase, not a physical CPU estimate.
class ScalerTiming {
  friend void ioClass(snap::Ar&, ScalerTiming&);
  std::optional<PanelTiming> timing_;
  uint64_t origin_=0, hz_=0;
  bool enabled_=false;
public:
  void clock(uint64_t hz) {
    if (hz < 1000000 || hz > 400000000) throw std::runtime_error("Exploratory display clock out of range");
    hz_=hz;
  }
  bool program(const PanelTiming &t, bool enabled, uint64_t tick) {
    if (!hz_ || !t.htotal || !t.vtotal || t.hend<t.hstart || t.vend<t.vstart ||
        t.hend>=t.htotal || t.vend>=t.vtotal || t.vsyncStart>t.vsyncEnd || t.vsyncEnd>=t.vtotal)
      { timing_.reset(); enabled_=false; return false; }
    bool changed=!timing_ || *timing_!=t || enabled_!=enabled;
    if (changed) { timing_=t; enabled_=enabled; origin_=tick; }
    return changed;
  }
  bool configured() const { return timing_.has_value(); }
  void invalidate() { timing_.reset(); enabled_=false; }
  const auto &timing() const { return timing_; }
  uint64_t pixels(uint64_t tick) const {
    auto elapsed=tick-origin_;
    // Avoid overflowing elapsed*hz for long deterministic runs.
    return (elapsed/312000000)*hz_ + (elapsed%312000000)*hz_/312000000;
  }
  uint64_t frames(uint64_t tick) const {
    return timing_ && enabled_ ? pixels(tick)/(uint64_t(timing_->htotal)*timing_->vtotal) : 0;
  }
  std::optional<bool> outputVsync(uint64_t tick) const {
    if (!timing_ || !enabled_) return {}; // clock-off monitor level unverified
    auto line=(pixels(tick)/timing_->htotal)%timing_->vtotal;
    bool pulse=line>=timing_->vsyncStart && line<=timing_->vsyncEnd;
    return pulse ^ timing_->invert;
  }
};
}
