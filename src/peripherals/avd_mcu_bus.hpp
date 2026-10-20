#pragma once
#include "snapshot/archive.hpp"
#include "cpu/vd_mcu.hpp"
#include <array>
#include <map>
namespace manta {
// Candidate AVD XDATA wiring, correlated with original 11C0 soft-stop and
// 11DC..11DF host-assisted requests and original MIPS 35C0/35DC..35DF accesses.
// No reset byte, idle mailbox, received signal or completion is initialized.
class AvdMcuBus {
  friend void ioClass(snap::Ar&, AvdMcuBus&);
  std::array<VdByte,256> ram_{};
  std::map<uint16_t,uint8_t> deferred_;
  bool deferStartup_=false;
public:
  uint64_t ramReads=0,ramWrites=0,sharedReads=0,sharedWrites=0,configurationWrites=0;
  std::function<VdByte(uint32_t)> sharedRead;
  std::function<void(uint32_t,VdByte)> sharedWrite;
  std::function<void(const std::string&,uint32_t,uint32_t,uint32_t,const std::string&)> event;
  void configureDeferredStartup(){deferStartup_=true;}
  // Original 1C22/1C55..1C63 publishes CODE table data to these
  // configuration bytes. A merged table result may overwrite them INVALID;
  // it is never treated as a same-register preserved value or device reply.
  bool symbolicTableConfiguration=false;
  // EXPLORATORY: 11FC is treated as a latched event register on which a software
  // write can only clear bits. Never set by this model; reads are supplied by the
  // owner of the backing. Off by default (strict stop).
  bool eventAckClearOnly=false;
  // DIAGNOSTIC: stores into the published block 11C0..11DF may carry INVALID or partially
  // known data (copies/derivations of raw hardware bytes). The known mask is preserved
  // exactly, so no information is created; off by default.
  bool invalidDataPublication=false;
  uint64_t eventAcks=0;
  static std::optional<uint32_t> sharedAddress(uint16_t address){
    if(address<0x1100 || address>=0x1300)return {};
    auto logical=uint32_t(address)+0x2400;
    return 0x1f200000u+(logical<<1)-(logical&1);
  }
  std::optional<VdByte> read(uint16_t address){
    if(address>=0x8000 && address<0x8100){
      ++ramReads;auto v=ram_[address-0x8000];if(v.known!=255 && !v.origin)v.origin=address;return v;
    }
    if(auto physical=sharedAddress(address)){
      if(!sharedRead)return {};
      auto v=sharedRead(*physical);v.origin=address;++sharedReads;
      if(event)event("avd_shared_register_read",*physical,1,v.value,
        "original AVD MOVX XDATA="+std::to_string(address)+"; MIPS shared backing, known="+std::to_string(v.known));
      return v;
    }
    // Deferred configuration records are NOT readable device latches. Their
    // actual register/clock/reset effects are still unresolved.
    return {};
  }
  bool write(uint16_t address,VdByte value){
    // Original 099B..09A1 complements the captured event byte before a store.
    // Plain RW storage would turn acknowledged zero bits into new events.
    // Its clearing/latching semantics are unresolved: stop, never fabricate
    // events from this software write (or guess write-zero-to-clear behavior).
    if(address==0x11fc){
      if(!eventAckClearOnly)return false;
      ++eventAcks;
      if(event)event("avd_event_ack_clear_only",0x1f206bf8,1,value.value,
        "EXPLORATORY: original ack of the captured event byte; modeled as clear-only, no event bit is created or set; valid under write-0 or write-1 clear semantics");
      return true;
    }
    if(address>=0x8000 && address<0x8100){
      ram_[address-0x8000]=value;++ramWrites;return true;
    }
    if(auto physical=sharedAddress(address)){
      // COMB banking can change between CPUs. A 16-bit MCU origin does not
      // encode that bank: reject partial COMB restores instead of laundering
      // unknown bits across banks. AFEC is unbanked on this shared aperture.
      bool tableData=symbolicTableConfiguration && address>=0x117c && address<=0x117f && value.origin==0;
      // Original 0289..0290 copies the private timer diagnostic byte before
      // entering its loop. Retain uninitialized 8077 as INVALID in 11D6;
      // this is a data publication, not a produced timer tick or ACK.
      bool timerCopy=symbolicTableConfiguration && address==0x11d6 && value.origin==0x8077;
      // Original 0AA9..0ABC -> 076E publishes the raw timing bytes, not a
      // completion. A data copy may retain an INVALID sample. The high-byte
      // AND #07 must have defined the upper five bits as zero; unrelated
      // origins, swapped lanes and unmasked high bytes remain unsupported.
      bool timingCopy=symbolicTableConfiguration &&
        ((address==0x11c7 && value.origin==0x113c) ||
         (address==0x11c8 && value.origin==0x113d && (value.known&0xf8)==0xf8 && !(value.value&0xf8)));
      bool publication=invalidDataPublication && address>=0x11c0 && address<0x11e0;
      if(!sharedWrite || (value.known!=255 && !tableData && !timerCopy && !timingCopy && !publication && (value.origin!=address || address>=0x1200)))return false;
      sharedWrite(*physical,value);++sharedWrites;
      if(event)event("avd_shared_register_write",*physical,1,value.value,
        "original AVD MOVX XDATA="+std::to_string(address)+"; known="+std::to_string(value.known)+"; no host response generated");
      return true;
    }
    bool originalStartup=(address==0xc008 && value.value==0x55) ||
      (address==0xc009 && value.value==0xaa) || (address==0x101b && value.value==7) ||
      (address==0x101a && value.value==6);
    if(deferStartup_ && originalStartup && value.known==255){
      deferred_[address]=value.value;++configurationWrites;
      if(event)event("diagnostic_avd_deferred_configuration",address,1,value.value,
        "EXPLORATORY: original startup write recorded; effects/readback unknown; assumes no asynchronous watchdog reset/clock disturbance during bounded prefix; NOT an acknowledgment or proven disable key");
      return true;
    }
    return false;
  }
  bool deferSfr(uint8_t address,VdByte value){
    bool startup=(address==0xd8 && value.value==0) || (address==0x8e && value.value==1) ||
      (address==0xe2 && value.value==0);
    if(!deferStartup_ || !startup || value.known!=255)return false;
    deferred_[0xff00u|address]=value.value;++configurationWrites;
    if(event)event("diagnostic_avd_deferred_sfr",address,1,value.value,
      "EXPLORATORY original SFR configuration write retained without asserting UART/clock/page effects; reads remain unsupported; bounded undisturbed-execution hypothesis");
    return true;
  }
  std::vector<uint8_t> stateBytes()const{
    std::vector<uint8_t> out={uint8_t(deferStartup_)};
    for(auto v:ram_){out.push_back(v.value);out.push_back(v.known);out.push_back(uint8_t(v.origin));out.push_back(uint8_t(v.origin>>8));}
    for(auto count:{ramReads,ramWrites,sharedReads,sharedWrites,configurationWrites})
      for(unsigned i=0;i<8;++i)out.push_back(uint8_t(count>>(8*i)));
    out.push_back(uint8_t(deferred_.size()));
    for(auto [address,value]:deferred_){out.push_back(uint8_t(address));out.push_back(uint8_t(address>>8));out.push_back(value);}
    if(symbolicTableConfiguration)out.push_back(0x54);
    return out;
  }
};
}
