#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include <functional>
#include <bitset>
namespace manta {
// Diagnostic execution only. Unknown observations remain invalid, including
// after the guest caches them in RAM. No branch or device command may consume
// them. This deliberately stops at the first use rather than choosing a value.
class UnknownObservations {
  friend void ioClass(snap::Ar&, UnknownObservations&);
  std::array<bool, 32> registers_{};
  uint32_t unknownMask_ = 0;   // bit r set when registers_[r]; kept in step with every write (cheap "any unknown register" test)
  std::array<bool, 32> timingRegisters_{};
  std::array<uint32_t,32> clockOrigins_{};
  std::map<uint32_t, uint32_t> bytes_; // address -> originating MMIO register
  // Negative filter in front of bytes_: a clear bit proves that 4 KB page (hashed) holds no unknown byte. Set on insertion, never cleared.
  std::bitset<65536> pageMaybe_;
  static size_t pageSlot(uint32_t a) { return (a >> 12) & 0xffff; }
  void noteByte(uint32_t a) { pageMaybe_[pageSlot(a)] = true; }
  bool maybeByte(uint32_t a) const { return pageMaybe_[pageSlot(a)]; }
  uint32_t spiCacheAddress_ = 0;
  bool spiCacheArmed_ = false;
public:
  void seedClockSnapshot(unsigned reg, uint32_t origin) { seed(reg); clockOrigins_[reg] = origin; }
  void seedTimingSnapshot(unsigned reg) { seed(reg); timingRegisters_[reg] = true; }
  bool timingSnapshotUnknown(unsigned reg) const { return registers_[reg] && timingRegisters_[reg]; }
  void seedSpiCache(uint32_t address) { seed(4); spiCacheAddress_ = address & 0x1fffffff; spiCacheArmed_ = true; }
  void seed(unsigned reg) { registers_.at(reg) = true; unknownMask_ |= 1u << reg; timingRegisters_[reg] = false; clockOrigins_[reg] = 0; }
  const auto &bytes() const { return bytes_; }
  void markMemoryUnknown(uint32_t address,size_t size,uint32_t origin){
    for(size_t i=0;i<size;++i){auto k=(address+uint32_t(i))&0x1fffffff;bytes_[k]=origin;noteByte(k);}
  }
  // Fast-path support for the per-instruction hook (identical results, no state change):
  bool anyRegisterUnknown() const {
    return unknownMask_ != 0;
  }
  // True when no byte of [ea, ea+width) (physical, low 29 bits) can be unknown.
  bool memoryClean(uint32_t ea, unsigned width) const {
    uint32_t a = ea & 0x1fffffff;
    return !maybeByte(a) && !maybeByte((a + width - 1) & 0x1fffffff);
  }
  void restored() { pageMaybe_.reset(); for (auto& kv : bytes_) noteByte(kv.first); unknownMask_ = 0; for (unsigned r = 0; r < 32; ++r) if (registers_[r]) unknownMask_ |= 1u << r; }   // after the snapshot loader rewrote bytes_
  void markRegisterKnown(unsigned reg){ registers_.at(reg)=false; unknownMask_ &= ~(1u << reg); timingRegisters_[reg]=false; clockOrigins_[reg]=0; }
  void overwriteKnownMemory(uint32_t address,size_t size) {
    for(size_t i=0;i<size;++i){auto k=(address+uint32_t(i))&0x1fffffff;if(maybeByte(k))bytes_.erase(k);}
  }
  bool registerUnknown(unsigned reg) const { return registers_.at(reg); }
  std::string observe(uint32_t pc, uint32_t w, const std::function<uint32_t(unsigned)> &reg) {
    // Fast path: no register holds an unknown observation (then its timing flag and clock origin are clear as well), so a non-memory
    // instruction can neither consume nor produce one. Loads and stores always take the full path (RAM bytes may be unknown).
    {
      unsigned fop = w >> 26;
      if (!unknownMask_ && (fop < 32 || fop == 47 || fop == 51)) return {};
    }
    // Exact identity copies in the stock clock snapshot routine. Other masks,
    // arithmetic, branch conditions and MMIO stores still reject the data.
    if ((pc == 0x805fd444 && w == 0x311100ff && clockOrigins_[8]) ||
        (pc == 0x805fd44c && w == 0x313200ff && clockOrigins_[9])) {
      unsigned from = pc == 0x805fd444 ? 8 : 9, to = from + 9;
      registers_[to] = true; unknownMask_ |= 1u << to; clockOrigins_[to] = clockOrigins_[from];
      return {};
    }
    // Stock display timing setup saves/restores one opaque byte around a
    // temporary hold. AND FF leaves every valid byte bit unchanged.
    if (pc == 0x805e5bfc && w == 0x314a00ff && timingSnapshotUnknown(10)) return {};
    if (pc == 0x805e5e4c && w == 0x30c600ff && timingSnapshotUnknown(6)) return {};
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
    bool spiCache = spiCacheArmed_ && pc == 0x8053c724 && w == 0xa0640000 && address == spiCacheAddress_;
    bool restore = address == 0x1f205e00 &&
                   ((pc == 0x805e5d44 && w == 0xa06a5e00 && timingSnapshotUnknown(10)) ||
                    (pc == 0x805e5eb4 && w == 0xa0465e00 && timingSnapshotUnknown(6)));
    bool clockCopy = op == 43 && address < 0x1f000000 && clockOrigins_[rt];
    if ((useRs && registers_[rs]) || (useRt && registers_[rt] && !((cache && op == 41) || spiCache || restore || clockCopy)))
      return "unknown diagnostic observation consumed by guest instruction";
    uint32_t reloadedClock = 0;
    if (readWidth) {
      if (op == 34 || op == 38)
        address &= ~3u;
      for (unsigned i = 0; i < readWidth; ++i)
        if (maybeByte(address + i) && bytes_.contains(address + i)) {
          auto origin = bytes_.at(address+i);
          if (op != 35 || (origin != 0x1f201751 && origin != 0x1f201754) ||
              (reloadedClock && reloadedClock != origin))
            return "unknown diagnostic observation consumed from guest RAM";
          reloadedClock = origin; // transparent interrupt/save-frame copy only
        }
    }
    if (writeWidth && address < 0x1f000000) {
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
        if (clockCopy && registers_[rt]) { bytes_[address+i] = clockOrigins_[rt]; noteByte(address + i); }
        else if (spiCache && registers_[rt]) {
          bytes_[address + i] = 0x1f001014; noteByte(address + i);
        } else if (cache && registers_[rt]) {
          bytes_[address + i] = pc == 0x804698a8 ? 0x1f200a38 : 0x1f200a3c; noteByte(address + i);
        } else if (op != 56) {
          if (maybeByte(address + i)) bytes_.erase(address + i); // SC success not known at dispatch
        }
      }
      if (spiCache) spiCacheArmed_ = false;
    }
    if (destination > 0) {
      registers_[unsigned(destination)] = false; unknownMask_ &= ~(1u << unsigned(destination));
      timingRegisters_[unsigned(destination)] = false;
      clockOrigins_[unsigned(destination)] = 0;
      if (reloadedClock) { registers_[unsigned(destination)] = true; unknownMask_ |= 1u << unsigned(destination); clockOrigins_[unsigned(destination)] = reloadedClock; }
    }
    registers_[0] = false; unknownMask_ &= ~1u;
    return {};
  }
};
} // namespace manta
