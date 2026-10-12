#pragma once
#include <cstdint>
#include <optional>
namespace manta {
// Experimental external grounded FB input, NOT validated Titania4 ADC transfer.
// Related MSD309 schematic has FB pull-down. Original code configures 4B10,
// then reads low nibble of 4B15 and compares with four. Only code zero at an
// explicitly grounded input is hypothesized; no connected-voltage conversion.
class ScartGroundedInput {
  bool grounded_=false,initialized_=false,configured_=false;
  uint64_t reads_=0;
public:
  void ground(){grounded_=true;}
  void configure(uint16_t value,uint16_t known){
    // Observed original boot setup, followed by its scaler-table reprogramming.
    // These are recognized configuration sequences, NOT decoded power bits.
    if((known&0x7f3f)!=0x7f3f){configured_=false;return;}
    auto fields=value&0x7f3f;
    if(fields==0x7f20)initialized_=true;
    configured_=initialized_ && (fields==0x7f20 || fields==0x7f10 || fields==0x10);
  }
  std::optional<uint8_t> sample()const{return grounded_&&configured_?std::optional<uint8_t>(0):std::nullopt;}
  void observe(){++reads_;}
  uint64_t reads()const{return reads_;}
};
}
