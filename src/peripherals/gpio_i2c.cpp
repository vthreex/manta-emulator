#include "peripherals/gpio_i2c.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
namespace manta {
void GpioI2c::driveLow(uint32_t a, uint8_t pins, bool enabled) {
  for (auto &b : buses_)
    if (b.input == a) {
      if (pins & ~(b.sclMask | b.sdaMask))
        throw std::runtime_error("unknown I2C pin");
      if (enabled)
        b.externalLow |= pins;
      else
        b.externalLow &= uint8_t(~pins);
      update(b);
      return;
    }
  throw std::runtime_error("unknown I2C bus");
}
void GpioI2c::write(uint32_t a, uint8_t v, uint8_t known) {
  for (auto &b : buses_) {
    if (a == b.output) {
      b.out = v;
      b.outKnown = known;
    } else if (a == b.oen) {
      b.direction = v;
      b.directionKnown = known;
    } else
      continue;
    update(b);
  }
}
uint8_t GpioI2c::input(uint32_t a, uint8_t &known) const {
  known = 0;
  uint8_t value = 0;
  for (const auto &b : buses_)
    if (a == b.input) {
      for (auto mask : {b.sclMask, b.sdaMask}) {
        if (!(b.directionKnown & mask))
          continue;
        // Push-pull high against an external low driver is contention, not a
        // known logic level. Stock I2C raises the line by releasing OEN instead.
        if (!(b.direction & mask) && (b.externalLow & mask) &&
            (!(b.outKnown & mask) || (b.out & mask)))
          continue;
        if ((b.direction & mask) || (b.outKnown & mask)) {
          known |= mask;
          if (!(b.externalLow & mask) && ((b.direction & mask) || (b.out & mask)))
            value |= mask;
        }
      }
    }
  return value;
}
void GpioI2c::update(Bus &b) {
  uint8_t known;
  auto v = input(b.input, known);
  auto pins = b.sclMask | b.sdaMask;
  if ((known & pins) != pins) {
    b.valid = b.active = false;
    return;
  }
  bool scl = (v & b.sclMask) != 0, sda = (v & b.sdaMask) != 0;
  if (!b.valid) {
    b.valid = true;
    b.scl = scl;
    b.sda = sda;
    return;
  }
  if (scl != b.scl || sda != b.sda)
    event("i2c_lines", b.input, (scl ? 2 : 0) | (sda ? 1 : 0),
          "resolved pull-up, guest OEN/output and external low driver");
  if (scl && b.scl && sda != b.sda) {
    if (tuner.on && &b == &buses_[1]) {   // START or STOP ends the slave's part of the transfer
      if (tuner.selected && !tuner.reading && tuner.count >= 2) {
        std::copy(tuner.frame, tuner.frame + 6, tuner.last); tuner.lastCount = tuner.count; ++tuner.writes;
        if (std::getenv("MANTA_I2C_LOG")) std::fprintf(stderr, "TUNER frame %u: %02x %02x %02x %02x (divider %u)\n", tuner.count, tuner.frame[0], tuner.frame[1], tuner.frame[2], tuner.frame[3],
          unsigned((tuner.frame[0] & 0x7f) << 8 | tuner.frame[1]));
      }
      tuner.selected = tuner.reading = false; tuner.count = 0; b.externalLow &= uint8_t(~b.sdaMask);
    }
    if (!sda) {
      b.active = true;
      b.bits = b.bytes = 0;
      b.shift = 0;
      event("i2c_start", b.input, 0, "START or repeated START from guest GPIO edges");
    } else {
      b.active = false;
      event("i2c_stop", b.input, 0, "STOP from guest GPIO edges");
    }
  } else if (!b.scl && scl && b.active) {
    if (b.bits < 8) {
      b.shift = uint8_t((b.shift << 1) | (sda ? 1 : 0));
      ++b.bits;
    } else {
      if (std::getenv("MANTA_I2C_LOG"))   // diagnostic: bus, byte index, value
        std::fprintf(stderr, "I2C %08x %u %02x\n", unsigned(b.input), b.bytes, b.shift);
      event(b.bytes == 0 ? "i2c_address" : "i2c_byte", b.input, b.shift,
            sda ? "NACK: no attached slave drives SDA"
                : "ACK-level sampled low; guest drive or external fault, no attached slave");
      ++b.bytes;
      b.bits = 0;
      b.shift = 0;
    }
  }
  if (tuner.on && &b == &buses_[1] && b.active && b.scl && !scl) {   // falling SCL: the slave changes SDA only while SCL is low
    auto drive = [&](bool low) { if (low) b.externalLow |= b.sdaMask; else b.externalLow &= uint8_t(~b.sdaMask); };
    if (b.bits == 8) {   // a byte is complete; the next clock is the acknowledge slot
      if (b.bytes == 0) { tuner.selected = (b.shift & 0xFE) == tuner.address; tuner.reading = b.shift & 1; if (tuner.reading && tuner.selected) ++tuner.reads; drive(tuner.selected); }
      else if (tuner.selected && !tuner.reading) { if (tuner.count < 6) tuner.frame[tuner.count] = b.shift; ++tuner.count; drive(true); }
      else drive(false);   // read: the master acknowledges
    } else if (b.bits == 0 && b.bytes > 0) { drive(false); if (tuner.selected && tuner.reading) drive(!(tuner.status & 0x80)); }
    else if (tuner.selected && tuner.reading && b.bits < 8) drive(!((tuner.status << b.bits) & 0x80));
  }
  b.scl = scl;
  b.sda = sda;
}
} // namespace manta
