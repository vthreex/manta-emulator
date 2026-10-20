#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <optional>
#include <string>
namespace manta {
// Register offsets and command bits corroborated by the stock 805482E4,
// 80548380 and 8054841C routines. Clock pacing is an explicit functional
// approximation: one guest dispatch = one tick of a 312 MHz virtual clock.
class Rtc {
  friend void ioClass(snap::Ar&, Rtc&);
  uint64_t lastTick_ = 0, oscillatorFraction_ = 0, dividerFraction_ = 0;
  uint32_t counter_ = 0, snapshot_ = 0;
  uint8_t control_ = 0;
  bool snapshotValid_ = false;

public:
  std::string error;
  void advance(uint64_t tick, uint32_t divider) {
    auto elapsed = tick - lastTick_;
    lastTick_ = tick;
    if (!(control_ & 1) || !(control_ & 2) || !divider)
      return;
    auto clocks = elapsed * 12000000 + oscillatorFraction_;
    oscillatorFraction_ = clocks % 312000000;
    auto sub = clocks / 312000000 + dividerFraction_;
    counter_ += uint32_t(sub / divider);
    dividerFraction_ = sub % divider;
  }
  uint8_t command(uint8_t value, uint64_t tick, uint32_t divider, bool dividerKnown, uint32_t load,
                  bool loadKnown) {
    advance(tick, dividerKnown ? divider : 0);
    if (!(value & 1)) {
      counter_ = 0;
      dividerFraction_ = oscillatorFraction_ = 0;
      snapshotValid_ = false;
    }
    if ((value & 2) && (!dividerKnown || !divider))
      error = "RTC enabled without a guest-programmed nonzero divider";
    if (value & 8) {
      if (!loadKnown)
        error = "RTC load command consumes an unknown load value";
      else {
        counter_ = load;
        dividerFraction_ = oscillatorFraction_ = 0;
      }
    }
    if (value & 16) {
      snapshot_ = counter_;
      snapshotValid_ = true;
    }
    // Snapshot/load strobes complete synchronously in this functional model.
    // Interrupt clear is a strobe. Alarm/IRQ delivery is not implemented.
    control_ = value & uint8_t(~(8 | 16 | 128));
    return control_;
  }
  std::optional<uint16_t> snapshot(bool high) const {
    if (!snapshotValid_)
      return std::nullopt;
    return uint16_t(snapshot_ >> (high ? 16 : 0));
  }
};
} // namespace manta
