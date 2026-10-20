#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <vector>
#include <stdexcept>
namespace manta {
// Original 804F565C loader (the observed payload is DVB-T): hold,
// mode50/51, address08/09, data18,
// rewind address, sequential read20. Related HAL_AVD_VDMCU_LoadDSP has
// the same transaction at a different register base. No MCU response is
// synthesized. The 16-bit address is a port encoding, not a capacity claim.
class VdMcuProgram {
  friend void ioClass(snap::Ar&, VdMcuProgram&);
  int hold_=-1, control_=-1, low_=-1, high_=-1, execution_=-1;
  uint16_t cursor_=0;
  std::vector<uint8_t> code_,valid_;
  void ready() const {
    if(hold_!=1 || execution_!=0 || control_!=0x51 || low_<0 || high_<0)
      throw std::runtime_error("VD MCU program port requires guest hold, disabled execution, mode51 and complete address");
  }
public:
  uint64_t writes=0,reads=0,startRequests=0,interruptRequests=0;
  bool interruptPending=false;
  // A start request only becomes CPU execution when the explicit bounded
  // startup experiment is enabled; no ready signal is inferred from it.
  bool write(unsigned offset,uint8_t value){
    if(offset==0){bool start=value==0 && hold_==1 && writes!=0;hold_=value;
      if(start)++startRequests;
      return start;}
    if(offset==1){execution_=value;return false;}
    if(offset==5){
      if(value==2){
        if(hold_!=0 || execution_!=1 || control_!=0)
          throw std::runtime_error("MCU interrupt doorbell requires released, configured MCU and previous low line");
        ++interruptRequests;interruptPending=true;control_=value;return false;
      }
      if(value!=0 && value!=0x50 && value!=0x51)
        throw std::runtime_error("Unsupported VD MCU program control");
      control_=value;return false;
    }
    if(offset==8 || offset==9){
      if(offset==8)low_=value;else high_=value;
      if(low_>=0 && high_>=0)cursor_=uint16_t(unsigned(low_)|(unsigned(high_)<<8));
      return false;
    }
    if(offset==0x18){
      ready();if(code_.empty()){code_.resize(65536);valid_.resize(65536);}
      code_[cursor_]=value;valid_[cursor_]=1;++cursor_;++writes;return false;
    }
    throw std::runtime_error("Unsupported VD MCU program port write");
  }
  uint8_t read(){
    ready();if(valid_.empty() || !valid_[cursor_])throw std::runtime_error("VD MCU program read of unwritten byte");
    auto v=code_[cursor_++];++reads;return v;
  }
  uint16_t cursor()const{return cursor_;}
  bool resetHeld()const{return hold_==1;}
  bool readable()const{return hold_==1 && execution_==0 && control_==0x51 && low_>=0 && high_>=0 && byteKnown(cursor_);}
  std::vector<uint8_t>& codeStorage(){return code_;}
  bool byteKnown(uint16_t address)const{return !valid_.empty() && valid_[address];}
  std::vector<uint8_t> contiguousImage()const{
    size_t end=0;while(end<valid_.size() && valid_[end])++end;
    if(!end)return {};
    return {code_.begin(),code_.begin()+end};
  }
};
}
