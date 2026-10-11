#include "peripherals/riu.hpp"
#include <array>
#include <stdexcept>
namespace manta {
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
                          pc == 0x8053bc08 || pc == 0x8053bd18;
  const bool rmw = pc == 0xbfc00c60 || pc == 0xbfc02804 || pc == 0xbfc02818 || pc == 0xbfc0282c ||
                   pc == 0xbfc02858 || pc == 0x81000408 || runtimeRmw;
  auto v = get(a, n);
  if ((pc == 0x8053bd80 || pc == 0x8053bde4) && n == 1 && bytes_.contains(a) &&
      (bytes_.at(a).known & mask) == mask) {
    event("read", a, n, v, "only guest-masked GPIO bits are consumed and all are known");
    return v;
  }
  if (symbolicInit && runtimeRmw)
    pendingRmw_[a] = pc == 0x803bd2e4 ? 0x800 : mask & 255;
  if (symbolicInit && pc == 0xbfc00c60) pendingRmw_[a] = mask & 255;
  if (!known && symbolicInit && rmw) {
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
