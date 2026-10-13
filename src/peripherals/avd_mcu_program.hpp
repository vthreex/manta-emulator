#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
namespace manta {
// Actual 805047D4 upload/readback sequence, independently matching the
// related HAL_AVD_VDMCU_LoadDSP. This is a programming port, not a ready MCU.
class AvdMcuProgram {
  int hold_=-1,mode_=-1,low_=-1,high_=-1;
  uint16_t cursor_=0;
  std::vector<uint8_t> code_=std::vector<uint8_t>(65536),valid_=std::vector<uint8_t>(65536);
  bool configured()const{return hold_==1 && mode_==0x51 && low_>=0 && high_>=0;}
public:
  uint64_t writes=0,reads=0,startRequests=0;
  bool write(unsigned offset,uint8_t value,uint8_t known){
    if(offset==1){
      if(!(known&1))throw std::runtime_error("AVD MCU execution/hold field unknown");
      bool start=(value&1) && hold_==1 && writes;
      hold_=!(value&1);if(start)++startRequests;return start;
    }
    if(offset==5){
      if((known&0xf1)!=0xf1){mode_=-1;return false;}
      auto mode=value&0xf1;
      if(mode!=0 && mode!=0x50 && mode!=0x51)throw std::runtime_error("AVD MCU unsupported programming mode");
      mode_=mode;return false;
    }
    if(known!=255)throw std::runtime_error("AVD MCU program address/data contains unknown bits");
    if(offset==8 || offset==9){
      if(offset==8)low_=value;else high_=value;
      if(low_>=0 && high_>=0)cursor_=uint16_t(low_|(high_<<8));
      return false;
    }
    if(offset==0x18){
      if(!configured())throw std::runtime_error("AVD MCU program write without guest hold/mode/address");
      code_[cursor_]=value;valid_[cursor_]=1;++cursor_;++writes;return false;
    }
    throw std::runtime_error("AVD MCU unsupported program port");
  }
  bool readable()const{return configured() && valid_[cursor_];}
  uint8_t read(){
    if(!readable())throw std::runtime_error("AVD MCU readback of unprogrammed code or outside programming mode");
    ++reads;return code_[cursor_++];
  }
  uint16_t cursor()const{return cursor_;}
  std::vector<uint8_t> contiguousImage()const{
    unsigned end=0;while(end<valid_.size() && valid_[end])++end;
    return {code_.begin(),code_.begin()+end};
  }
};
}
