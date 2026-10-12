#pragma once
#include <array>
#include <optional>
#include <cstdint>
#include <stdexcept>
namespace manta {
// Eight-bit SAR result interface identified in original byte readers.
// Input/reference voltage and conversion latency are explicit fixtures.
class SarAdc {
  std::array<std::optional<unsigned>,2> inputs_{};
  std::array<std::optional<uint8_t>,2> result_{};
  bool running_=false;uint64_t due_=0,period_=0,conversions_=0;
public:
  void input(unsigned channel,unsigned millivolts){
    if(channel>=inputs_.size() || millivolts>3300)throw std::runtime_error("SAR external voltage outside fixture bounds");
    inputs_[channel]=millivolts;
  }
  void configure(uint8_t digital,uint8_t dk,uint8_t analog,uint8_t ak,uint8_t period,uint8_t pk,uint64_t tick){
    bool enable=(dk&0x60)==0x60 && (digital&0x60)==0x20 && (ak&3)==3 && (analog&3)==2 && pk==255;
    if(!enable){running_=false;result_={};return;}
    auto delay=(uint64_t(period)+1)*8;
    if(!running_ || period_!=delay){running_=true;period_=delay;due_=tick+delay;}
  }
  void advance(uint64_t tick){
    if(!running_ || tick<due_)return;
    auto elapsed=(tick-due_)/period_+1;conversions_+=elapsed;due_+=elapsed*period_;
    for(unsigned i=0;i<inputs_.size();++i)if(inputs_[i])result_[i]=uint8_t((*inputs_[i]*255)/3300);
  }
  std::optional<uint8_t> result(unsigned channel)const{return result_.at(channel);}
  uint64_t conversions()const{return conversions_;}
};
}
