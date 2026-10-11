#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "peripherals/gpio_i2c.hpp"
namespace manta {
struct RiuByte {
  uint8_t value = 0xa5, known = 0;
};
class Riu {
  std::map<uint32_t, RiuByte> bytes_;
  uint64_t bdmaComplete_ = 0;
  bool bdmaPending_ = false;
  std::map<uint32_t, uint32_t> pendingRmw_;
  bool uartEnabled_ = false;
  GpioI2c gpio_;
  unsigned miuSequence_[2]{};
  uint64_t miuReady_[2]{};
  bool miuArmed_[2]{};
  uint64_t bistReady_ = 0;
  uint32_t bistResult_ = 0;
  bool bistPending_ = false;
  uint64_t uartReady_ = 0;
  uint32_t get(uint32_t a, unsigned size) const;
  void put(uint32_t a, unsigned size, uint32_t v);

public:
  void prepareRmw(uint32_t a, uint32_t definedBits) { pendingRmw_[a] = definedBits; }
  uint32_t knownBits(uint32_t a, unsigned size) const;
  bool symbolicInit = false;
  bool candidateBdma = false;
  bool gpioI2c = false;
  bool candidateMiu = false;
  void resetMiuModel() {
    // Inferred from stock BIST0 preceding any selector write, followed by
    // explicit bit15=1 selection in BIST1. Only this bit is defined.
    bytes_[0x1f2025e1] = {0x25, 0x80};
  }
  void holdI2cSda(bool enabled) { gpio_.driveLow(0x1f203d05, 4, enabled); }
  void enableCandidateUart() {
    uartEnabled_ = true;
    put(0x1f201318, 1, 0);
  }
  std::string stopReason, uart;
  std::function<void(const std::string &, uint32_t, unsigned, uint32_t, const std::string &)> event;
  std::function<std::vector<uint8_t>(uint32_t, size_t)> flashRead;
  std::function<bool(uint32_t, size_t, uint16_t)> dramTest;
  uint32_t read(uint32_t a, unsigned size, uint32_t pc, uint32_t mask, uint64_t tick);
  void write(uint32_t a, unsigned size, uint32_t value, uint32_t pc, uint32_t mask, uint64_t tick);
  static uint32_t crcMsb(const std::vector<uint8_t> &data, uint32_t polynomial, uint32_t seed);
};
} // namespace manta
