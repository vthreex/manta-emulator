#include "peripherals/riu.hpp"
#include <array>
#include <stdexcept>
namespace manta {
bool Riu::beginCombRestore(uint32_t a,uint32_t unknown) {
  if(!exploratoryCombRestore || !symbolicInit || combRestore_ || !combBank() ||
      (unknown&255)!=(unknown>>8) || !unknown || unknown>0xffff ||
      knownBits(a,1)!=255) return false;
  auto off=a-0x1f206c00;
  if(off!=0x8c && off!=0xbc && off!=0x11c && off!=0x1ac && off!=0x1bc)return false;
  combRestore_=CombRestore{a,uint8_t(*combBank()),uint8_t(get(a,1)),unknown};
  event("exploratory_comb_restore_begin",a,2,unknown,
    "HYPOTHETICAL side-effect-free T4 paired write; unknown high bits stay invalid, temporary low bits quarantined until original restore");
  return true;
}
uint32_t Riu::preservedStoreKey(uint32_t a) const {
  if(combRestore_ && !combRestore_->written && a==combRestore_->address)
    return storageKey(a+1);
  return storageKey(a);
}
std::optional<PanelTiming> Riu::panelTiming() const {
  auto word=[&](unsigned off,unsigned mask)->std::optional<unsigned> {
    auto l=scByte(0x10,off),h=scByte(0x10,off+1);
    unsigned known=l.known | (unsigned(h.known)<<8);
    if((known&mask)!=mask) return {};
    return (l.value | (unsigned(h.value)<<8))&mask;
  };
  auto ht=word(0x30,0xfff),vt=word(0x34,0xfff);
  auto hs=word(0x10,0xfff),he=word(0x14,0xfff),vs=word(0x18,0xfff),ve=word(0x1c,0xfff);
  auto ss=word(8,0xfff),se=word(12,0xfff),inv=word(0x40,0x8000);
  if(!ht || !vt || !hs || !he || !vs || !ve || !ss || !se || !inv) return {};
  return PanelTiming{uint16_t(*ht+1),uint16_t(*vt+1),uint16_t(*hs),uint16_t(*he),uint16_t(*vs),uint16_t(*ve),uint16_t(*ss),uint16_t(*se),bool(*inv)};
}
void Riu::updateOutputTiming(uint64_t tick) {
  if(!exploratoryDisplayClock) return;
  if(auto t=panelTiming(); t && (knownBits(0x1f20174c,1)&1))
    { if(scaler_.program(*t,!(get(0x1f20174c,1)&1),tick))
      event("exploratory_panel_timing",0x1f205e00,0,uint32_t(exploratoryDisplayClock),"HYPOTHETICAL output clock; pulse phase begins on original geometry/clock programming, never on a read");
    }
  else scaler_.invalidate();
}
bool Riu::gopBankKnown() const {
  auto it = bytes_.find(0x1f203ffc);
  return it != bytes_.end() && (it->second.known & 15) == 15;
}
uint8_t Riu::gopBank() const {
  if (!gopBankKnown()) throw std::runtime_error("GOP access before bank selector is known");
  return bytes_.at(0x1f203ffc).value & 15;
}
RiuByte Riu::gopByte(unsigned bank, unsigned offset) const {
  if (bank >= 16 || offset >= 0x1fc) throw std::runtime_error("GOP bank/register out of bounds");
  auto it = bytes_.find(0x80000000u + (bank << 10) + offset);
  return it == bytes_.end() ? RiuByte{} : it->second;
}
uint32_t Riu::storageKey(uint32_t a) const {
  if (a >= 0x1f203e00 && a < 0x1f203ffc)
    return 0x80000000u + (uint32_t(gopBank()) << 10) + a - 0x1f203e00;
  if (a >= 0x1f205e04 && a < 0x1f206000) {
    auto selector = bytes_.find(0x1f205e00);
    if (selector == bytes_.end() || selector->second.known != 255)
      throw std::runtime_error("SC access before bank selector is known");
    return 0x81000000u + (uint32_t(selector->second.value) << 10) + a - 0x1f205e00;
  }
  if(a>=0x1f206c04 && a<0x1f206e00) {
    auto bank=combBank();
    if(!bank)throw std::runtime_error("COMB access before bank selector is known");
    return 0x82000000u+(uint32_t(*bank)<<10)+a-0x1f206c00;
  }
  return a;
}
uint32_t Riu::knownBits(uint32_t a, unsigned n) const {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(storageKey(a + i));
    if (it != bytes_.end())
      v |= uint32_t(it->second.known) << (8 * i);
  }
  return v;
}
uint32_t Riu::get(uint32_t a, unsigned n) const {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(storageKey(a + i));
    v |= uint32_t(it == bytes_.end() ? 0xa5 : it->second.value) << (i * 8);
  }
  return v;
}
void Riu::put(uint32_t a, unsigned n, uint32_t v) {
  for (unsigned i = 0; i < n; ++i)
    bytes_[storageKey(a + i)] = {uint8_t(v >> (i * 8)), 255};
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
  bool masked = consumedRead_.contains(a);
  auto provenConsumed = masked ? consumedRead_.at(a) : 0;
  consumedRead_.clear(); // one-shot proof, including peripheral early returns
  if (n != 1 && n != 2 && n != 4)
    throw std::runtime_error("Unsupported RIU access width");
  bool opaque=opaqueRead_ && *opaqueRead_==a;
  opaqueRead_.reset();
  if(combRestore_ && !(combRestore_->written && a==combRestore_->address && n==1 &&
      pc==0x805f8860 && pendingRmw_.contains(a) && pendingRmw_.at(a)==255)) {
    stopReason="COMB temporary unknown bits quarantined before original low-byte restore";
    event("blocked_read",a,n,0,stopReason);return 0;
  }
  if(candidateSemaphore && HardwareSemaphore::aperture(a)) {
    if(auto v=semaphore_.owner(a,n)) {
      event("semaphore_read",a,n,*v,"hardware resource owner, not ordinary writable storage");return *v;
    }
    stopReason="semaphore initial resource ownership unknown";
    event("blocked_read",a,n,0xa5a5,stopReason);return 0xa5a5;
  }
  if(opaque && symbolicInit) {
    auto v=get(a,n);
    event("read",a,n,v,"INVALID one-shot sample; machine bit-provenance guard rejects first decision/new-device use");
    return v;
  }
  if (exploratoryDisplayClock && a==0x1f205e78 && n==1 && (scBank()==1 || scBank()==3) && masked && provenConsumed==1) {
    if (scaler_.configured()) {
      if(auto state=scaler_.outputVsync(tick)) {
        ++outputSyncReads_;
        if(lastOutputSync_ && *lastOutputSync_!=*state) {
          ++outputSyncTransitions_;
          event("exploratory_output_vsync_edge",a,n,*state,"virtual scan counter edge, not read-driven; exact Titania4 monitor polarity remains unverified");
        }
        lastOutputSync_=*state;
        auto value=(get(a,n)&~1u)|*state;
        event("read",a,n,value,"EXPLORATORY output-VSYNC bit only; known clock gate, guest-programmed geometry, explicit clock-rate assumption");
        return value;
      }
    }
    stopReason="exploratory output sync requires complete guest timing and enabled display clock";
    event("blocked_read",a,n,0xa5,stopReason); return 0xa5;
  }
  if(exploratoryDlcTimeout && a==0x1f205e10 && n==1 && scBank()==0x1a && masked && provenConsumed==8) {
    auto v=get(a,n)&~8u;
    event("read",a,n,v,"EXPLORATORY DLC completion held low to exercise stock timeout, not hardware completion");
    return v;
  }
  if (candidateChipId && a == 0x1f003d98 && n == 2) {
    event("read", a, n, 0x18, "candidate Titania4 immutable chip identity inferred from original HAL check at 8054CDC4; silicon capture unverified");
    return 0x18;
  }
  if (candidateSpi && n == 2 && a >= 0x1f001000 && a < 0x1f001100) {
    auto value = spi_.read(a - 0x1f001000, mask, tick);
    if (value) {
      event("read", a, n, *value,
            "SPI ISP command/read FIFO state; only statically proven consumed bits returned; "
            "configured " + std::string(spi_.nor.name()));
      return *value;
    }
  }
  if (candidateUtmi && n == 1 && (a == 0x1f207578 || a == 0x1f207478) && mask == 2) {
    auto done = utmi_[a == 0x1f207478].done(tick);
    auto value = (get(a, 1) & ~2u) | (done ? 2u : 0u);
    event("read", a, n, value,
          "only CA_END consumed; derived from guest clock/start/hold/release sequence; analogue "
          "calibration data unknown");
    return value;
  }
  if (virtualRtc && n == 2 &&
      (a == 0x1f002420 || a == 0x1f002424 || a == 0x1f002620 || a == 0x1f002624)) {
    auto v = rtc_[a >= 0x1f002600].snapshot((a & 255) == 0x24);
    if (v) {
      event("read", a, n, *v,
            "RTC snapshot from guest divider/control commands; virtual clock pacing");
      return *v;
    }
    stopReason = "RTC counter read before guest snapshot command";
    event("blocked_read", a, n, 0xa5a5, stopReason);
    return 0xa5a5;
  }
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
  bool known = knownBits(a, n) == (n == 4 ? 0xffffffffu : (1u << (n * 8)) - 1);
  const bool runtimeRmw = pc == 0x8055b578 || pc == 0x8055b584 || pc == 0x803bd6f4 ||
                          pc == 0x803bd754 || pc == 0x803bd2e4 || pc == 0x8053bbdc ||
                          pc == 0x8053bc08 || pc == 0x8053bd18 || pc == 0x8053bc84 ||
                          pc == 0x8053bcb0 || pc == 0x8060a780 || pc == 0x8060a784;
  const bool rmw = pc == 0xbfc00c60 || pc == 0xbfc02804 || pc == 0xbfc02818 || pc == 0xbfc0282c ||
                   pc == 0xbfc02858 || pc == 0x81000408 || runtimeRmw;
  auto v = get(a, n);
  if (candidateGamma && a == 0x1f205fb4 && n == 1 && get(0x1f205e00, 1) == 0x10) {
    auto before = gamma_.commits;
    auto &b = bytes_[storageKey(a)];
    b.value = uint8_t((b.value & ~0xe0) | gamma_.status(tick)); b.known |= 0xe0;
    v = b.value;
    if (gamma_.commits != before)
      event("gamma_commit", a, 1, uint32_t(gamma_.commits), "original guest-programmed gamma RAM transaction committed");
  }
  if (masked) {
    if ((knownBits(a, n) & provenConsumed) == provenConsumed) {
      event("read", a, n, v, "only known bits consumed by byte-verified immediate destructive ANDI; other bits remain unknown");
      return v;
    }
  }
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
  bool closingComb=false;
  if(combRestore_) {
    auto &c=*combRestore_;
    if(combBank()!=c.bank || a!=c.address ||
       (!c.written && (n!=2 || pc!=0x805f883c || (v&255)!=((v>>8)&255))) ||
       (c.written && (n!=1 || pc!=0x805f887c || uint8_t(v)!=c.low))) {
      stopReason="COMB paired-write experiment requires original duplicate then exact low-byte restore";
      event("blocked_write",a,n,v,stopReason);return;
    }
    if(!c.written) {
      // Unlike a same-origin RMW, this transfers unknown bits into previously
      // known low bits. They must become invalid, never poison marked known.
      bytes_[storageKey(a)].known &= uint8_t(~c.unknown);
      c.written=true;
    } else closingComb=true;
  }
  if(candidateSemaphore && HardwareSemaphore::aperture(a)) {
    if(!semaphore_.write(a,n,uint16_t(v))) {
      stopReason="semaphore acquire cannot decide unknown initial ownership";
      event("blocked_write",a,n,v,stopReason);return;
    }
    event("semaphore_write",a,n,v,"zero releases; acquire retains occupied owner; original guest must read back resource ID");
    return;
  }
  // Straight-line RMW updates retain unknown-bit masks. No status poll is
  // guessed.
  if(auto it=preservedUnknownWrite_.find(a);it!=preservedUnknownWrite_.end()) {
    for(unsigned i=0;i<n;++i)bytes_[storageKey(a+i)].known &= uint8_t(~(it->second>>(i*8)));
    preservedUnknownWrite_.erase(it);
  }
  if (symbolicInit && pendingRmw_.contains(a)) {
    auto forced = pendingRmw_.at(a);
    for (unsigned i = 0; i < n; ++i) {
      auto &b = bytes_[storageKey(a + i)];
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
  if(a<=0x1f205e10 && a+n>0x1f205e10 && scBank()==0x1a) {
    // A command write does not prove the asynchronous capture completion.
    // Keep status bit 3 invalid until a real producer is established.
    bytes_[storageKey(0x1f205e10)].known &= uint8_t(~8u);
    if((knownBits(0x1f205e10,1)&4) && (get(0x1f205e10,1)&4))
      event("dlc_capture_request",0x1f205e10,1,v,"original capture-range request; completion/latch domain unverified");
  }
  // Bank-select writes do not manufacture interrupt or hardware write-ack
  // status. These fields have no producer model yet and remain unknown.
  if (a <= 0x1f203ffd && a + n > 0x1f203ffc) {
    for (unsigned i=0; i<n; ++i)
      if (a+i == 0x1f203ffc || a+i == 0x1f203ffd)
        bytes_[a+i].known &= 0x0f;
  }
  event("write", a, n, v, "recorded register storage; not a complete device");
  if(closingComb) {
    ++combRestoreCount_;
    event("exploratory_comb_restore_end",a,1,v,
      "original low snapshot restored; high preserved bits remain unknown; analogue side effects of intermediate write not validated");
    combRestore_.reset();
  }
  if(exploratoryDisplayClock && ((a>=0x1f205e04 && a<0x1f205e44) || a==0x1f20174c))
    updateOutputTiming(tick);
  if (candidateGamma && a == 0x1f205fb4 && n == 1 && get(0x1f205e00, 1) == 0x10) {
    auto command = v & 0xe0;
    if ((knownBits(a, 1) & 0xe0) != 0xe0)
      throw std::runtime_error("gamma command contains unknown channel bits");
    if (command) {
      auto channel = command == 0x20 ? 0 : command == 0x40 ? 1 : 2;
      auto dataAddress = 0x1f205fb8u + channel * 4;
      if ((knownBits(0x1f205fb0, 2) & 0x3ff) != 0x3ff || (knownBits(dataAddress, 2) & 0xfff) != 0xfff)
        throw std::runtime_error("gamma transfer address/sample contains unknown bits");
      gamma_.write(uint8_t(command), get(0x1f205fb0, 2) & 0x3ff, get(dataAddress, 2) & 0xfff, tick);
      event("gamma_write", get(0x1f205fb0, 2) & 0x3ff, channel, get(dataAddress, 2) & 0xfff,
            "original channel strobe captured original 12-bit sample; RAM completion pending");
    }
  }
  if (candidateSpi && n == 2 && a >= 0x1f001000 && a < 0x1f001100) {
    auto off = a - 0x1f001000;
    if (off == 0 || off == 4 || off == 0x10 || off == 0x18 || off == 0x20 || off == 0x30 ||
        off == 0xa8) {
      spi_.flashByte = [this](uint32_t offset) { return flashRead(offset, 1).at(0); };
        spi_.busEvent = [this, a, n](const std::string &kind, uint32_t value) {
          event(kind, a, n, value, "CS-framed MSB-first SPI bus; CF output unknown unless explicit exploratory electrical input configured");
        };
      spi_.write(off, uint16_t(v), tick);
      event("spi_command", a, n, v,
            "original guest ISP FIFO/control command; delayed readiness, no invented response to "
            "unsupported opcodes");
      if (!spi_.error.empty())
        stopReason = spi_.error;
    }
  }
  if (candidateUtmi && n == 1) {
    if (a == 0x1f200e00 || a == 0x1f200f00)
      utmi_[a == 0x1f200f00].clock(uint8_t(v));
    if ((a == 0x1f207578 || a == 0x1f207478) && (knownBits(a, 1) & 1)) {
      utmi_[a == 0x1f207478].command(v & 1, tick);
      event(
          "utmi_calibration", a, n, v & 1,
          "guest CA_START transition; requires programmed clock and held start before completion");
    }
  }
  if (virtualRtc && n == 1 && (a == 0x1f002400 || a == 0x1f002600)) {
    auto index = unsigned(a == 0x1f002600);
    auto divider = get(a + 4, 2) | (get(a + 8, 2) << 16);
    auto load = get(a + 12, 2) | (get(a + 16, 2) << 16);
    bool dividerKnown = knownBits(a + 4, 2) == 0xffff && knownBits(a + 8, 2) == 0xffff;
    bool loadKnown = knownBits(a + 12, 2) == 0xffff && knownBits(a + 16, 2) == 0xffff;
    auto control = rtc_[index].command(uint8_t(v), tick, divider, dividerKnown, load, loadKnown);
    put(a, 1, control);
    if (!rtc_[index].error.empty())
      stopReason = rtc_[index].error;
    if (v & 16)
      event("rtc_snapshot", a, 1, control,
            "counter latched by original guest READ_EN command; no host wall clock");
  }
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
