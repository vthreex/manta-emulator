#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
namespace manta {
// Recovered from stock 805E55D4: channel command bits 5/6/7, 10-bit
// address, 12-bit samples, busy until the programmed RAM write completes.
// This is gamma RAM, not an OSD renderer. Deadline is provisional virtual time.
class GammaRam {
  uint8_t busy_ = 0;
  uint16_t address_ = 0, sample_ = 0;
  unsigned channel_ = 0;
  uint64_t ready_ = 0;
public:
  std::array<std::array<uint16_t,1024>,3> data{};
  std::array<std::array<bool,1024>,3> written{};
  uint64_t commits = 0;
  uint8_t status(uint64_t tick) {
    if (busy_ && tick >= ready_) {
      data[channel_][address_] = sample_; written[channel_][address_] = true;
      busy_ = 0; ++commits;
    }
    return busy_;
  }
  void write(uint8_t command, unsigned address, unsigned sample, uint64_t tick) {
    status(tick);
    auto bits = command & 0xe0;
    if (!bits) return;
    if (bits != 0x20 && bits != 0x40 && bits != 0x80)
      throw std::runtime_error("unsupported gamma multi-channel command");
    if (busy_) throw std::runtime_error("gamma RAM write while previous transaction busy");
    if (address >= 1024 || sample >= 4096) throw std::runtime_error("gamma RAM address/sample out of range");
    channel_ = bits == 0x20 ? 0 : bits == 0x40 ? 1 : 2;
    address_ = uint16_t(address); sample_ = uint16_t(sample);
    busy_ = bits; ready_ = tick + 64;
  }
};
}
