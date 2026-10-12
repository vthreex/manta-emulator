#include "peripherals/riu.hpp"
#include <array>
#include <stdexcept>
#include "storage/sha256.hpp"
namespace manta {
std::string Riu::tspCoreStateSha()const{
  auto bytes=tspCpu_.stateBytes(),qmem=tspDownload_.stateBytes();bytes.insert(bytes.end(),qmem.begin(),qmem.end());
  for(unsigned i=0;i<8;++i)bytes.push_back(uint8_t(tspCpuTick_>>(i*8)));
  return sha256(bytes);
}
void Riu::configureExploratoryTspCpu(){
  exploratoryTspCpu_=true;
  // Nearby register definitions agree with the original ORed mask bits, but
  // unprogrammed Titania4 reset fields and reset PC remain explicit fixtures.
  for(auto address:{0x1f202b88u,0x1f202b8cu,0x1f202b90u,0x1f202b94u,
       0x1f202b98u,0x1f202b9cu,0x1f202ba0u,0x1f202ba4u})put(address,2,0);
  bytes_[0x1f202be9]={0x85,0x30}; // Only instruction/data endian bits (LE).
  tspCpu_.read=[this](uint32_t a,unsigned n,bool f){return tspCpuRead(a,n,f);};
  tspCpu_.write=[this](uint32_t a,unsigned n,OrWord v){tspCpuWrite(a,n,v);};
  tspCpu_.trace=[this](uint32_t pc,uint32_t word){
    bool fresh=orVisited_.insert(pc).second;
    if(!orFirstVisits_ || fresh)event("or1k_instruction",pc,4,word,orFirstVisits_?
      "original auxiliary instruction; first visit per PC trace; total execution and immutable-word comparisons counted separately":
      "original TSP QMEM instruction; deterministic auxiliary dispatch");
  };
}
void Riu::advanceTsp(uint64_t tick){
  if(!exploratoryTspCpu_ || !tspCpu_.running())return;
  // One auxiliary dispatch per MIPS dispatch is provisional, not a measured
  // frequency. Scheduling uses the same monotonically increasing guest tick.
  while(tick-tspCpuTick_>=orDivider_){
    if(executionStopped && executionStopped())return;
    if(tspCpu_.instructions()>=orBudget_)throw std::runtime_error("OpenRISC diagnostic dispatch budget reached; no hardware timeout or successful boot inferred");
    tspCpuTick_+=orDivider_;tspCpu_.step();
  }
}
OrWord Riu::tspCpuRead(uint32_t a,unsigned n,bool fetch){
  uint32_t base=fetch?0x1f202b88:0x1f202b98,mask=fetch?0x1f202b90:0x1f202ba0;
  if(knownBits(base,2)!=0xffff || knownBits(base+4,2)!=0xffff || knownBits(mask,2)!=0xffff || knownBits(mask+4,2)!=0xffff)
    throw std::runtime_error("OpenRISC QMEM mapping contains unknown bits");
  auto combine=[&](uint32_t p){return get(p,2)|(get(p+4,2)<<16);};
  if((a&combine(mask))==combine(base)){
    auto offset=a&0x3fff;if(offset+n>0x4000)throw std::runtime_error("OpenRISC QMEM boundary crossing");
    uint32_t value=0;for(unsigned i=0;i<n;++i)value|=uint32_t(tspDownload_.byte(offset+i))<<(i*8);
    if(fetch){bool original=true;for(unsigned i=0;i<n;++i){auto b=tspDownload_.originalByte(offset+i);if(!b||*b!=uint8_t(value>>(i*8)))original=false;}
      if(original)++orOriginalWords_;else ++orModifiedWords_;}
    if(!fetch)++tspCpuReads_;
    return {value,0xffffffffu,{}};
  }
  if(fetch)throw std::runtime_error("OpenRISC instruction fetch outside configured QMEM unsupported");
  ++tspCpuReads_;
  // These aliases are corroborated by both original command handlers: MIPS
  // Alive issues 01000000 at B00; OR main sets global=210200, dispatches +130,
  // responds at +13c, and clears +130. MIPS reads response from C00.
  if((a==0x210330 || a==0x21033c) && n==4){
    uint32_t p=a==0x210330?0x1f202b00:0x1f202c00;
    OrWord v{get(p,2)|(get(p+4,2)<<16),knownBits(p,2)|(knownBits(p+4,2)<<16),a};
    event("or1k_register_read",a,n,v.value,"shared TSP register storage; unknown reset bits retain origin");return v;
  }
  if(a>=0x210000 && a<0x218000 && n==4 && !(a&3)){
    OrWord v{0,0,a};
    for(unsigned i=0;i<4;++i){auto it=bytes_.find(tspPayloadKey(a<<1,i));auto byte=it==bytes_.end()?RiuByte{}:it->second;
      v.value|=uint32_t(byte.value)<<(i*8);v.known|=uint32_t(byte.known)<<(i*8);}
    event("or1k_internal_read",a,n,v.value,"internal register aperture shared with MIPS indirect address<<1 transport; undefined reset bits remain invalid");return v;
  }
  throw std::runtime_error("OpenRISC data bus address unsupported");
}
void Riu::tspCpuWrite(uint32_t a,unsigned n,OrWord v){
  ++tspCpuWrites_;
  auto dbase=get(0x1f202b98,2)|(get(0x1f202b9c,2)<<16),dmask=get(0x1f202ba0,2)|(get(0x1f202ba4,2)<<16);
  uint32_t wanted=n==4?0xffffffffu:(1u<<(n*8))-1;
  if((a&dmask)==dbase){
    if((v.known&wanted)!=wanted)throw std::runtime_error("OpenRISC unknown bits stored in QMEM unsupported");
    for(unsigned i=0;i<n;++i)tspDownload_.byte((a&0x3fff)+i,uint8_t(v.value>>(i*8)));
    event("or1k_qmem_write",a,n,v.value,"original auxiliary instruction writes the shared downloaded QMEM backing");return;
  }
  if((a==0x210330 || a==0x21033c) && n==4){
    if(v.known!=0xffffffffu && v.origin!=a)throw std::runtime_error("OpenRISC unknown foreign register write");
    uint32_t p=a==0x210330?0x1f202b00:0x1f202c00;
    for(unsigned i=0;i<4;++i)bytes_[p+(i>=2?4:0)+(i&1)]={uint8_t(v.value>>(i*8)),uint8_t(v.known>>(i*8))};
    event("or1k_register_write",a,n,v.value,"original auxiliary instruction writes shared TSP register backing; validity preserved");return;
  }
  if(a>=0x210000 && a<0x218000 && n==4 && !(a&3)){
    if(v.known!=0xffffffffu && v.origin!=a)throw std::runtime_error("OpenRISC unknown foreign internal register write");
    for(unsigned i=0;i<4;++i)bytes_[tspPayloadKey(a<<1,i)]={uint8_t(v.value>>(i*8)),uint8_t(v.known>>(i*8))};
    event("or1k_internal_write",a,n,v.value,"original auxiliary write to shared internal register storage; no unknown side-effect completion claimed");return;
  }
  throw std::runtime_error("OpenRISC store address unsupported");
}
uint32_t Riu::tspPayloadKey(uint32_t target,unsigned lane) {
  if(target<0x00420000 || target>=0x00430000 || (target&7) || lane>=4)
    throw std::runtime_error("TSP indirect target outside corroborated aligned filter-register aperture");
  if(target==0x420660 || target==0x420678){
    uint32_t base=target==0x420660?0x1f202b00:0x1f202c00;
    return base+(lane>=2?4:0)+(lane&1);
  }
  return 0x83000000u+(target-0x00420000)/2+lane;
}
uint32_t Riu::tspSelectedTarget() const {
  if(knownBits(0x1f202b24,2)!=0xffff || knownBits(0x1f202b28,2)!=0xffff)
    throw std::runtime_error("TSP indirect address contains unknown bits");
  auto encoded=get(0x1f202b24,2)|(get(0x1f202b28,2)<<16);
  if(encoded>0x7fffffffu)throw std::runtime_error("TSP indirect address shift would overflow");
  auto target=encoded<<1;
  tspPayloadKey(target,0);return target;
}
uint32_t Riu::controlByteKey(uint32_t a) const {
  if(candidateTsp) {
    if((a>=0x1f202b2c && a<0x1f202b2e) || (a>=0x1f202b30 && a<0x1f202b32))
      return tspPayloadKey(tspSelectedTarget(),a>=0x1f202b30?2+a-0x1f202b30:a-0x1f202b2c);
    if((a>=0x1f202b34 && a<0x1f202b36) || (a>=0x1f202b38 && a<0x1f202b3a)) {
      if(!tspReadTarget_)throw std::runtime_error("TSP indirect read has no completed address/command producer");
      return tspPayloadKey(*tspReadTarget_,a>=0x1f202b38?2+a-0x1f202b38:a-0x1f202b34);
    }
  }
  return storageKey(a);
}
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
  return controlByteKey(a);
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
  if(exploratoryIrDmaReset_ && n==1 && (a==0x1f206e79 || a==0x1f206e7c) && irDma_.producer())return 255;
  if(a==0x1f001cdd && n==1 && pmBoot_.cause())return 255;
  if(exploratorySarFullscale_ && n==1 && (a==0x1f002834 || a==0x1f002835) && sar_.result(a-0x1f002834))return 255;
  if(candidateTsp && a==0x1f202afc && n==2)return tsp_.known()?TspQueue::countStatusMask:0;
  if(candidateGe && ge_.initialized() && a==0x1f20501c && n==2)return GeQueue::statusKnown;
  if(candidateGe && a==0x1f2050c8 && n==2)return ge_.completedTag()?0xffff:0;
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(storageKey(a + i));
    if (it != bytes_.end())
      v |= uint32_t(it->second.known) << (8 * i);
  }
  if(candidateTsp && a==0x1f202be8 && n==2) {
    v&=~8u;if(tspDownload_.known())v|=8;
  }
  return v;
}
uint32_t Riu::get(uint32_t a, unsigned n) const {
  if(candidateTsp && a==0x1f202afc && n==2)
    return tsp_.known()?((0xa5a5u&~TspQueue::countStatusMask)|tsp_.status()):0xa5a5;
  if(candidateGe && ge_.initialized() && a==0x1f20501c && n==2)
    return (0xa5a5u&~GeQueue::statusKnown)|ge_.status();
  if(candidateGe && a==0x1f2050c8 && n==2)return ge_.completedTag().value_or(0xa5a5);
  uint32_t v = 0;
  for (unsigned i = 0; i < n; ++i) {
    auto it = bytes_.find(storageKey(a + i));
    v |= uint32_t(it == bytes_.end() ? 0xa5 : it->second.value) << (i * 8);
  }
  if(candidateTsp && a==0x1f202be8 && n==2) {
    v&=~8u;if(tspDownload_.done())v|=8;
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
  advanceTsp(tick);
  if(exploratoryIrDmaReset_ && n==1 && (a==0x1f206e79 || a==0x1f206e7c))if(auto cursor=irDma_.producer()){
    auto v=(*cursor>>(a==0x1f206e79?0:8))&255;
    event("exploratory_ir_dma_cursor",a,n,v,"capture-ring producer index after original descriptor command; no external IR records supplied, no key or interrupt fabricated; reset coupling unvalidated");return v;
  }
  if(exploratorySarFullscale_){
    sar_.advance(tick);
    if(n==1 && (a==0x1f002834 || a==0x1f002835))if(auto v=sar_.result(a-0x1f002834)){
      event("exploratory_sar_sample",a,n,*v,"free-running conversion from explicit 3300mV/reference fixture, guest digital/analog power controls and period; keypad wiring and latency unvalidated");return *v;
    }
  }
  if(a==0x1f001cdd && n==1 && pmBoot_.cause()){
    event("exploratory_pm_boot_reason",a,n,*pmBoot_.cause(),"EXPLORATORY external PM boundary with applied AC power: documented ACON enum ff; no PM51 code, ready ACK or wake key fabricated");
    return *pmBoot_.cause();
  }
  if(candidateTsp && tspDownload_.advance(tick))
    event("tsp_firmware_download_complete",tspDownloadSource_,unsigned(tspDownload_.bytes()),0,
        "actual guest RAM bytes installed into internal QMEM; sha256="+sha256(tspDownload_.transferred())+"; transfer latency provisional, auxiliary CPU not emulated");
  if(candidateTsp)tsp_.execute(tick);
  if(candidateGe)ge_.advance(tick);
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
  advanceTsp(tick);
  if(a<=0x1f001cdd && uint64_t(a)+n>0x1f001cdd && pmBoot_.cause())
    throw std::runtime_error("PM PowerOnMode writer unsupported under external AC-on boundary; mode not silently changed or acknowledged");
  if (n != 1 && n != 2 && n != 4)
    throw std::runtime_error("Unsupported RIU access width");
  auto previousTspControl=candidateTsp && a==0x1f202be8?get(a,n):0;
  auto previousTspKnown=candidateTsp && a==0x1f202be8?knownBits(a,n):0;
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
  if(exploratorySarFullscale_ && (a==0x1f002800 || a==0x1f002801 || a==0x1f002804)){
    sar_.configure(uint8_t(get(0x1f002800,1)),uint8_t(knownBits(0x1f002800,1)),
      uint8_t(get(0x1f002801,1)),uint8_t(knownBits(0x1f002801,1)),
      uint8_t(get(0x1f002804,1)),uint8_t(knownBits(0x1f002804,1)),tick);
  }
  if(exploratoryIrDmaReset_ && a==0x1f206e3d && n==1 && knownBits(a,1)==255 && v==0x82){
    for(auto field:{0x1f206e70u,0x1f206e71u,0x1f206e74u,0x1f206e75u,0x1f206e78u})
      if(knownBits(field,1)!=255)throw std::runtime_error("IR DMA descriptor contains unknown bits");
    if(!(knownBits(0x1f206e39,1)&1))throw std::runtime_error("IR DMA MIU selection unknown");
    uint32_t address=(get(0x1f206e70,1)|(get(0x1f206e71,1)<<8)|(get(0x1f206e74,1)<<16))<<3;
    address+=(get(0x1f206e39,1)&1)?0x08000000:0;
    unsigned capacity=get(0x1f206e75,1)|(get(0x1f206e78,1)<<8);
    irDma_.configure(address,capacity);
    event("exploratory_ir_dma_configure",address,48,capacity,"original buffer fields committed; HYPOTHETICAL command 82h starts/reset-empty capture ring; no captured pulses or keys invented");
  }
  if(candidateTsp && n==2) {
    if(a==0x1f202be8 && (knownBits(a,2)&4) && (v&4) &&
       (previousTspKnown&4) && !(previousTspControl&4)) {
      if(knownBits(0x1f202be0,2)!=0xffff || knownBits(0x1f202be4,2)!=0xffff ||
         (knownBits(0x1f202c1c,2)&15)!=15 || !dramRead)
        throw std::runtime_error("TSP firmware DMA descriptor/address high bits or DRAM backend unknown");
      tspDownloadSource_=(get(0x1f202be0,2)<<6)|((get(0x1f202c1c,2)&15)<<22);
      auto length=get(0x1f202be4,2)<<2;
      auto payload=dramRead(tspDownloadSource_,length);
      if(payload.size()!=length)throw std::runtime_error("TSP firmware DMA source unavailable");
      tspDownload_.start(payload,tick);
      event("tsp_firmware_download_request",tspDownloadSource_,length,0,
          "original start edge; source[21:6]+high[25:22], NUM[17:2]; bounded guest RAM payload captured for QMEM transfer, no completion fabricated");
    }
    if(a==0x1f202be8 && (knownBits(a,2)&1) && (v&1) &&
       (previousTspKnown&1) && !(previousTspControl&1))
      {
        if(!exploratoryTspCpu_)throw std::runtime_error("TSP OpenRISC auxiliary CPU execution/address mapping unsupported; no alive/firmware response fabricated");
        if(!tspDownload_.done() || (knownBits(a,2)&0x3002)!=0x3002 || (v&0x3000) || !(v&2))
          throw std::runtime_error("OpenRISC enable before download/reset/endianness configuration");
        tspCpu_.reset(0x100);tspCpuTick_=tick;
        event("exploratory_or1k_enable",a,n,v,"HYPOTHETICAL reset PC 100, SR 8001, zero unprogrammed QMEM fields, little-endian buses, configured auxiliary dispatch divider; no alive response supplied");
      }
    if(exploratoryTspCpu_ && a==0x1f202be8 &&
       (((knownBits(a,2)&1)&&!(v&1)) || ((knownBits(a,2)&2)&&!(v&2))))tspCpu_.halt();
    if(a==0x1f202b20 && (knownBits(a,2)&0xff)==0xff && (v&1)) {
      auto command=v&0xff;
      if(command!=0x21 && command!=0x23)
        throw std::runtime_error("TSP indirect command unsupported; no borrowed Titania3 opcode response");
      if(!tsp_.known() || tsp_.held())throw std::runtime_error("TSP indirect access before known experimental reset release");
      auto target=tspSelectedTarget();
      for(unsigned lane=0;lane<4;++lane) {
        auto key=tspPayloadKey(target,lane);
        auto port=(command==0x21?0x1f202b34u:0x1f202b2cu)+(lane>=2?4:0)+(lane&1);
        if(command==0x21) {
          auto it=bytes_.find(key);bytes_[port]=it==bytes_.end()?RiuByte{}:it->second;
        } else bytes_[key]=bytes_[port];
      }
      if(command==0x21){tspReadTarget_=target;++tspIndirectReads_;}
      else ++tspIndirectWrites_;
      event(command==0x21?"tsp_indirect_read":"tsp_indirect_write",target,4,
        get(command==0x21?0x1f202b34:0x1f202b2c,2)|(get(command==0x21?0x1f202b38:0x1f202b30,2)<<16),
        "original address>>1 and 21h/23h command transport; unknown filter bits retain internal-register provenance; no filter reset values invented");
    }
    if(a==0x1f202be8 && tsp_.control(uint16_t(v),uint16_t(knownBits(a,2))))
      event("exploratory_tsp_reset",a,n,v,"HYPOTHETICAL active-low engine control bit1 reset coupling; queue emptied by original reset write, not by status reads");
    if(a==0x1f202bfc && tsp_.commandReset(uint16_t(v),uint16_t(knownBits(a,2))))
      event("tsp_cmdq_reset",a,n,v,"original HAL command-queue reset bit11 asserted; pending work discarded by reset, not completed");
    if(a==0x1f202af8 && (knownBits(a,2)&1) && (v&1)) {
      auto address=get(0x1f202ae8,2)|(get(0x1f202aec,2)<<16);
      auto size=get(0x1f202af0,2)|(get(0x1f202af4,2)<<16);
      if((knownBits(0x1f202ae8,2)|(knownBits(0x1f202aec,2)<<16))!=0xffffffffu ||
         (knownBits(0x1f202af0,2)|(knownBits(0x1f202af4,2)<<16))!=0xffffffffu)
        throw std::runtime_error("TSP file-in descriptor contains unknown bits");
      tsp_.submit({address,size,uint16_t(v),tick});
      event("tsp_filein_submit",address,4,size,"guest descriptor retained pending; TS DMA/demux execution unsupported");
      tsp_.execute(tick);
    }
  }
  if(candidateGe && GeQueue::aperture(a)) {
    if(n!=2 || (a&3))throw std::runtime_error("GE controller requires corroborated halfword register lanes");
    if(a==0x1f20501c)throw std::runtime_error("GE queue status is hardware-owned, not a writable latch");
    if(a==0x1f205004 && (knownBits(a,2)&2) && (v&2))
      throw std::runtime_error("GE virtual command memory transport unsupported; no fabricated VCmd completion");
    if(!ge_.submit(uint16_t((a-0x1f205000)/4),uint16_t(v),tick)) {
      stopReason="GE submission requires known initial occupancy and available queue capacity";
      event("blocked_write",a,n,v,stopReason);return;
    }
    event("ge_queue_submit",a,n,v,"guest parameter register submission; two experimental timed stages; no raster operation retired");
  }
  if(exploratoryGopResetWindows_ && a==0x1f203e00 && n==2 &&
      (gopBank()==0 || gopBank()==3) && (knownBits(a,n)&1)){
    unsigned plane=gopBank()/3;
    if(v&1){
      if(!gopResetHeld_[plane]){
        ++gopResetPulses_;auto bank=plane?4u:1u;auto windows=plane?2u:4u;
        for(unsigned w=0;w<windows;++w){auto &b=bytes_[0x80000000u+(bank<<10)+w*0x80];
          b.value&=0xfe;b.known|=1;}
        event("exploratory_gop_reset_windows",gopBank(),2,v,"HYPOTHETICAL original top-bank bit0 reset pulse disables corresponding GWIN enable bits only; format/address/IRQ/other reset fields remain unknown");
      }
      gopResetHeld_[plane]=true;
    }else gopResetHeld_[plane]=false;
  }
  if(candidateGopPalette && a==0x1f203e14 && n==2 && (gopBank()==0 || gopBank()==3)) {
    auto color=get(0x1f203e0c,2)|(get(0x1f203e10,2)<<16);
    auto known=knownBits(0x1f203e0c,2)|(knownBits(0x1f203e10,2)<<16);
    if(palette_.pulse(gopBank(),uint16_t(v),uint16_t(knownBits(a,2)),color,known))
      event("gop_palette_commit",gopBank()*256+(v&255),4,color,"original guest color/index and full register5 write pulse; no invented palette reset values");
  }
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
