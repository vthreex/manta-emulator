#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
namespace manta {
// Manta 804FF690/804FF49C: write address, two 16-bit port phases
// packing each 24-bit word, separate read address, request bit3, three
// readback bytes. No DSP ISA, reset contents, alive flag or mailbox modeled.
// Completion is atomic at the final port phase (functional timing model).
class AudioIdma {
  std::map<uint16_t,std::array<uint8_t,3>> words_;
  std::array<uint8_t,3> staged_{},readback_{};
  std::optional<uint16_t> writeAddress_,readAddress_;
  unsigned phase_=0;
  bool readbackKnown_=false,writeCompleted_=false,readCompleted_=false;
public:
  static bool readPort(unsigned offset){return offset==0x18 || offset==0x19 || offset==0x1c;}
  uint64_t wordWrites=0,wordReads=0;
  void address(bool read,uint16_t value){
    if(!read && phase_)throw std::runtime_error("audio IDMA address changes inside an unfinished word");
    if(read)readAddress_=value;else{writeAddress_=value;phase_=0;}
  }
  void invalidateAddress(bool read){
    if(!read && phase_)throw std::runtime_error("audio IDMA partial address inside an unfinished word");
    if(read)readAddress_.reset();else writeAddress_.reset();
  }
  bool data(unsigned port,uint8_t value){
    if(!writeAddress_)throw std::runtime_error("audio IDMA data before original write address");
    const unsigned order[]={0,1,0,1};
    if(port!=order[phase_])throw std::runtime_error("audio IDMA unsupported data-phase order");
    switch(phase_){case 0:staged_[1]=value;break;case 1:staged_[2]=value;break;
      case 2:staged_[0]=value;break;case 3:
        if(value)throw std::runtime_error("audio IDMA fourth byte is not the observed zero padding");
        words_[*writeAddress_]=staged_;++*writeAddress_;++wordWrites;writeCompleted_=true;break;}
    phase_=(phase_+1)%4;return phase_==0;
  }
  bool command(uint8_t value,uint8_t known){
    if(!(known&8) || !(value&8))return false;
    if(!readAddress_)throw std::runtime_error("audio IDMA read request without original read address");
    auto it=words_.find(*readAddress_);
    readbackKnown_=it!=words_.end();if(readbackKnown_)readback_=it->second;
    ++*readAddress_;++wordReads;readCompleted_=true;return true;
  }
  unsigned statusKnown()const{return (writeCompleted_?0x10:0)|(readCompleted_?8:0);}
  std::optional<uint8_t> read(unsigned offset)const{
    if(!readbackKnown_)return {};
    // 2D0C = middle byte, 2D0D = high, 2D0E = low.
    if(offset==0x18)return readback_[1];
    if(offset==0x19)return readback_[2];
    if(offset==0x1c)return readback_[0];
    return {};
  }
  size_t initializedWords()const{return words_.size();}
  std::vector<uint8_t> stateBytes()const{
    std::vector<uint8_t> b;for(const auto& [a,v]:words_){b.push_back(uint8_t(a));b.push_back(uint8_t(a>>8));b.insert(b.end(),v.begin(),v.end());}
    return b;
  }
};
}
