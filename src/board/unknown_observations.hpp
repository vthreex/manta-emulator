#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <functional>
namespace manta {
// Diagnostic execution only. Unknown observations remain invalid, including
// after the guest caches them in RAM. No branch or device command may consume
// them. This deliberately stops at the first use rather than choosing a value.
class UnknownObservations {
  std::array<bool, 32> registers_{};
  std::map<uint32_t, uint32_t> bytes_; // address -> originating MMIO register
public:
  void seed(unsigned reg) { registers_.at(reg) = true; }
  const auto &bytes() const { return bytes_; }
  bool registerUnknown(unsigned reg) const { return registers_.at(reg); }
  std::string observe(uint32_t pc, uint32_t w, const std::function<uint32_t(unsigned)> &reg) {
    unsigned op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31,
             fn = w & 63;
    bool useRs = false, useRt = false;
    int destination = -1;
    unsigned readWidth = 0, writeWidth = 0;
    if (op == 0) {
      if (fn == 0 || fn == 2 || fn == 3) {
        useRt = true;
        destination = int(rd);
      } else if (fn == 16 || fn == 18)
        destination = int(rd);
      else if (fn == 8 || fn == 9) {
        useRs = true;
        if (fn == 9)
          destination = int(rd);
      } else if (fn == 17 || fn == 19)
        useRs = true;
      else if (fn == 12 || fn == 13 || fn == 15) {
      } // syscall, break, sync
      else {
        useRs = true;
        useRt = true;
        destination = int(rd);
        if ((fn == 10 && reg(rt) != 0) || (fn == 11 && reg(rt) == 0))
          destination = -1;
      }
    } else if (op == 2 || op == 3) {
      if (op == 3)
        destination = 31;
    } else if (op == 1 || op == 6 || op == 7 || op == 22 || op == 23)
      useRs = true;
    else if (op == 4 || op == 5 || op == 20 || op == 21) {
      useRs = true;
      useRt = true;
    } else if (op >= 8 && op <= 14) {
      useRs = true;
      destination = int(rt);
    } else if (op == 15)
      destination = int(rt);
    else if (op == 16) {
      if (rs == 0)
        destination = int(rt);
      else if (rs == 4)
        useRt = true;
    } else if (op == 28) {
      useRs = true;
      useRt = true;
      destination = int(rd);
    } else if (op == 31) {
      useRs = true;
      useRt = true;
      destination = int(rt);
    } else if (op == 32 || op == 36) {
      readWidth = 1;
      destination = int(rt);
      useRs = true;
    } else if (op == 33 || op == 37) {
      readWidth = 2;
      destination = int(rt);
      useRs = true;
    } else if (op == 35 || op == 48) {
      readWidth = 4;
      destination = int(rt);
      useRs = true;
    } else if (op == 34 || op == 38) {
      readWidth = 4;
      useRs = true;
      useRt = true;
      destination = int(rt);
    } else if (op == 40) {
      writeWidth = 1;
      useRs = true;
      useRt = true;
    } else if (op == 41) {
      writeWidth = 2;
      useRs = true;
      useRt = true;
    } else if (op == 42 || op == 43 || op == 46 || op == 56) {
      writeWidth = 4;
      useRs = true;
      useRt = true;
    } else if (op == 47 || op == 51)
      useRs = true; // cache, prefetch
    else if (op == 49 || op == 53) {
      useRs = true;
      readWidth = op == 49 ? 4 : 8;
    } else if (op == 57 || op == 61) {
      useRs = true;
      writeWidth = op == 57 ? 4 : 8;
    } else {
      for (bool unknown : registers_)
        if (unknown)
          return "unknown observation: unsupported instruction provenance";
    }
    auto address = (reg(rs) + uint32_t(int32_t(int16_t(w)))) & 0x1fffffff;
    // Only the two disassembled diagnostic cache stores are permitted to copy
    // an unknown register. Their source read is separately byte-verified.
    bool cache =
        (pc == 0x804698a8 && address == 0x006b914a) || (pc == 0x804698b8 && address == 0x006b910e);
    if ((useRs && registers_[rs]) || (useRt && registers_[rt] && !(cache && op == 41)))
      return "unknown diagnostic observation consumed by guest instruction";
    if (readWidth) {
      if (op == 34 || op == 38)
        address &= ~3u;
      for (unsigned i = 0; i < readWidth; ++i)
        if (bytes_.contains(address + i))
          return "unknown diagnostic observation consumed from guest RAM";
    }
    if (writeWidth && address < 0x10000000) {
      if (op == 42 || op == 46)
        address &= ~3u; // conservative full-word invalidation
      // Partial SWL/SWR must not falsely clear bytes they do not overwrite.
      unsigned begin = 0, end = writeWidth;
      if (op == 42) {
        auto lane = (reg(rs) + uint32_t(int32_t(int16_t(w)))) & 3;
        end = lane + 1;
      }
      if (op == 46) {
        auto lane = (reg(rs) + uint32_t(int32_t(int16_t(w)))) & 3;
        begin = lane;
      }
      for (unsigned i = begin; i < end; ++i) {
        if (cache && registers_[rt])
          bytes_[address + i] = pc == 0x804698a8 ? 0x1f200a38 : 0x1f200a3c;
        else if (op != 56)
          bytes_.erase(address + i); // SC success not known at dispatch
      }
    }
    if (destination > 0)
      registers_[unsigned(destination)] = false;
    registers_[0] = false;
    return {};
  }
};
} // namespace manta
