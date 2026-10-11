#include "peripherals/riu.hpp"
#include <array>
#include <stdexcept>
namespace manta {
uint32_t Riu::knownBits(uint32_t a, unsigned n) const {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(a + i);
    if (it != bytes_.end())
      v |= uint32_t(it->second.known) << (8 * i);
  }
  return v;
}
uint32_t Riu::get(uint32_t a, unsigned n) const {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(a + i);
    v |= uint32_t(it == bytes_.end() ? 0xa5 : it->second.value) << (i * 8);
  }
  return v;
}
void Riu::put(uint32_t a, unsigned n, uint32_t v) {
  for (unsigned i = 0; i < n; ++i)
    bytes_[a + i] = {uint8_t(v >> (i * 8)), 255};
}
uint32_t Riu::crcMsb(const std::vector<uint8_t> &b, uint32_t poly, uint32_t crc) {
  std::array<uint32_t, 256> t{};
  for (unsigned i = 0; i < 256; ++i) {
    auto c = i << 24;
    for (int j = 0; j < 8; ++j)
      c = (c << 1) ^ ((c & 0x80000000) ? poly : 0);
    t[i] = c;
  }
  for (auto c : b)
    crc = (crc << 8) ^ t[((crc >> 24) ^ c) & 255];
  return crc;
}
uint32_t Riu::read(uint32_t a, unsigned n, uint32_t pc, uint32_t mask, uint64_t tick) {
  if (n != 1 && n != 2 && n != 4)
    throw std::runtime_error("Unsupported RIU access width");
  if (candidateMiu && (a == 0x1f202400 || a == 0x1f200c00) && miuArmed_[a == 0x1f200c00]) {
    auto index = unsigned(a == 0x1f200c00);
    auto v = get(a, n) & ~0x8000u;
    if (tick >= miuReady_[index])
      v |= 0x8000;
    event("read", a, n, v,
          "MIU initialization completion after observed 0/8/C/E/1F sequence; provisional timing");
    return v;
  }
  if (candidateMiu && a == 0x1f2025c0 && bistPending_) {
    auto v = get(a, n) & ~0xe000u;
    if (tick >= bistReady_)
      v |= bistResult_;
    event("read", a, n, v,
          "BIST completion and failure derived from bounded guest DRAM write/read comparison");
    return v;
  }
  if (gpioI2c && n == 1) {
    uint8_t known;
    auto levels = gpio_.input(a, known);
    if (known && mask && (known & mask) == mask) {
      auto v = (get(a, 1) & ~uint32_t(known)) | levels;
      event("read", a, n, v,
            "GPIO input resolved from guest OEN/output, pull-ups and external low driver; no "
            "attached I2C slave");
      return v;
    }
  }
  if (uartEnabled_ && a == 0x1f201328 && n == 1) {
    uint32_t status = tick >= uartReady_ ? 0x60 : 0;
    event("read", a, n, status,
          "candidate UART LSR: THRE/TEMT follows guest TX writes; no RX source");
    return status;
  }
  if (candidateBdma && a == 0x1f201204 && n == 1) {
    if (bdmaPending_ && tick >= bdmaComplete_) {
      put(a, 1, 8);
      bdmaPending_ = false;
    }
    auto v = bytes_.contains(a) ? get(a, 1) : 0;
    event("read", a, n, v, "candidate BDMA busy/done status");
    return v;
  }
  bool known = true;
  for (unsigned i = 0; i < n; ++i)
    if (!bytes_.contains(a + i) || bytes_.at(a + i).known != 255)
      known = false;
  const bool runtimeRmw = pc == 0x8055b578 || pc == 0x8055b584 || pc == 0x803bd6f4 ||
                          pc == 0x803bd754 || pc == 0x803bd2e4 || pc == 0x8053bbdc ||
                          pc == 0x8053bc08 || pc == 0x8053bd18 || pc == 0x8053bc84 ||
                          pc == 0x8053bcb0 || pc == 0x8060a780 || pc == 0x8060a784;
  const bool rmw = pc == 0xbfc00c60 || pc == 0xbfc02804 || pc == 0xbfc02818 || pc == 0xbfc0282c ||
                   pc == 0xbfc02858 || pc == 0x81000408 || runtimeRmw;
  auto v = get(a, n);
  if (candidateMiu && symbolicInit && (pc == 0xbfc01bc4 || pc == 0xbfc01b70) && n == 4 &&
      (a == 0x1f221adc || a == 0x1f221bdc) && (knownBits(a, n) & 255) == 255) {
    event("read", a, n, v,
          "only guest-programmed low byte of DDR phase seed is consumed; BFC01C3C/1C74 masks "
          "discard unknown upper bits");
    return v;
  }
  if ((pc == 0x8053bd80 || pc == 0x8053bde4) && n == 1 && bytes_.contains(a) &&
      (bytes_.at(a).known & mask) == mask) {
    event("read", a, n, v, "only guest-masked GPIO bits are consumed and all are known");
    return v;
  }
  if (symbolicInit && runtimeRmw)
    pendingRmw_[a] = pc == 0x803bd2e4                         ? 0x800
                     : (pc == 0x8060a780 || pc == 0x8060a784) ? mask & 65535
                                                              : mask & 255;
  if (symbolicInit && pc == 0xbfc00c60)
    pendingRmw_[a] = mask & 255;
  if (!known && symbolicInit && (rmw || pendingRmw_.contains(a))) {
    event("read", a, n, v,
          "SYMBOLIC unknown bits preserved by verified configuration RMW; mask=" +
              std::to_string(mask));
    return v;
  }
  if (!known) {
    stopReason = "unmodeled MMIO read";
    event("blocked_read", a, n, v, "unknown reset/status bits; value is poison, execution stops");
    return v;
  }
  event("read", a, n, v, "write latch only; hardware side effects unverified");
  return v;
}
void Riu::write(uint32_t a, unsigned n, uint32_t v, uint32_t pc, uint32_t mask, uint64_t tick) {
  if (n != 1 && n != 2 && n != 4)
    throw std::runtime_error("Unsupported RIU access width");
  // Straight-line RMW updates retain unknown-bit masks. No status poll is
  // guessed.
  if (symbolicInit && pendingRmw_.contains(a)) {
    auto forced = pendingRmw_.at(a);
    for (unsigned i = 0; i < n; ++i) {
      auto &b = bytes_[a + i];
      b.value = uint8_t(v >> (i * 8));
      b.known |= uint8_t(forced >> (i * 8));
    }
    pendingRmw_.erase(a);
  } else if (symbolicInit && pc == 0xbfc00c7c && n == 1) {
    auto &b = bytes_[a];
    b.value = uint8_t(v);
    b.known |= uint8_t(mask);
  } else if (symbolicInit && (pc == 0xbfc02810 || pc == 0xbfc02824 || pc == 0xbfc02838 ||
                              pc == 0xbfc02864 || pc == 0x81000410)) {
    uint32_t forced = pc == 0xbfc02810   ? 0x800
                      : pc == 0xbfc02824 ? 0x400
                      : pc == 0xbfc02838 ? 0x80
                      : pc == 0xbfc02864 ? 0x80
                                         : 0x1000;
    for (unsigned i = 0; i < n; ++i) {
      auto &b = bytes_[a + i];
      b.value = uint8_t(v >> (8 * i));
      b.known |= uint8_t(forced >> (8 * i));
    }
  } else
    put(a, n, v);
  event("write", a, n, v, "recorded register storage; not a complete device");
  if (candidateMiu && (a == 0x1f202400 || a == 0x1f200c00)) {
    auto index = unsigned(a == 0x1f200c00);
    constexpr uint32_t sequence[] = {0, 8, 12, 14, 31};
    if (v == 0) {
      miuSequence_[index] = 1;
      miuArmed_[index] = false;
    } else if (miuSequence_[index] < 5 && v == sequence[miuSequence_[index]]) {
      if (++miuSequence_[index] == 5) {
        miuArmed_[index] = true;
        miuReady_[index] = tick + 64;
        event("miu_init", a, n, v,
              "stock DRAM initialization command sequence accepted; ideal digital DRAM, no analog "
              "PHY timing model");
      }
    } else {
      miuArmed_[index] = false;
      miuSequence_[index] = 0;
    }
  }
  if (candidateMiu && a == 0x1f2025c0) {
    bistPending_ = false;
    if (v & 1) {
      auto mode = v & 0x1fff;
      if (mode != 3 && mode != 0x93 && mode != 0x95 && mode != 0x9b && mode != 0x99 &&
          mode != 0x97) {
        stopReason = "unsupported MIU BIST pattern command " + std::to_string(v);
        return;
      }
      for (auto field : {0x1f2025c4u, 0x1f2025c8u, 0x1f2025ccu, 0x1f2025d0u})
        if (knownBits(field, 2) != 0xffff) {
          stopReason = "MIU BIST requires guest-programmed address, length and pattern";
          return;
        }
      if (!(knownBits(0x1f2025e0, 2) & 0x8000)) {
        stopReason = "MIU BIST bank selection unknown";
        return;
      }
      uint64_t start = uint64_t(get(0x1f2025c4, 2)) * 4096;
      uint64_t length = (uint64_t(get(0x1f2025c8, 2)) | (uint64_t(get(0x1f2025cc, 2)) << 16)) * 16;
      bool second = (get(0x1f2025e0, 2) & 0x8000) != 0;
      if (start >= 0x08000000 || !length || length > 0x08000000 - start || !miuArmed_[second]) {
        stopReason = "MIU BIST range or initialized bank invalid";
        return;
      }
      auto address = uint32_t(start) + (second ? 0x08000000 : 0);
      auto pattern = uint16_t(get(0x1f2025d0, 2));
      // All five calibration commands perform destructive write/read tests.
      // This functional digital-memory model tests the programmed pattern and
      // its complement; exact silicon pattern generators/PHY eye are unresolved.
      bool pass = dramTest(address, size_t(length), pattern);
      if (mode != 3)
        pass = dramTest(address, size_t(length), uint16_t(~pattern)) && pass;
      bistResult_ = 0x8000 | (pass ? 0 : 0x2000);
      bistReady_ = tick + 64;
      bistPending_ = true;
      event("bist_compare", address, unsigned(length), pattern,
            pass ? "guest DRAM write/read passed; calibration uses functional pattern/complement "
                   "approximation, analog PHY not modeled"
                 : "guest DRAM comparison failed");
    }
  }
  if (gpioI2c) {
    gpio_.event = [this](auto &kind, auto address, auto value, auto &detail) {
      event(kind, address, 1, value, detail);
    };
    for (unsigned i = 0; i < n; ++i)
      gpio_.write(a + i, bytes_[a + i].value, bytes_[a + i].known);
  }
  if (a == 0x1f201300 && (n == 1 || n == 4)) {
    if (!uartEnabled_)
      uart.push_back(char(v & 255)); // raw candidate UART write capture only
    else if (!(get(0x1f201318, 1) & 0x80)) {
      uart.push_back(char(v & 255));
      uartReady_ = tick + 16;
      event("uart_tx", a, n, v, "guest byte emitted; 16 dispatch ticks provisional TX latency");
    }
  }
  if (candidateBdma && a == 0x1f201200 && n == 1 && (v & 1)) {
    auto mode = get(0x1f201208, 2);
    auto src = get(0x1f201210, 2) | (get(0x1f201214, 2) << 16);
    auto len = get(0x1f201220, 2) | (get(0x1f201224, 2) << 16);
    auto poly = get(0x1f201228, 2) | (get(0x1f20122c, 2) << 16);
    auto seed = get(0x1f201230, 2) | (get(0x1f201234, 2) << 16);
    if (mode != 0x335 || poly != 0x04c11db7 || src > 0x7fffff || len > 0x800000 - src) {
      stopReason = "unsupported BDMA command or bounds";
      return;
    }
    auto crc = crcMsb(flashRead(src, len), poly, seed);
    put(0x1f201230, 2, crc & 65535);
    put(0x1f201234, 2, crc >> 16);
    put(0x1f201204, 1, 1);
    bdmaPending_ = true;
    bdmaComplete_ = tick + 64;
    event("bdma_crc", src, len, crc,
          "stock programmed polynomial/seed and bounded flash source; "
          "candidate completion latency 64 dispatch ticks");
  }
}
} // namespace manta
