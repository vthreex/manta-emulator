#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <functional>
#include <string>
namespace manta {
// Stock descriptor pairs: GPIO 65/66 and 69/70. OEN=1 releases the
// line. A pulled-up bus resolves low if any participant drives low.
// No slave is attached: address and data ACK bits are therefore high.
class GpioI2c {
  friend void ioClass(snap::Ar&, GpioI2c&);
  struct Bus {
    uint32_t input, output, oen;
    uint8_t sclMask, sdaMask;
    uint8_t out = 0, outKnown = 0, direction = 0, directionKnown = 0;
    bool valid = false, scl = true, sda = true, active = false;
    unsigned bits = 0, bytes = 0;
    uint8_t shift = 0;
    uint8_t externalLow = 0;
  };
  Bus buses_[2] = {{0x1f203d05, 0x1f203d09, 0x1f203d0d, 2, 4},
                   {0x1f203d11, 0x1f203d15, 0x1f203d19, 4, 8}};
  void update(Bus &b);

public:
  // EXPLORATORY PLL tuner slave on the tuner bus (GPIO pair 2, input 1F203D11): the original firmware writes address 0xC0 + 4 bytes
  // (DB1 DB2 CB BB, the classic PLL tuner frame) and retries 5x when nobody ACKs. The model ACKs 0xC0/0xC1, keeps the last frame and
  // answers reads with `status` (bit 6 = FL phase lock in that family; ASSUMPTION, no datasheet for the Manta tuner). Not snapshot state.
  struct PllTuner { bool on = false; uint8_t address = 0xC0, status = 0x40; uint8_t frame[6]{}; unsigned count = 0; uint32_t writes = 0, reads = 0;
    uint8_t last[6]{}; unsigned lastCount = 0; bool selected = false, reading = false; } tuner;
  std::function<void(const std::string &, uint32_t, uint32_t, const std::string &)> event;
  void write(uint32_t a, uint8_t v, uint8_t known);
  // An external open-drain participant can pull a line low, never drive high.
  // Suitable for real peripheral models and explicit stuck-bus fault fixtures.
  void driveLow(uint32_t inputAddress, uint8_t pins, bool enabled);
  // Returns only electrically established input bits, preserving other bits.
  uint8_t input(uint32_t a, uint8_t &known) const;
};
} // namespace manta
