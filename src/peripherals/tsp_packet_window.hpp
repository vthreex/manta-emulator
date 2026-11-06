#pragma once
#include "cpu/openrisc.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <stdexcept>

namespace manta {
// Candidate single-packet read window, derived from the ORIGINAL TSP program:
// FF8 selects a byte cursor, 3C4 waits/acks bit8, 1004 reads the selected byte.
// 138C releases the packet with command10 followed by0. This does not select
// PID filters, assemble sections, fabricate DMA completion or discover services.
class TspPacketWindow {
  std::array<uint8_t,188> packet_{};
  unsigned cursor_=0,filter_=0;
  bool occupied_=false,ready_=false,release_=false;
public:
  uint64_t captured=0,released=0;
  bool capture(std::span<const uint8_t> packet,unsigned sectionFilter){
    if(packet.size()!=188 || packet[0]!=0x47 || sectionFilter>=32)
      throw std::runtime_error("TSP packet window requires an actual188-byte packet and valid section filter");
    if(occupied_)return false;
    std::copy(packet.begin(),packet.end(),packet_.begin());
    cursor_=0;filter_=sectionFilter;occupied_=ready_=true;release_=false;++captured;return true;
  }
  bool occupied()const{return occupied_;}
  static bool releaseCommand(OrWord value){
    if(value.known==0xffffffffu)return value.value==0 || value.value==0x10;
    // ORIGINAL1E0C..1E14 reads command, ORs bit4 and carries every unknown
    // bit back to the SAME register. Admit only that exact bit-scoped update.
    return value.known==0x10 && value.origin==0x210218 && (value.value&0x10);
  }
  unsigned cursor()const{return cursor_;}
  unsigned filter()const{return filter_;}
  std::span<const uint8_t> packet()const{return packet_;}
  void serialize(snap::Ar& a){a.tag("TspPacketWindow1");a(packet_,cursor_,filter_,occupied_,ready_,release_,captured,released);}
  std::optional<OrWord> read(uint32_t address)const{
    if(address==0x210214)return OrWord{ready_?0x100u:0u,0x100,address};
    if(address==0x21021c)return OrWord{filter_,occupied_?0xffffffffu:0u,address};
    if(address!=0x210204 && address!=0x210208)return {};
    OrWord result{0,0,address};
    for(unsigned i=0;i<4;++i){const auto offset=cursor_+(address-0x210204)+i;
      if(occupied_ && offset<packet_.size()){result.value|=uint32_t(packet_[offset])<<(8*i);result.known|=0xffu<<(8*i);}}
    return result;
  }
  bool write(uint32_t address,OrWord value){
    if(address==0x210200){
      if(value.known!=0xffffffffu || value.value>=188)throw std::runtime_error("TSP packet cursor is unknown or outside the packet");
      cursor_=value.value;ready_=occupied_;return true;
    }
    if(address==0x210214){
      if(!(value.known&0x100) || (value.value&0x100))throw std::runtime_error("unsupported TSP packet-ready write");
      ready_=false;return true;
    }
    if(address==0x210218 && releaseCommand(value)){
      if(value.value&0x10)release_=true;
      else if(release_){if(occupied_)++released;occupied_=ready_=release_=false;}
      return true;
    }
    return false;
  }
};
}
