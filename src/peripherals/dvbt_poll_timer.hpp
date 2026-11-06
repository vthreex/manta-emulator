#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <stdexcept>

namespace manta {
// CANDIDATE timeout counter inferred from ORIGINAL MCU16AB/16B3 and polls in
// 443C/535F/355D/506A. Command40 resets, F2:F3 programs the count, command1
// starts, command0 stops. The clock divisor must be supplied explicitly;
// neither a calibrated frequency nor reserved status bits are invented.
class DvbtPollTimer {
  bool reset_=false,running_=false,latched_=false;
  uint64_t start_=0,interval_=0;
public:
  uint64_t divider=0;
  bool expired(uint64_t now)const{
    if(running_ && now<start_)throw std::runtime_error("DVB-T timer clock moved backwards");
    return latched_ || (running_ && now-start_>=interval_);
  }
  uint8_t known()const{return reset_?1:0;}
  void control(uint8_t command,uint16_t count,uint16_t countKnown,uint64_t now){
    if(!divider)throw std::runtime_error("DVB-T timer has no configured clock divisor");
    if(command==0x40){reset_=true;running_=latched_=false;return;}
    if(command==0){latched_=expired(now);running_=false;return;}
    if(command!=1)throw std::runtime_error("DVB-T timer control command not yet modeled");
    if(!reset_ || countKnown!=0xffff || !count)throw std::runtime_error("DVB-T timer start lacks reset/known nonzero period");
    start_=now;interval_=uint64_t(count)*divider;running_=true;latched_=false;
  }
  void serialize(snap::Ar& a){a.tag("DvbtPollTimer1");a(reset_,running_,latched_,start_,interval_,divider);}
};
}
