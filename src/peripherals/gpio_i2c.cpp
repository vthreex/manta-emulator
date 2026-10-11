#include "peripherals/gpio_i2c.hpp"
#include <stdexcept>
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
      event(b.bytes == 0 ? "i2c_address" : "i2c_byte", b.input, b.shift,
            sda ? "NACK: no attached slave drives SDA"
                : "ACK-level sampled low; guest drive or external fault, no attached slave");
      ++b.bytes;
      b.bits = 0;
      b.shift = 0;
    }
  }
  b.scl = scl;
  b.sda = sda;
}
} // namespace manta
