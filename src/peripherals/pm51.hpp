#pragma once
#include "cpu/vd_mcu.hpp"
#include "storage/sha256.hpp"
#include <array>
#include <set>
namespace manta {
// Explicit housekeeping/SPI reset experiment. Status outputs have no defaults.
// CODE is a view of original flash; RIU observations use the host CPU's backing.
class Pm51 {
  VdMcuCpu cpu_;
  std::span<const uint8_t> code_;
  std::array<VdByte,1024> ram_{};
  std::array<bool,1024> written_{};
  std::set<uint16_t> visited_;
  std::optional<uint8_t> romControl_;
  uint64_t tick_=0;
  bool started_=false,suspended_=false;
  std::string boundary_;
public:
  uint64_t divider=32,budget=1000000,ramReads=0,ramWrites=0,riuReads=0,riuWrites=0;
  uint64_t markerWrites=0,controlWrites=0;
  bool firstVisits=true;
  std::function<VdByte(uint16_t)> sharedRead;
  std::function<bool(uint16_t,VdByte)> sharedWrite;
  std::function<bool(uint16_t,uint8_t)> codeSourceValid;
  std::function<bool()> executionStopped;
  std::function<void(const std::string&,uint32_t,unsigned,uint32_t,const std::string&)> event;
  void start(std::span<const uint8_t> code){
    if(started_ || !divider || !sharedRead || !sharedWrite)
      throw std::runtime_error("PM startup configuration incomplete/repeated");
    code_=code;
    cpu_.busRead=[this](uint16_t a)->std::optional<VdByte>{
      if(a==0x1018 && romControl_)return VdByte{*romControl_,255,0};
      if(a>=0x4000 && a<0x4400){
        if(!written_[a-0x4000])throw VdHardwareBoundary("PM private RAM read before original initialization");
        ++ramReads;return ram_[a-0x4000];
      }
      if(a>=0x0e00 && a<0x0f00){
        if(cpu_.port2()!=0)throw VdHardwareBoundary("PM RIU page other than zero unresolved");
        ++riuReads;return sharedRead(a);
      }
      return {};
    };
    cpu_.busWrite=[this](uint16_t a,VdByte v){
      if(a==0x1018 && v.known==255 && v.value==2){
        romControl_=v.value;++controlWrites;
        event("pm51_spi_source",a,1,v.value,"original PM instruction selects configured SPI CODE view; exact board reset mapping remains exploratory");
        return true;
      }
      if(a>=0x4000 && a<0x4400){
        ram_[a-0x4000]=v;written_[a-0x4000]=true;++ramWrites;return true;
      }
      if((a==0x0e69 || a==0x0e6f) && cpu_.port2()==0 && sharedWrite(a,v)){
        ++riuWrites;if(a==0x0e69)++markerWrites;return true;
      }
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
    cpu_.resetPmCandidate(code_,[this](uint16_t a){return !codeSourceValid || codeSourceValid(a,code_[a]);});
    started_=true;
    event("pm51_start",0x10000,0,0,"original 65536-byte CODE view; exploratory standard reset concurrent with housekeeping CPU, no ready/marker produced");
  }
  void advance(uint64_t tick){
    if(!started_ || !cpu_.running())return;
    if(tick<tick_)throw std::runtime_error("PM virtual time moved backwards");
    try{while(tick-tick_>=divider){
      if(executionStopped && executionStopped())return;
      if(cpu_.totalInstructions()>=budget)throw std::runtime_error("PM original instruction budget reached");
      tick_+=divider;cpu_.step();
    }}catch(const VdHardwareBoundary& e){
      suspended_=true;boundary_=e.what();
      event("pm51_unknown_boundary",cpu_.pc(),0,0,boundary_);
    }
  }
  const VdMcuCpu& cpu()const{return cpu_;}
  bool started()const{return started_;}
  bool suspended()const{return suspended_;}
  const std::string& boundary()const{return boundary_;}
  std::string stateSha()const{
    auto b=cpu_.stateBytes();
    auto append=[&](uint64_t value,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(uint8_t(value>>(8*i)));};
    for(unsigned i=0;i<ram_.size();++i){append(ram_[i].value,1);append(ram_[i].known,1);append(ram_[i].origin,2);append(written_[i],1);}
    for(auto count:{tick_,divider,ramReads,ramWrites,riuReads,riuWrites,markerWrites,controlWrites})append(count,8);
    append(started_,1);append(suspended_,1);append(romControl_.has_value(),1);append(romControl_.value_or(0),1);
    return sha256(b);
  }
};
}
