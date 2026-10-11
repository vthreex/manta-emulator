#pragma once
#include <cstdint>
namespace manta {
// Functional self-calibration handshake only. Stock 804F1048..109C programs
// CA_START, delays, clears it and polls CA_END. The analogue result and power
// good bits remain unknown. Delay threshold/pacing is explicitly provisional.
class UtmiCalibration {
  bool clockReset_ = false, clockEnabled_ = false, started_ = false, released_ = false;
  uint64_t start_ = 0, ready_ = 0;

public:
  void clock(uint8_t value) {
    if (value == 0x0a) {
      clockReset_ = true;
      clockEnabled_ = false;
      started_ = released_ = false;
    } else if (value == 0x28 && clockReset_)
      clockEnabled_ = true;
    else {
      clockReset_ = clockEnabled_ = started_ = released_ = false;
    }
  }
  void command(bool start, uint64_t tick) {
    if (start) {
      started_ = clockEnabled_;
      released_ = false;
      start_ = tick;
    } else if (started_) {
      released_ = clockEnabled_ && tick - start_ >= 312000;
      ready_ = tick + 64;
      started_ = false;
    }
  }
  bool done(uint64_t tick) const { return released_ && clockEnabled_ && tick >= ready_; }
};
} // namespace manta
