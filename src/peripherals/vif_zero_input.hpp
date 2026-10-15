#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace manta {
// Explicit idealized steady-state experiment, NOT a disconnected-antenna
// measurement or a Titania4 reset model. Zero post-ADC signal energy has zero
// envelope; negative feedback with a positive reference requests maximum gain.
// PGA2 saturation/distribution, clock availability and settling time are
// hypotheses. VGA, carrier/HSYNC lock and all other observations stay invalid.
class VifZeroInput {
public:
  struct Field { uint8_t value=0xa5,known=0;bool operator==(const Field&)const=default; };
  // Original T4 addresses, correlated by the complete original initializer and
  // handler with the related MsVIF driver; never copy its different RIU bank.
  static constexpr std::array<uint32_t,9> configuration={
    0x1f224400,0x1f224600,0x1f224601,0x1f224610,0x1f224618,
    0x1f224628,0x1f224629,0x1f224604,0x1f224605};
  using Configuration=std::array<Field,configuration.size()>;
  static bool observation(uint32_t address){
    return address==0x1f224644 || address==0x1f22465d ||
      address==0x1f224650 || address==0x1f224651;
  }
private:
  bool enabled_=false,armed_=false,settled_=false;
  uint64_t settling_=0,due_=0,settlements_=0;
  Configuration config_{};
public:
  void configure(uint64_t cycles){
    if(!cycles)throw std::runtime_error("VIF ideal-zero hypothesis requires a positive explicit settling interval");
    if(enabled_)throw std::runtime_error("VIF ideal-zero input cannot be reconfigured during execution");
    enabled_=true;settling_=cycles;
  }
  bool enabled()const{return enabled_;}
  bool settled()const{return settled_;}
  uint64_t settlements()const{return settlements_;}
  uint64_t settlingCycles()const{return settling_;}
  void update(const Configuration& config,uint64_t tick){
    if(!enabled_)return;
    auto field=[&](unsigned i,uint8_t mask,uint8_t value){
      return (config[i].known&mask)==mask && (config[i].value&mask)==value;
    };
    bool ready=field(0,0x7f,0x7f) && field(1,0x0d,5) &&
      field(2,0x1a,0x1a) && config[3].known==255 && config[3].value>0 &&
      field(4,3,0) && (config[5].known&31)==31 && (config[6].known&31)==31 &&
      (config[5].value&31)<=(config[6].value&31) &&
      config[7].known==255 && config[8].known==255 &&
      (config[7].value || config[8].value);
    // Only consumed fields determine the configuration epoch. Unknown poison
    // in preserved high bits must not start/restart an analog experiment.
    constexpr std::array<uint8_t,9> masks={0x7f,0x0d,0x1a,255,3,31,31,255,255};
    bool changed=false;
    for(unsigned i=0;i<config.size();++i)
      changed|=((config[i].known^config_[i].known)&masks[i]) ||
        ((config[i].value^config_[i].value)&config[i].known&config_[i].known&masks[i]);
    config_=config;
    if(!ready){armed_=settled_=false;return;}
    if(!armed_ || changed){
      if(tick>UINT64_MAX-settling_)throw std::runtime_error("VIF settling deadline overflow");
      due_=tick+settling_;armed_=true;settled_=false;
    }
    if(!settled_ && tick>=due_){settled_=true;++settlements_;}
  }
  Field sample(uint32_t address)const{
    if(!enabled_ || !settled_)return {};
    if(address==0x1f224644)return {0,255};
    if(address==0x1f22465d)return {uint8_t(config_[6].value&31),31};
    return {}; // In particular, VGA is NOT inferred from PGA saturation.
  }
  std::vector<uint8_t> stateBytes()const{
    std::vector<uint8_t> out={uint8_t(enabled_),uint8_t(armed_),uint8_t(settled_)};
    for(auto value:{settling_,due_,settlements_})for(unsigned i=0;i<8;++i)out.push_back(uint8_t(value>>(8*i)));
    for(auto field:config_){out.push_back(field.value);out.push_back(field.known);}
    return out;
  }
};
}
