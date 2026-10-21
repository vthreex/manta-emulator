#pragma once
// ORBIS32 (OpenRISC 1000, 32-bit) interpreter for the multimedia coprocessor (EXPLORATORY; instruction set per the public OpenRISC 1000
// architecture manual, little-endian memory image as found in the original resource, see docs/multimedia-copro-startup.md).
// Concrete values only: unknown hardware stays unknown at the bus (the bus callbacks decide whether an access is modelled).
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
class Or1k {
public:
  // size is 1, 2 or 4; `addr` is the CPU's effective (virtual == physical) address. A bus callback may throw to stop the machine.
  std::function<uint32_t(uint32_t addr, unsigned size)> read;
  std::function<void(uint32_t addr, unsigned size, uint32_t value)> write;
  std::function<void(const std::string&)> log;      // optional diagnostics
  bool bigEndian = false;                           // data accesses (instruction words are always fetched little-endian from the image)

  static constexpr uint32_t kSM = 1u << 0, kTEE = 1u << 1, kIEE = 1u << 2, kF = 1u << 9, kCY = 1u << 10, kOV = 1u << 11, kDSX = 1u << 13, kEPH = 1u << 14;

  void reset(uint32_t pc) {
    r_.fill(0); pc_ = pc; delayActive_ = false; sr_ = kSM | (1u << 15); epcr_ = eear_ = esr_ = 0; ttmr_ = ttcr_ = 0; picmr_ = picsr_ = levelLines_ = 0;
    maclo_ = machi_ = 0; instructions_ = 0; halted_ = false; spr_.clear(); sleeping_ = false;
  }
  uint32_t pc() const { return pc_; }
  uint32_t reg(unsigned n) const { return r_[n]; }
  void setReg(unsigned n, uint32_t v) { if (n) r_[n] = v; }
  uint32_t sr() const { return sr_; }
  uint64_t instructions() const { return instructions_; }
  bool halted() const { return halted_; }
  void halt() { halted_ = true; }
  bool sleeping() const { return sleeping_; }
  void setIrq(unsigned line, bool level) { if (level) picsr_ |= 1u << line; else picsr_ &= ~(1u << line); }
  // Level-sensitive line: the device owns the state, a PICSR write cannot clear it.
  void setLevel(unsigned line, bool level) { if (level) levelLines_ |= 1u << line; else levelLines_ &= ~(1u << line); }
  uint32_t picsr() const { return picsr_ | levelLines_; }
  uint32_t picmr() const { return picmr_; }
  uint32_t ttcr() const { return ttcr_; }

  // Execute one instruction (with its delay slot pending semantics). Returns false when halted.
  bool step() {
    if (halted_) return false;
    ++instructions_;
    tickTimer(1);
    if (!delayActive_ && (sr_ & kIEE) && (((picsr_ | levelLines_) & picmr_) != 0 || ((ttmr_ >> 28) & 1 && (ttmr_ >> 29) & 1) && (sr_ & kTEE))) {
      if ((sr_ & kTEE) && ((ttmr_ >> 28) & 1) && ((ttmr_ >> 29) & 1)) { exception(0x500, pc_, pc_); exceptionTaken_ = false; return true; }
      if ((picsr_ | levelLines_) & picmr_) { exception(0x800, pc_, pc_); exceptionTaken_ = false; return true; }
    }
    uint32_t cur = pc_;
    if (cur & 3) { exception(0x600, cur, cur); exceptionTaken_ = false; return true; }   // unaligned instruction fetch: alignment exception, EEAR = the bad PC
    uint32_t w = fetch(cur);
    bool inDelay = delayActive_;
    uint32_t nextPc = inDelay ? delayTarget_ : cur + 4;
    bool wasDelay = delayActive_;
    delayActive_ = false;
    bool jumped = false; uint32_t target = 0;
    exec(w, cur, wasDelay ? branchPc_ : cur, jumped, target);
    if (halted_) return false;
    if (exceptionTaken_) { exceptionTaken_ = false; return true; }
    if (jumped) {            // the instruction after this one (delay slot) executes first, then the target
      delayActive_ = true; delayTarget_ = target; branchPc_ = cur;
      pc_ = cur + 4;
    } else pc_ = nextPc;
    return true;
  }

private:
  std::array<uint32_t, 32> r_{};
  uint32_t pc_ = 0, sr_ = 0, epcr_ = 0, eear_ = 0, esr_ = 0, ttmr_ = 0, ttcr_ = 0, picmr_ = 0, picsr_ = 0, levelLines_ = 0, maclo_ = 0, machi_ = 0;
  uint32_t delayTarget_ = 0, branchPc_ = 0;
  bool delayActive_ = false, halted_ = false, exceptionTaken_ = false, sleeping_ = false;
  uint64_t instructions_ = 0;
  std::map<uint32_t, uint32_t> spr_;

  void tickTimer(uint32_t n) {
    unsigned mode = ttmr_ >> 30;
    if (!mode) return;
    ttcr_ += n;
    if ((ttcr_ & 0x0fffffff) >= (ttmr_ & 0x0fffffff) && !((ttmr_ >> 28) & 1)) {
      ttmr_ |= 1u << 28;                       // IP
      if (mode == 1) ttcr_ = 0;                // restart
      else if (mode == 2) ttmr_ &= 0x3fffffff; // single run: stop
    }
  }
  uint32_t fetch(uint32_t a) const {
    uint32_t v = read(a, 4);
    if (bigEndian) return __builtin_bswap32(v);
    return v;
  }
  uint32_t load(uint32_t a, unsigned size, bool sign) {
    if (a & (size - 1)) { align(a); return 0; }
    uint32_t v = read(a, size);
    if (bigEndian) { if (size == 2) v = __builtin_bswap16(uint16_t(v)); else if (size == 4) v = __builtin_bswap32(v); }
    if (sign) { if (size == 1) v = uint32_t(int32_t(int8_t(v))); else if (size == 2) v = uint32_t(int32_t(int16_t(v))); }
    return v;
  }
  void store(uint32_t a, unsigned size, uint32_t v) {
    if (a & (size - 1)) { align(a); return; }
    if (bigEndian) { if (size == 2) v = __builtin_bswap16(uint16_t(v)); else if (size == 4) v = __builtin_bswap32(v); }
    write(a, size, v);
  }
  void align(uint32_t a) { exception(0x600, curPc_, a); }
  uint32_t curPc_ = 0;
  void exception(unsigned vector, uint32_t epc, uint32_t ea) {
    if (log) { char b[96]; std::snprintf(b, sizeof b, "exception %03x at pc=%08x epc=%08x ea=%08x sr=%08x", vector, pc_, epc, ea, sr_); log(b); }
    esr_ = sr_; epcr_ = epc; eear_ = ea;
    sr_ = (sr_ | kSM) & ~(kIEE | kTEE);
    pc_ = ((sr_ & kEPH) ? 0xf0000000u : 0u) + vector;
    delayActive_ = false; exceptionTaken_ = true; sleeping_ = false;
  }
  static int32_t sext(uint32_t v, unsigned bits) { return int32_t(v << (32 - bits)) >> (32 - bits); }
  void setFlag(bool f) { if (f) sr_ |= kF; else sr_ &= ~kF; }
  uint32_t sprRead(uint32_t n) {
    switch (n) {
    case 0: return 0x12000001;      // VR (version register: placeholder, bit layout only)
    case 1: return 0x1;             // UPR: present
    case 11: return 0;              // EVBAR
    case 16: return pc_;            // NPC
    case 17: return sr_;
    case 18: return curPc_;         // PPC
    case 32: return epcr_;
    case 48: return eear_;
    case 64: return esr_;
    case 0x2801: return maclo_;
    case 0x2802: return machi_;
    case 0x4800: return picmr_;
    case 0x4802: return picsr_ | levelLines_;
    case 0x5000: return ttmr_;
    case 0x5001: return ttcr_;
    }
    if (n >= 1024 && n < 1024 + 32) return r_[n - 1024];
    auto it = spr_.find(n);
    return it == spr_.end() ? 0 : it->second;
  }
  void sprWrite(uint32_t n, uint32_t v) {
    if (log && (n == 0x5000 || n == 0x5001 || n == 0x4800 || n == 0x4802)) { char b[96]; std::snprintf(b, sizeof b, "mtspr %04x = %08x at pc=%08x (n=%llu)", n, v, curPc_, (unsigned long long)instructions_); log(b); }
    switch (n) {
    case 17: sr_ = v | (1u << 15); return;
    case 32: epcr_ = v; return;
    case 48: eear_ = v; return;
    case 64: esr_ = v; return;
    case 0x2801: maclo_ = v; return;
    case 0x2802: machi_ = v; return;
    case 0x4800: picmr_ = v; return;
    case 0x4802: picsr_ &= v; return;     // write-0-clear (OR1K PICSR semantics for edge/level handled by the device)
    case 0x5000: ttmr_ = v; return;
    case 0x5001: ttcr_ = v; return;
    }
    if (n >= 1024 && n < 1024 + 32) { if (n != 1024) r_[n - 1024] = v; return; }
    spr_[n] = v;
  }
  void exec(uint32_t w, uint32_t pc, uint32_t exceptionPc, bool& jumped, uint32_t& target) {
    curPc_ = pc;
    unsigned op = w >> 26, D = (w >> 21) & 31, A = (w >> 16) & 31, B = (w >> 11) & 31;
    uint32_t I = w & 0xffff;
    int32_t sI = int16_t(I);
    auto rA = [&] { return r_[A]; };
    auto rB = [&] { return r_[B]; };
    auto wr = [&](unsigned d, uint32_t v) { if (d) r_[d] = v; };
    auto illegal = [&] { exception(0x700, exceptionPc, pc); };
    switch (op) {
    case 0x00: jumped = true; target = pc + uint32_t(sext(w & 0x3ffffff, 26) * 4); break;                     // l.j
    case 0x01: wr(9, pc + 8); jumped = true; target = pc + uint32_t(sext(w & 0x3ffffff, 26) * 4); break;       // l.jal
    case 0x03: if (!(sr_ & kF)) { jumped = true; target = pc + uint32_t(sext(w & 0x3ffffff, 26) * 4); } break;  // l.bnf
    case 0x04: if (sr_ & kF) { jumped = true; target = pc + uint32_t(sext(w & 0x3ffffff, 26) * 4); } break;     // l.bf
    case 0x05: break;                                                                                          // l.nop
    case 0x06: if (w & (1u << 16)) { wr(D, maclo_); maclo_ = machi_ = 0; } else wr(D, I << 16); break;         // l.macrc / l.movhi
    case 0x08: {
      unsigned sub = w >> 16;
      if (sub == 0x2000) exception(0xc00, pc + 4, pc);                                                          // l.sys
      else if (sub == 0x2100) exception(0xe00, pc, pc);                                                          // l.trap
      else if (sub == 0x2200 || sub == 0x2280 || sub == 0x2300) {}                                              // msync/psync/csync
      else illegal();
      break;
    }
    case 0x09: { uint32_t e = esr_, p = epcr_; sr_ = e; pc_ = p; delayActive_ = false; exceptionTaken_ = true; break; }   // l.rfe
    case 0x11: jumped = true; target = rB(); break;                                                            // l.jr
    case 0x12: { uint32_t t = rB(); wr(9, pc + 8); jumped = true; target = t; break; }                         // l.jalr
    case 0x13: { int64_t acc = (int64_t(machi_) << 32) | maclo_; acc += int64_t(int32_t(rA())) * sI; maclo_ = uint32_t(acc); machi_ = uint32_t(acc >> 32); break; }  // l.maci
    case 0x21: wr(D, load(rA() + uint32_t(sI), 4, false)); break;
    case 0x22: wr(D, load(rA() + uint32_t(sI), 4, true)); break;
    case 0x23: wr(D, load(rA() + uint32_t(sI), 1, false)); break;
    case 0x24: wr(D, load(rA() + uint32_t(sI), 1, true)); break;
    case 0x25: wr(D, load(rA() + uint32_t(sI), 2, false)); break;
    case 0x26: wr(D, load(rA() + uint32_t(sI), 2, true)); break;
    case 0x27: { uint32_t a = rA(), b = uint32_t(sI), s = a + b; setFlagCyOv(a, b, s, false); wr(D, s); break; }   // l.addi
    case 0x28: { uint32_t a = rA(), b = uint32_t(sI), c = (sr_ & kCY) ? 1 : 0, s = a + b + c; setFlagCyOv(a, b, s, false); wr(D, s); break; }
    case 0x29: wr(D, rA() & I); break;                                                                        // l.andi
    case 0x2a: wr(D, rA() | I); break;                                                                        // l.ori
    case 0x2b: wr(D, rA() ^ uint32_t(sI)); break;                                                             // l.xori
    case 0x2c: wr(D, uint32_t(int32_t(rA()) * sI)); break;                                                    // l.muli
    case 0x2d: wr(D, sprRead(rA() | I)); break;                                                               // l.mfspr
    case 0x2e: {
      unsigned sh = w & 63; uint32_t a = rA();
      switch ((w >> 6) & 3) {
      case 0: wr(D, a << sh); break;
      case 1: wr(D, a >> sh); break;
      case 2: wr(D, uint32_t(int32_t(a) >> sh)); break;
      default: wr(D, sh ? (a >> sh) | (a << (32 - sh)) : a); break;
      }
      break;
    }
    case 0x2f: {                                                                                              // l.sf*i
      uint32_t a = rA(); int32_t sa = int32_t(a); uint32_t ub = uint32_t(sI); int32_t sb = sI;
      switch (D) {
      case 0x0: setFlag(a == ub); break; case 0x1: setFlag(a != ub); break; case 0x2: setFlag(a > ub); break;
      case 0x3: setFlag(a >= ub); break; case 0x4: setFlag(a < ub); break; case 0x5: setFlag(a <= ub); break;
      case 0xa: setFlag(sa > sb); break; case 0xb: setFlag(sa >= sb); break; case 0xc: setFlag(sa < sb); break; case 0xd: setFlag(sa <= sb); break;
      default: illegal(); break;
      }
      break;
    }
    case 0x30: sprWrite(rA() | (((w >> 21) & 31) << 11) | (w & 0x7ff), rB()); break;                           // l.mtspr
    case 0x31: {                                                                                              // l.mac / l.msb
      int64_t acc = (int64_t(machi_) << 32) | maclo_; int64_t p = int64_t(int32_t(rA())) * int32_t(rB());
      if ((w & 0xf) == 1) acc += p; else if ((w & 0xf) == 2) acc -= p; else { illegal(); break; }
      maclo_ = uint32_t(acc); machi_ = uint32_t(acc >> 32); break;
    }
    case 0x35: store(rA() + uint32_t(sext((((w >> 21) & 31) << 11) | (w & 0x7ff), 16)), 4, rB()); break;      // l.sw
    case 0x36: store(rA() + uint32_t(sext((((w >> 21) & 31) << 11) | (w & 0x7ff), 16)), 1, rB()); break;      // l.sb
    case 0x37: store(rA() + uint32_t(sext((((w >> 21) & 31) << 11) | (w & 0x7ff), 16)), 2, rB()); break;      // l.sh
    case 0x38: alu(w, D, A, B, illegal); break;
    case 0x39: {                                                                                              // l.sf*
      uint32_t a = rA(), b = rB(); int32_t sa = int32_t(a), sb = int32_t(b);
      switch (D) {
      case 0x0: setFlag(a == b); break; case 0x1: setFlag(a != b); break; case 0x2: setFlag(a > b); break;
      case 0x3: setFlag(a >= b); break; case 0x4: setFlag(a < b); break; case 0x5: setFlag(a <= b); break;
      case 0xa: setFlag(sa > sb); break; case 0xb: setFlag(sa >= sb); break; case 0xc: setFlag(sa < sb); break; case 0xd: setFlag(sa <= sb); break;
      default: illegal(); break;
      }
      break;
    }
    default: illegal(); break;
    }
  }
  void setFlagCyOv(uint32_t a, uint32_t b, uint32_t s, bool) {
    if (s < a) sr_ |= kCY; else sr_ &= ~kCY;
    if ((~(a ^ b) & (a ^ s)) >> 31) sr_ |= kOV; else sr_ &= ~kOV;
  }
  template <class Illegal> void alu(uint32_t w, unsigned D, unsigned A, unsigned B, Illegal illegal) {
    uint32_t a = r_[A], b = r_[B], res = 0;
    unsigned fn = w & 0xf, ext = (w >> 8) & 3, sub = (w >> 6) & 3;
    auto wr = [&](uint32_t v) { if (D) r_[D] = v; };
    switch (fn) {
    case 0x0: res = a + b; setFlagCyOv(a, b, res, false); wr(res); break;                 // l.add
    case 0x1: res = a + b + ((sr_ & kCY) ? 1 : 0); setFlagCyOv(a, b, res, false); wr(res); break;   // l.addc
    case 0x2: res = a - b; if (a < b) sr_ |= kCY; else sr_ &= ~kCY; wr(res); break;       // l.sub
    case 0x3: wr(a & b); break;
    case 0x4: wr(a | b); break;
    case 0x5: wr(a ^ b); break;
    case 0x6: wr(uint32_t(int32_t(a) * int32_t(b))); break;                               // l.mul
    case 0x8:
      switch (sub) {
      case 0: wr(a << (b & 31)); break;
      case 1: wr(a >> (b & 31)); break;
      case 2: wr(uint32_t(int32_t(a) >> (b & 31))); break;
      default: wr((b & 31) ? (a >> (b & 31)) | (a << (32 - (b & 31))) : a); break;
      }
      break;
    case 0x9: if (!b) { sr_ |= kOV; wr(0); } else if (int32_t(a) == INT32_MIN && int32_t(b) == -1) { sr_ |= kOV; wr(a); } else wr(uint32_t(int32_t(a) / int32_t(b))); break;   // l.div
    case 0xa: if (!b) { sr_ |= kOV; wr(0); } else wr(a / b); break;                       // l.divu
    case 0xb: wr(uint32_t((uint64_t(a) * b))); break;                                     // l.mulu
    case 0xc:
      switch (sub) {
      case 0: wr(uint32_t(int32_t(int16_t(a)))); break;   // l.exths
      case 1: wr(uint32_t(int32_t(int8_t(a)))); break;    // l.extbs
      case 2: wr(a & 0xffff); break;                      // l.exthz
      default: wr(a & 0xff); break;                       // l.extbz
      }
      break;
    case 0xd: wr(a); break;                               // l.extws / l.extwz
    case 0xe: wr((sr_ & kF) ? a : b); break;              // l.cmov
    case 0xf: if (ext == 0) { uint32_t v = 0; for (unsigned i = 0; i < 32; ++i) if (a >> i & 1) { v = i + 1; break; } wr(v); }   // l.ff1
              else { uint32_t v = 0; for (int i = 31; i >= 0; --i) if (a >> i & 1) { v = unsigned(i) + 1; break; } wr(v); } break;   // l.fl1
    default: illegal(); break;
    }
  }
};
}  // namespace manta
