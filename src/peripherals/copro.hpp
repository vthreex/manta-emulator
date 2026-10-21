// EXPLORATORY multimedia coprocessor (AEON / OpenRISC) running the ORIGINAL resource-61 image that the original MIPS firmware loaded into DRAM.
// Nothing is supplied for hardware whose behaviour is unknown: an access outside the modelled set stops the coprocessor (strict) unless the
// labelled `zeroUnknownReads` hypothesis is on, in which case unknown reads return 0 and are listed in the summary.
//
// Modelled, with the evidence for each in docs/multimedia-copro-or1k.md:
//  * DRAM: CP physical address == MIPS physical address (low 128 MiB), shared with the MIPS engine.
//  * 16550-style UART at 0x90000000 (THR text captured; LSR reports ready).
//  * RIU window at 0xA0200000+off == MIPS RIU 0x1F200000+off. The mailbox bytes 0x6700..0x677F are the SAME storage the MIPS firmware uses.
//  * Interrupt controller words 0x3290..0x32FF: mask words are plain storage (a cleared bit enables, as the firmware's own unmask code does);
//    the only modelled source is the MIPS->AEON mailbox: hardware interrupt 113 (status word 0x32B8/0x32BC bit 17) = mailbox 0 control bit 0 (FIRE).
//    Its output is PIC line 2, level sensitive. All other status bits read as unknown.
#pragma once
#include "cpu/or1k.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
class Copro {
public:
  static constexpr uint32_t kWindow = 0xA0200000u, kWindowSize = 0x100000u;
  static constexpr uint32_t kMailBase = 0x6700u, kMailEnd = 0x6800u;
  static constexpr uint32_t kIntcBase = 0x3290u, kIntcEnd = 0x3300u;
  uint8_t* ram = nullptr;
  size_t ramSize = 0;
  bool zeroUnknownReads = false;                                          // LABELLED HYPOTHESIS: unknown registers read as 0
  std::function<bool(uint32_t, uint8_t&)> mailRead;                        // shared RIU byte storage (known bytes only)
  std::function<void(uint32_t, uint8_t)> mailWrite;
  std::function<void(uint32_t, size_t)> ramWritten;                        // bookkeeping hook for CP stores into DRAM
  std::function<void(const std::string&, uint32_t, unsigned, uint32_t, const std::string&)> event;
  std::string uart, stopReason;
  std::map<uint32_t, uint64_t> unknownReads;                               // address -> count (zeroUnknownReads only)
  std::function<void()> onReply;   // the coprocessor fired its reply mailbox (mailbox 1 control bit 0 set)
  struct IoStat { uint64_t reads = 0, writes = 0; uint32_t last = 0, firstPc = 0; };
  std::map<uint32_t, IoStat> ioLog;   // every other peripheral-window / 0xB0000000 access by address (capped at 600 distinct addresses)
  void ioNote(uint32_t a, bool write, uint32_t v) {
    seqNote(a, write, v);
    if (ioLog.size() >= 600 && !ioLog.count(a)) return;
    auto& s = ioLog[a];
    if (!s.reads && !s.writes) s.firstPc = cpu_.pc();
    (write ? s.writes : s.reads)++;
    s.last = v;
  }
  uint32_t captureAddr = 0;           // diagnostic: record the sequence of values the coprocessor writes to this peripheral address (first 16384)
  std::vector<uint32_t> captured;
  uint32_t seqLo = 0, seqHi = 0;      // diagnostic: ordered access sequence (kind, address, value, pc) for peripheral addresses in [seqLo, seqHi), first 6000
  std::vector<std::array<uint32_t, 4>> seq;
  void seqNote(uint32_t a, bool write, uint32_t v) { if (seqHi && a >= seqLo && a < seqHi && seq.size() < 6000) seq.push_back({write ? 1u : 0u, a, v, cpu_.pc()}); }
  uint32_t watchAddr = 0;   // diagnostic only
  std::vector<std::string> notes, mailLog;   // mailLog: every MIPS->CP fire and CP->MIPS reply (first 600)
  void logMail(const std::string& m) { if (mailLog.size() < 600) mailLog.push_back(m); }
  std::string mailDesc(uint32_t base) {
    std::string o;
    for (uint32_t k : {1u, 4u, 5u}) { uint8_t v = 0; mailRead && mailRead(0x1f200000u + base + k, v); o += (k == 1 ? "cls=" : k == 4 ? " cmd=" : " n=") + hex8(v).substr(6); }
    o += " p="; for (uint32_t k = 8; k < 28; k += 2) { uint8_t v = 0; mailRead && mailRead(0x1f200000u + base + k, v); o += hex8(v).substr(6); }
    return o;
  }
  void note(const std::string& m) { if (notes.size() < 64) notes.push_back(m); }
  uint64_t starts = 0, holds = 0;
  uint64_t unknownReadTotal = 0;
  uint64_t mailboxIrqs = 0, hostNotifies = 0, latchedWrites = 0;
  std::map<uint32_t, uint8_t> latch;   // writes to registers whose effect is unknown: stored so the CP reads back what it wrote ("write latch only")
  bool started = false, stopped = false;

  void start(uint32_t entry = 0x100) {   // 0x100 is the architectural OpenRISC reset vector
    cpu_.read = [this](uint32_t a, unsigned n) { return busRead(a, n); };
    cpu_.write = [this](uint32_t a, unsigned n, uint32_t v) { busWrite(a, n, v); };
    cpu_.reset(entry);
    intc_.fill(0); intcKnown_.fill(0); latch.clear(); dlab_ = 0; irqLine2_ = false;
    ++starts;
    started = true;
    stopped = false;
    stopReason.clear();
    updateIrq();
  }
  // Run up to `budget` instructions; stops early (and permanently) at the first unmodelled access.
  void run(uint64_t budget) {
    if (!started || stopped) return;
    if (watchAddr && ram) {   // diagnostic: report changes to a watched DRAM word made by anyone but the coprocessor
      uint32_t cur; std::memcpy(&cur, ram + watchAddr, 4);
      if (cur != watchLast_) {
        note("host changed " + hex8(watchAddr) + " " + hex8(watchLast_) + " -> " + hex8(cur) + " before cp instr " + std::to_string(cpu_.instructions()));
        watchLast_ = cur;
      }
    }
    try {
      for (uint64_t i = 0; i < budget; ++i) {
        ring_[ringPos_++ & 63] = cpu_.pc();
        if (cpu_.pc() >= ramSize) { stop("instruction fetch outside DRAM at " + hex8(cpu_.pc()) + "; r6=" + hex8(cpu_.reg(6)) + " r7=" + hex8(cpu_.reg(7)) + " r9=" + hex8(cpu_.reg(9)) + " r11=" + hex8(cpu_.reg(11)) + "; last pcs " + recentPcs()); return; }
        if (!cpu_.step()) { stop("halted"); return; }
      }
    } catch (const std::exception& e) { stop(e.what()); }
  }
  // The guest wrote a reset-control value other than "run": the core stops executing until the next start (state is rebuilt by start()).
  void hold(uint32_t value) {
    if (!started) return;
    ++holds;
    note("reset-control write " + hex8(value) + " at cp instr " + std::to_string(cpu_.instructions()) + ": coprocessor held");
    stopped = true;
    stopReason = "held in reset by the guest";
  }
  // The MIPS side stored to the shared mailbox window or the reset-control byte.
  void mipsWrote(uint32_t riuAddr, unsigned n, uint32_t v) {
    if (!started) return;
    if (riuAddr < 0x1f200000u + kMailEnd && riuAddr + n > 0x1f200000u + kMailBase) {
      if (riuAddr <= 0x1f206700u && riuAddr + n > 0x1f206700u && ((v >> (8 * (0x1f206700u - riuAddr))) & 1)) logMail("MIPS fire @cp" + std::to_string(cpu_.instructions()) + " " + mailDesc(0x6700));
      if (riuAddr >= 0x1f206704u) logMail("MIPS wrote " + hex8(riuAddr) + "/" + std::to_string(n) + "=" + hex8(v) + " @cp" + std::to_string(cpu_.instructions()));
      updateIrq();
    }
    (void)v;
  }
  uint64_t instructions() const { return cpu_.instructions(); }
  uint32_t pc() const { return cpu_.pc(); }
  bool irqLine2() const { return irqLine2_; }
  uint64_t intcWrites() const { return intcWrites_; }

private:
  Or1k cpu_;
  bool irqLine2_ = false;
  uint64_t intcWrites_ = 0;
  std::array<uint8_t, kIntcEnd - kIntcBase> intc_{}, intcKnown_{};
  std::array<uint32_t, 64> ring_{};
  uint64_t ringPos_ = 0;
  uint32_t watchLast_ = 0;
  uint32_t dlab_ = 0;
  static std::string hex8(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "%08x", v); return b; }
  std::string recentPcs() const {
    std::string o;
    for (uint64_t k = ringPos_ > 24 ? ringPos_ - 24 : 0; k < ringPos_; ++k) o += hex8(ring_[k & 63]) + " ";
    return o;
  }

  void stop(const std::string& why) {
    stopped = true;
    stopReason = why;
    if (event) event("copro_stop", cpu_.pc(), 4, uint32_t(cpu_.instructions()), why);
  }
  [[noreturn]] void fail(const char* what, uint32_t a, unsigned n) {
    char b[96];
    std::snprintf(b, sizeof b, "unmodelled %s%u at %08x (pc %08x)", what, n, a, cpu_.pc());
    throw std::runtime_error(b);
  }
  void countUnknown(uint32_t a) { ++unknownReadTotal; if (unknownReads.size() < 512 || unknownReads.count(a)) ++unknownReads[a]; }
  static bool isStatus(uint32_t off) { return (off >= 0x32B0 && off < 0x32C0) || (off >= 0x32F0 && off < 0x3300); }
  uint8_t mailByte(uint32_t off) {
    uint8_t v = 0;
    if (!mailRead || !mailRead(0x1f200000u + off, v)) throw std::runtime_error("mailbox byte " + std::to_string(off) + " read before any side defined it");
    return v;
  }
  bool pending113() {
    uint8_t v = 0;
    return mailRead && mailRead(0x1f200000u + kMailBase, v) && (v & 1);
  }
  void updateIrq() {
    // enabled when the firmware cleared the mask bit (mask word 0x3298/0x329C, bit 17 = upper half bit 1)
    bool maskKnown = intcKnown_[0x329C - kIntcBase] != 0;
    bool masked = !maskKnown || (intc_[0x329C - kIntcBase] & 2);
    bool level = pending113() && !masked;
    if (level && !irqLine2_) ++mailboxIrqs;
    irqLine2_ = level;
    cpu_.setLevel(2, level);
  }
  // ASSUMPTION (labelled): 0x80000000..0x87FFFFFF is a cached alias of the same DRAM (KSEG0-style); the shared-memory structures the MIPS firmware
  // publishes (e.g. the ImageDecoder version block at 0x808AC0F0) are addressed that way by the coprocessor firmware.
  static uint32_t unalias(uint32_t a) { return (a >= 0x80000000u && a < 0x88000000u) ? a - 0x80000000u : a; }
  uint32_t busRead(uint32_t a, unsigned n) {
    a = unalias(a);
    if (a < ramSize) {
      if (uint64_t(a) + n > ramSize) fail("read", a, n);
      uint32_t v = 0;
      std::memcpy(&v, ram + a, n);
      return v;
    }
    if (a >= 0x90000000u && a < 0x90000008u) return (a & 7) == 5 ? 0x60u : (a & 7) == 2 ? 0x01u : 0u;
    if (a >= kWindow && a < kWindow + kWindowSize) {
      uint32_t off = a - kWindow, v = 0;
      if (off >= kMailBase && off + n <= kMailEnd) {
        for (unsigned i = 0; i < n; ++i) v |= uint32_t(mailByte(off + i)) << (8 * i);
        return v;
      }
      if (off >= kIntcBase && off + n <= kIntcEnd) {
        if (isStatus(off)) {
          for (unsigned i = 0; i < n; ++i) {
            uint32_t o = off + i;
            // 0x32BC bit 1 (hardware interrupt 113): MIPS->AEON mailbox pending. Every other status bit has no modelled source.
            uint8_t b = (o == 0x32BC && pending113()) ? 2 : 0;
            if (o != 0x32BC && !zeroUnknownReads) fail("status read", a, n);
            v |= uint32_t(b) << (8 * i);
          }
          return v;
        }
        for (unsigned i = 0; i < n; ++i) {
          uint32_t o = off + i - kIntcBase;
          if (!intcKnown_[o]) {
            if (!zeroUnknownReads) fail("mask read", a, n);
            countUnknown(a + i);
            continue;
          }
          v |= uint32_t(intc_[o]) << (8 * i);
        }
        return v;
      }
    }
    {
      uint32_t v = 0;
      bool all = n > 0;
      for (unsigned i = 0; i < n && all; ++i) {
        auto it = latch.find(a + i);
        if (it == latch.end()) all = false; else v |= uint32_t(it->second) << (8 * i);
      }
      if (all) { ioNote(a, false, v); return v; }
    }
    if (zeroUnknownReads) { countUnknown(a); ioNote(a, false, 0); return 0; }
    fail("read", a, n);
  }
  void busWrite(uint32_t a, unsigned n, uint32_t v) {
    a = unalias(a);
    if (a < ramSize) {
      if (uint64_t(a) + n > ramSize) fail("write", a, n);
      std::memcpy(ram + a, &v, n);
      if (watchAddr && a < watchAddr + 4 && a + n > watchAddr) {
        uint32_t cur; std::memcpy(&cur, ram + watchAddr, 4); watchLast_ = cur;
        note("cp wrote " + hex8(a) + "/" + std::to_string(n) + " = " + hex8(v) + " at pc " + hex8(cpu_.pc()) + " cp instr " + std::to_string(cpu_.instructions()));
      }
      if (ramWritten) ramWritten(a, n);
      return;
    }
    if (a >= 0x90000000u && a < 0x90000008u) {   // 16550: LCR bit 7 (DLAB) redirects offset 0/1 to the divisor latch
      if ((a & 7) == 3) dlab_ = v & 0x80;
      else if ((a & 7) == 0 && !dlab_) uart.push_back(char(v));
      return;
    }
    if (a >= kWindow && a < kWindow + kWindowSize) {
      uint32_t off = a - kWindow;
      if (off >= kMailBase && off + n <= kMailEnd) {
        for (unsigned i = 0; i < n; ++i) if (mailWrite) mailWrite(0x1f200000u + off + i, uint8_t(v >> (8 * i)));
        if (off == 0x6720 && (v & 1) && onReply) onReply();
        if (off == 0x6720 && (v & 1)) logMail("CP reply @cp" + std::to_string(cpu_.instructions()) + " ctrl=" + hex8(v & 0xff).substr(6) + " " + mailDesc(0x6720));
        if (off == 0x6700) logMail("CP wrote ctrl0=" + hex8(v & 0xff).substr(6) + " @cp" + std::to_string(cpu_.instructions()));
        updateIrq();
        return;
      }
      if (off >= kIntcBase && off + n <= kIntcEnd) {
        ++intcWrites_;
        if (isStatus(off)) return;   // status write semantics unverified: ignored (the only modelled source is level-derived)
        for (unsigned i = 0; i < n; ++i) { intc_[off + i - kIntcBase] = uint8_t(v >> (8 * i)); intcKnown_[off + i - kIntcBase] = 1; }
        updateIrq();
        return;
      }
      if (off == 0x0A88 && n == 2) {   // HYPOTHESIS: AEON->host interrupt strobe (written 4 then 0 right after a mailbox reply); only counted
        ++hostNotifies;
        if (event) event("copro_host_notify", a, n, v, "HYPOTHESIS: unverified write observed right after the CP mailbox reply");
        return;
      }
    }
    if ((a >= kWindow && a < kWindow + kWindowSize) || (a >= 0xB0000000u && a < 0xB0001000u)) {
      for (unsigned i = 0; i < n; ++i) latch[a + i] = uint8_t(v >> (8 * i));
      ++latchedWrites;
      ioNote(a, true, v);
      if (captureAddr && a == captureAddr && captured.size() < 16384) captured.push_back(v);
      return;
    }
    fail("write", a, n);
  }
};
}  // namespace manta
