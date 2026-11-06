// EXPLORATORY multimedia coprocessor (AEON / OpenRISC) running the ORIGINAL image that the original MIPS firmware loaded into DRAM
// (resource 61 for multimedia, resource 51 for DTV). The host does not select or replace the guest program.
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
#include "cpu/or1k_observations.hpp"
#include "peripherals/jpd.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
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
  // Coherent connection to the board's existing RIU devices. No value is
  // supplied unless their known-bit mask covers the complete read.
  std::function<std::optional<uint32_t>(uint32_t,unsigned)> peripheralRead;
  std::function<uint32_t(uint32_t,unsigned)> peripheralKnown;
  std::function<void(uint32_t,unsigned,uint32_t)> peripheralWrite;
  std::function<void(uint32_t,unsigned,uint32_t,uint32_t)> peripheralWriteKnown;
  bool deferIdentityCache=false;
  std::function<uint32_t(uint32_t,unsigned)> identityObservation;
  std::function<bool(uint32_t,unsigned)> ramKnown;
  std::function<void(uint32_t,unsigned,uint32_t)> ramUnknown;
  uint64_t deferredIdentityReads=0;
  bool trackUnknown=false;
  void knownSnapshot(snap::Ar& a){a.tag("cpknown");observations_.serialize(a);a(intcBitKnown_,ramMasks_,ramOrigins_,deferredIdentityReads);}
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
  bool jpdEnabled = false;            // EXPLORATORY JPEG decoder block model (peripherals/jpd.hpp)
  Jpd jpd;
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

  void rebind() {   // (re)attach the bus callbacks; used by start() and after a snapshot restore
    cpu_.read = [this](uint32_t a, unsigned n) {
      lastReadMask_=n==4?0xffffffffu:(1u<<(n*8))-1;lastReadOrigin_=0;
      auto value=busRead(a,n);
      if(trackUnknown && inInstruction_ && observations_.access.kind==1)observations_.loaded(lastReadMask_,lastReadOrigin_);
      return value;
    };
    cpu_.write = [this](uint32_t a, unsigned n, uint32_t v) { busWrite(a, n, v); };
    if(trackUnknown)cpu_.beforeInstruction=[this](uint32_t,uint32_t word){
      inInstruction_=true;observations_.begin(word,[this](unsigned r){return cpu_.reg(r);},(cpu_.sr()&Or1k::kF)!=0);
    };
    else cpu_.beforeInstruction={};
  }
  void serialize(snap::Ar& a) {   // snapshot state of the running coprocessor (DRAM is in the machine's own section)
    a.tag("copro");
    a(started, stopped, stopReason, uart, intc_, intcKnown_, irqLine2_, intcWrites_, mailboxIrqs, hostNotifies, latchedWrites, starts, holds, unknownReadTotal, dlab_,
      latch, unknownReads, mailLog, notes);
    cpu_.serialize(a);
    jpd.serialize(a);
    if (a.loading() && started) { rebind(); cpu_.bigEndian = false; cpu_.setLevel(2, irqLine2_); }
  }
  void start(uint32_t entry = 0x100) {   // 0x100 is the architectural OpenRISC reset vector
    rebind();
    cpu_.reset(entry);
    intc_.fill(0); intcKnown_.fill(0); latch.clear(); dlab_ = 0; irqLine2_ = false;
    ++starts;
    started = true;
    stopped = false;
    observations_.reset();intcBitKnown_.fill(0);inInstruction_=false;
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
        inInstruction_=false;
        if(trackUnknown && !(intcBitKnown_[0x329c-kIntcBase]&2) && (cpu_.sr()&Or1k::kIEE) && (cpu_.picmr()&4) && pending113())
          throw std::runtime_error("INVALID interrupt-mask bit affects a pending mailbox interrupt");
        ring_[ringPos_++ & 63] = cpu_.pc();
        if (cpu_.pc() >= ramSize) { stop("instruction fetch outside DRAM at " + hex8(cpu_.pc()) + "; r6=" + hex8(cpu_.reg(6)) + " r7=" + hex8(cpu_.reg(7)) + " r9=" + hex8(cpu_.reg(9)) + " r11=" + hex8(cpu_.reg(11)) + "; last pcs " + recentPcs()); return; }
        if (!cpu_.step()) { stop("halted"); return; }
        inInstruction_=false;
      }
    } catch (const std::exception& e) { inInstruction_=false;stop(e.what()); }
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
  Or1kObservations observations_;
  bool inInstruction_=false;
  uint32_t lastReadMask_=0xffffffffu;
  uint32_t lastReadOrigin_=0;
  std::array<uint8_t,kIntcEnd-kIntcBase> intcBitKnown_{};
  std::map<uint32_t,uint8_t> ramMasks_;
  std::map<uint32_t,uint32_t> ramOrigins_;
  bool identitySignature()const{
    static constexpr uint32_t words[]={0x18e00015,0xd5271b40,0x84e30198,0x18c00015,0xdd263b44,0x84e3019c,0x9cc64b44,0xd8063802,
      0x84e3019c,0xb8e70048,0xd8063803,0x18e00800,0xd4063804,0x18e04000,0x44004800,0xd4063808};
    return ram && ramSize>=0xc8494 && std::memcmp(ram+0xc8454,words,sizeof(words))==0;
  }
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
    bool maskKnown = trackUnknown?(intcBitKnown_[0x329C-kIntcBase]&2)!=0:intcKnown_[0x329C - kIntcBase] != 0;
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
      uint32_t mask=0,joinedOrigin=0;bool haveOrigin=false,conflictingOrigins=false;
      for(unsigned i=0;i<n;++i){
        auto it=ramMasks_.find(a+i);const bool externalClean=ramKnown && ramKnown(a+i,1);
        // The shared board observer has byte granularity. Another CPU may
        // have overwritten this byte since our last store, even with the
        // same concrete value. Never reuse a stale CP partial mask/origin
        // to promote any of that new invalid observation to known bits.
        uint8_t known=ramKnown?(externalClean?255:0):it!=ramMasks_.end()?it->second:255;
        mask|=uint32_t(known)<<(8*i);
        if(known!=255){auto origin=ramOrigins_.find(a+i);const uint32_t o=ramKnown || origin==ramOrigins_.end()?0:origin->second;if(!haveOrigin){joinedOrigin=o;haveOrigin=true;}else if(joinedOrigin!=o)conflictingOrigins=true;}
      }
      lastReadOrigin_=conflictingOrigins?0:joinedOrigin;
      const uint32_t full=n==4?0xffffffffu:(1u<<(n*8))-1;
      if(mask!=full && (!trackUnknown || !inInstruction_ || observations_.access.kind!=1))
        throw std::runtime_error("coprocessor instruction/read of INVALID cached RAM at "+hex8(a));
      lastReadMask_=mask;
      uint32_t v = 0;
      std::memcpy(&v, ram + a, n);
      return v;
    }
    if (a >= 0x90000000u && a < 0x90000008u) return (a & 7) == 5 ? 0x60u : (a & 7) == 2 ? 0x01u : 0u;
    if (a >= kWindow && a < kWindow + kWindowSize) {
      uint32_t off = a - kWindow, v = 0;
      if (jpdEnabled && jpd.owns(off, n)) { v = jpd.read(off, n); ioNote(a, false, v); return v; }
      if (off >= kMailBase && off + n <= kMailEnd) {
        lastReadMask_=0;lastReadOrigin_=0x1f200000u+off;
        for (unsigned i = 0; i < n; ++i) {
          uint8_t byte=0;
          if(mailRead && mailRead(0x1f200000u+off+i,byte)){
            v|=uint32_t(byte)<<(8*i);lastReadMask_|=255u<<(8*i);
          }else if(trackUnknown && peripheralRead){
            const auto value=peripheralRead(off+i,1);
            if(!value)fail("undefined mailbox read",a,n);
            v|=(*value&255u)<<(8*i);
            lastReadMask_|=(peripheralKnown?peripheralKnown(off+i,1)&255u:0u)<<(8*i);
          }else v|=uint32_t(mailByte(off+i))<<(8*i);
        }
        return v;
      }
      if (off >= kIntcBase && off + n <= kIntcEnd) {
        if (isStatus(off)) {
          for (unsigned i = 0; i < n; ++i) {
            uint32_t o = off + i;
            // 0x32BC bit 1 (hardware interrupt 113): MIPS->AEON mailbox pending. Every other status bit has no modelled source.
            uint8_t b = (o == 0x32BC && pending113()) ? 2 : 0;
            // With the connected RIU fabric, pending status is derived from
            // the configured mailbox source. Other devices have no wired CP
            // interrupt output; this is a mailbox-only fabric, not a read-zero
            // fallback for unknown registers.
            if (o != 0x32BC && !zeroUnknownReads && !trackUnknown) fail("status read", a, n);
            v |= uint32_t(b) << (8 * i);
          }
          return v;
        }
        lastReadMask_=0;lastReadOrigin_=0x1f200000u+off;
        for (unsigned i = 0; i < n; ++i) {
          uint32_t o = off + i - kIntcBase;
          if(trackUnknown){
            lastReadMask_|=uint32_t(intcBitKnown_[o])<<(8*i);v|=uint32_t(intc_[o])<<(8*i);continue;
          }
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
    if(a>=kWindow && a<kWindow+kWindowSize && peripheralRead){
      if(auto value=peripheralRead(a-kWindow,n)){lastReadMask_=peripheralKnown?peripheralKnown(a-kWindow,n):lastReadMask_;lastReadOrigin_=0x1f200000u+a-kWindow;ioNote(a,false,*value);return *value;}
      const auto pc=cpu_.pc();
      if(trackUnknown && deferIdentityCache && identityObservation && identitySignature() && n==4 && cpu_.reg(3)==kWindow &&
          ((pc==0xc845c && a==kWindow+0x198) || ((pc==0xc8468 || pc==0xc8474) && a==kWindow+0x19c))){
        lastReadMask_=0;lastReadOrigin_=0x1f200000u+a-kWindow;++deferredIdentityReads;
        return identityObservation(a-kWindow,n); // INVALID, only the proved cache stores may receive it
      }
      fail("shared RIU read",a,n); // never fall through to the blanket-zero diagnostic
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
    const uint32_t full=n==4?0xffffffffu:(1u<<(n*8))-1;
    const uint32_t known=trackUnknown && inInstruction_ && observations_.access.kind==2?observations_.access.known&full:full;
    if (a < ramSize) {
      if (uint64_t(a) + n > ramSize) fail("write", a, n);
      std::memcpy(ram + a, &v, n);
      if (watchAddr && a < watchAddr + 4 && a + n > watchAddr) {
        uint32_t cur; std::memcpy(&cur, ram + watchAddr, 4); watchLast_ = cur;
        note("cp wrote " + hex8(a) + "/" + std::to_string(n) + " = " + hex8(v) + " at pc " + hex8(cpu_.pc()) + " cp instr " + std::to_string(cpu_.instructions()));
      }
      if (ramWritten) ramWritten(a, n);
      if(trackUnknown)for(unsigned i=0;i<n;++i){
        const auto mask=uint8_t(known>>(8*i));
        if(mask==255){ramMasks_.erase(a+i);ramOrigins_.erase(a+i);}
        else{ramMasks_[a+i]=mask;ramOrigins_[a+i]=observations_.access.origin;if(ramUnknown)ramUnknown(a+i,1,observations_.access.origin);}
      }
      return;
    }
    const auto off=a-kWindow;
    const bool interruptMask=a>=kWindow+kIntcBase && uint64_t(a)+n<=kWindow+kIntcEnd && !isStatus(off);
    if(known!=full && !interruptMask){
      if(peripheralWriteKnown && a>=kWindow && a<kWindow+kWindowSize && observations_.access.origin==0x1f200000u+off){
        peripheralWriteKnown(off,n,v,known);return;
      }
      throw std::runtime_error("INVALID coprocessor bits used in a foreign peripheral write");
    }
    if (a >= 0x90000000u && a < 0x90000008u) {   // 16550: LCR bit 7 (DLAB) redirects offset 0/1 to the divisor latch
      if ((a & 7) == 3) dlab_ = v & 0x80;
      else if ((a & 7) == 0 && !dlab_) uart.push_back(char(v));
      return;
    }
    if (a >= kWindow && a < kWindow + kWindowSize) {
      uint32_t off = a - kWindow;
      if (jpdEnabled && jpd.owns(off, n)) { ioNote(a, true, v); jpd.write(off, n, v); return; }
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
        for (unsigned i = 0; i < n; ++i) { const auto mask=uint8_t(known>>(8*i));intc_[off + i - kIntcBase] = uint8_t(v >> (8 * i)); intcKnown_[off + i - kIntcBase] = mask==255;intcBitKnown_[off+i-kIntcBase]=mask; }
        updateIrq();
        return;
      }
      if (off == 0x0A88 && n == 2) {   // HYPOTHESIS: AEON->host interrupt strobe (written 4 then 0 right after a mailbox reply); only counted
        ++hostNotifies;
        if (event) event("copro_host_notify", a, n, v, "HYPOTHESIS: unverified write observed right after the CP mailbox reply");
        return;
      }
      if(peripheralWrite){ioNote(a,true,v);peripheralWrite(off,n,v);return;}
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
