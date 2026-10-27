#pragma once
#include "snapshot/archive.hpp"
#include "cpu/vd_mcu.hpp"
#include "storage/sha256.hpp"
#include <array>
#include <set>
#include <deque>
#include <sstream>
namespace manta {
// Explicit housekeeping/SPI reset experiment. Status outputs have no defaults.
// CODE is a view of original flash; RIU observations use the host CPU's backing.
class Pm51 {
  friend void ioClass(snap::Ar&, Pm51&);
  VdMcuCpu cpu_;
  std::span<const uint8_t> code_;
  std::array<VdByte,1024> ram_{};
  std::array<bool,1024> written_{};
  std::set<uint16_t> visited_;
  std::optional<uint8_t> romControl_;
  struct DeferredConfiguration {uint16_t pc,address;VdByte payload;};
  std::vector<DeferredConfiguration> deferredConfigurations_;
  uint64_t tick_=0;
  bool started_=false,suspended_=false;
  std::string boundary_;
  struct InstructionObservation {
    uint64_t tick, count;
    uint16_t pc, dptr, origin;
    uint8_t opcode, accumulator, known, stack, status, page;
    bool dispatched=false;
  };
  struct BusObservation {
    uint64_t tick;
    uint16_t pc, address;
    uint8_t page;
    VdByte byte;
    bool write, accepted;
  };
  std::deque<InstructionObservation> instructionHistory_;
  std::deque<BusObservation> busHistory_;
  void installHooks(){
    cpu_.busRead=[this](uint16_t a)->std::optional<VdByte>{
      if(a==0x1018 && romControl_){auto v=VdByte{*romControl_,255,0};observeBus(a,v,false,true);return v;}
      if(a>=0x4000 && a<0x4400){
        if(!written_[a-0x4000]){observeBus(a,{0xa5,0,a},false,false);throw VdHardwareBoundary("PM private RAM read before original initialization");}
        ++ramReads;auto v=ram_[a-0x4000];observeBus(a,v,false,true);return v;
      }
      if((a>=0x0e00 && a<0x0f00) || (registerStorage && a>=0x0f00 && a<0x8000 && !(a>=0x4000 && a<0x4400))){
        if(cpu_.port2()!=0 && !registerStorage){observeBus(a,{0xa5,0,a},false,false);throw VdHardwareBoundary("PM RIU page other than zero unresolved");}
        ++riuReads;auto v=sharedRead(a);observeBus(a,v,false,true);return v;
      }
      observeBus(a,{0xa5,0,a},false,false);return {};
    };
    cpu_.busWrite=[this](uint16_t a,VdByte v){
      if(a==0x1018 && v.known==255 && v.value==2){
        romControl_=v.value;++controlWrites;
        observeBus(a,v,true,true);
        event("pm51_spi_source",a,1,v.value,"original PM instruction selects configured SPI CODE view; exact board reset mapping remains exploratory");
        return true;
      }
      if(a>=0x4000 && a<0x4400){
        ram_[a-0x4000]=v;written_[a-0x4000]=true;++ramWrites;observeBus(a,v,true,true);return true;
      }
      if(probeDeferredStartupConfigurations && cpu_.port2()==0){
        const auto stage=deferredConfigurations_.size();
        const bool maskedPad=stage==0 && cpu_.pc()==0x574f && a==0x0e6a &&
          v.known==0x80 && v.origin==a && !(v.value&0x80);
        const bool followingZero=(stage==1 && cpu_.pc()==0x5754 && a==0x0e26) ||
          (stage==2 && cpu_.pc()==0x5756 && a==0x0e27);
        if(maskedPad || (followingZero && v.known==255 && v.value==0)){
          deferredConfigurations_.push_back({cpu_.pc(),a,v});observeBus(a,v,true,true);
          event("probe_pm51_deferred_configuration",a,1,v.value,
            "PROBE ONLY: original store retired with hardware effects PENDING/UNIMPLEMENTED; payload validity="+
            std::to_string(v.known)+"; shared RIU output/known bits unchanged, no completion generated");
          return true;
        }
      }
      if((a==0x0e69 || a==0x0e6f || (cpu_.candidateR8051Uart && (a==0x0e13 || a==0x0e1e)) || (registerStorage && a>=0x0e00 && a<0x8000)) && (cpu_.port2()==0 || registerStorage) && sharedWrite(a,v)){
        ++riuWrites;if(a==0x0e69)++markerWrites;observeBus(a,v,true,true);return true;
      }
      if(extendedWrite && extendedWrite(cpu_.port2(),a,v)){
        ++riuWrites;observeBus(a,v,true,true);return true;
      }
      observeBus(a,v,true,false);
      return false;
    };
    cpu_.instruction=[this](uint16_t pc,uint8_t opcode,const std::string& text){
      if(opcode!=code_[pc])throw VdHardwareBoundary("PM instruction differs from shared original flash");
      if(!firstVisits || visited_.insert(pc).second)
        event("pm51_instruction",pc,1,opcode,text);
    };
    cpu_.external=[this](uint16_t a,bool w,uint8_t v){
      event("pm51_unresolved_xdata",a,1,v,w?"unimplemented original PM write; no effects supplied":"unimplemented original PM read; no value supplied");
    };
  }
  void observeBus(uint16_t address,VdByte value,bool write,bool accepted){
    if(!captureHistory)return;
    busHistory_.push_back({tick_,cpu_.pc(),address,cpu_.port2(),value,write,accepted});
    if(busHistory_.size()>64)busHistory_.pop_front();
  }
public:
  uint64_t divider=32,budget=1000000,ramReads=0,ramWrites=0,riuReads=0,riuWrites=0;
  uint64_t markerWrites=0,controlWrites=0;
  bool firstVisits=true;
  bool registerStorage=false; // EXPLORATORY (--exploratory-pm51-register-storage): MOVX to the PM register banks XDATA 0x0E00..0x1FFF is routed to the shared RIU byte store
  bool captureHistory=false; // bounded diagnostics only; never part of hardware state
  bool probeDeferredStartupConfigurations=false; // probe-only; no hardware effects/readback
  std::function<VdByte(uint16_t)> sharedRead;
  std::function<bool(uint16_t,VdByte)> sharedWrite;
  std::function<bool(uint8_t,uint16_t,VdByte)> extendedWrite;
  std::function<bool(uint16_t,uint8_t)> codeSourceValid;
  std::function<bool()> executionStopped;
  std::function<void(const std::string&,uint32_t,unsigned,uint32_t,const std::string&)> event;
  void start(std::span<const uint8_t> code){
    if(started_ || !divider || !sharedRead || !sharedWrite)
      throw std::runtime_error("PM startup configuration incomplete/repeated");
    code_=code;
    installHooks();
    cpu_.resetPmCandidate(code_,[this](uint16_t a){return !codeSourceValid || codeSourceValid(a,code_[a]);});
    started_=true;
    event("pm51_start",0x10000,0,0,"original 65536-byte CODE view; exploratory standard reset concurrent with housekeeping CPU, no ready/marker produced");
  }
  // Save-state restore of a started PM51: reinstall the bus/trace callbacks, then re-attach the code view (state is overlaid afterwards).
  void setSnapshotCode(std::span<const uint8_t> code){code_=code;}
  void rebind(std::span<const uint8_t> code){
    code_=code;installHooks();
    cpu_.rebindPm(code_,[this](uint16_t a){return !codeSourceValid || codeSourceValid(a,code_[a]);});
  }
  void advance(uint64_t tick){
    if(!started_ || !cpu_.running())return;
    if(tick<tick_)throw std::runtime_error("PM virtual time moved backwards");
    try{while(tick-tick_>=divider){
      if(executionStopped && executionStopped())return;
      if(cpu_.totalInstructions()>=budget)throw std::runtime_error("PM original instruction budget reached");
      tick_+=divider;
      if(captureHistory){
        auto pc=cpu_.pc();
        instructionHistory_.push_back({tick_,cpu_.totalInstructions(),pc,cpu_.dataPointer(),cpu_.accumulatorOrigin(),
          code_[pc],cpu_.accumulator(),cpu_.accumulatorKnown(),cpu_.stack(),cpu_.status(),cpu_.port2(),false});
        if(instructionHistory_.size()>256)instructionHistory_.pop_front();
      }
      cpu_.step();
      if(captureHistory)instructionHistory_.back().dispatched=true;
    }}catch(const VdHardwareBoundary& e){
      suspended_=true;boundary_=e.what();
      event("pm51_unknown_boundary",cpu_.pc(),0,0,boundary_);
    }
  }
  const VdMcuCpu& cpu()const{return cpu_;}
  void enableStandardInterrupts(){cpu_.standardInterrupts=true;cpu_.allStandardSources=true;cpu_.sfrStorage=true;}
  void configureCandidateUart(bool enable){
    if(started_)throw std::runtime_error("PM UART configuration changed after reset");
    cpu_.candidateR8051Uart=enable;
  }
  bool started()const{return started_;}
  bool suspended()const{return suspended_;}
  const std::string& boundary()const{return boundary_;}
  size_t deferredConfigurationCount()const{return deferredConfigurations_.size();}
  std::string historyJsonLines()const{
    std::ostringstream out;
    // Integers are decimal JSON numbers; validity is independent of poison data.
    for(const auto& i:instructionHistory_)
      out<<"{\"kind\":\"instruction\",\"tick\":"<<i.tick<<",\"count_before\":"<<i.count
         <<",\"pc\":"<<i.pc<<",\"opcode\":"<<unsigned(i.opcode)<<",\"dptr\":"<<i.dptr
         <<",\"a\":"<<unsigned(i.accumulator)<<",\"known\":"<<unsigned(i.known)<<",\"origin\":"<<i.origin
         <<",\"sp\":"<<unsigned(i.stack)<<",\"psw\":"<<unsigned(i.status)<<",\"page\":"<<unsigned(i.page)
         <<",\"dispatched\":"<<(i.dispatched?"true":"false")<<"}\n";
    for(const auto& b:busHistory_)
      out<<"{\"kind\":\"bus\",\"tick\":"<<b.tick<<",\"pc\":"<<b.pc<<",\"address\":"<<b.address
         <<",\"page\":"<<unsigned(b.page)<<",\"value\":"<<unsigned(b.byte.value)
         <<",\"known\":"<<unsigned(b.byte.known)<<",\"origin\":"<<b.byte.origin
         <<",\"write\":"<<(b.write?"true":"false")<<",\"accepted\":"<<(b.accepted?"true":"false")<<"}\n";
    return out.str();
  }
  std::string stateSha()const{
    auto b=cpu_.stateBytes();
    auto append=[&](uint64_t value,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(uint8_t(value>>(8*i)));};
    for(unsigned i=0;i<ram_.size();++i){append(ram_[i].value,1);append(ram_[i].known,1);append(ram_[i].origin,2);append(written_[i],1);}
    for(auto count:{tick_,divider,ramReads,ramWrites,riuReads,riuWrites,markerWrites,controlWrites})append(count,8);
    append(started_,1);append(suspended_,1);append(romControl_.has_value(),1);append(romControl_.value_or(0),1);
    if(probeDeferredStartupConfigurations){
      append(1,1);append(deferredConfigurations_.size(),1);
      for(const auto& p:deferredConfigurations_){append(p.pc,2);append(p.address,2);append(p.payload.value,1);append(p.payload.known,1);append(p.payload.origin,2);}
    }
    return sha256(b);
  }
};
}
