#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <optional>
namespace manta {
// T4 stock 805383CC pulses0100, waits for ACK-mask clear, and falls back to
// force0200. Global-bank scope and output-sync latch domain are explicit
// exploratory hypotheses; no reset ACK/IRQ value is supplied.
class GopUpdate {
  friend void ioClass(snap::Ar&, GopUpdate&);
  bool normalHigh_=false,forceHigh_=false,pending_=false;
  uint16_t known_=0,busy_=0;
  std::optional<uint64_t> requestedEdge_;
public:
  uint64_t requests=0,forced=0,latches=0;
  bool write(uint16_t value,uint16_t known,std::optional<uint64_t> edge){
    bool normal=false,force=false;
    if(known&0x100){
      if(value&0x100)normalHigh_=true;
      else if(normalHigh_){normal=true;normalHigh_=false;}
    }else normalHigh_=false;
    if(known&0x200){
      if(value&0x200)forceHigh_=true;
      else if(forceHigh_){force=true;forceHigh_=false;}
    }else forceHigh_=false;
    if(normal){++requests;pending_=true;requestedEdge_=edge;known_=busy_=0xf000;}
    if(force){++forced;++latches;pending_=false;requestedEdge_.reset();known_=0xf000;busy_=0;return true;}
    return false;
  }
  bool advance(std::optional<uint64_t> edge){
    if(!pending_ || !edge)return false;
    if(!requestedEdge_ || *edge<*requestedEdge_){requestedEdge_=edge;return false;}
    if(*edge==*requestedEdge_)return false;
    pending_=false;busy_=0;++latches;return true;
  }
  uint16_t known()const{return known_;}
  uint16_t busy()const{return busy_;}
  bool pending()const{return pending_;}
};
}
