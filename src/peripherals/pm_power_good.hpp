#pragma once
#include "snapshot/archive.hpp"
#include "cpu/vd_mcu.hpp"
#include <optional>
namespace manta {
// Related MStar amber5/kronus boot.S names (XDATA 0EC2 & 7)==7
// CheckPowerGood. The individual MSD309 detectors and their thresholds are
// NOT documented. This aggregate wiring is an explicitly configured hypothesis,
// never a reset value or a status that advances on reads.
class PmPowerGood {
  friend void ioClass(snap::Ar&, PmPowerGood&);
  std::optional<bool> externalGood_;
public:
  uint64_t transitions=0;
  void input(bool good){
    if(externalGood_!=good){externalGood_=good;++transitions;}
  }
  void disconnect(){if(externalGood_){externalGood_.reset();++transitions;}}
  bool configured()const{return externalGood_.has_value();}
  VdByte sample()const{
    return {uint8_t(externalGood_.value_or(false)?0xa7:0xa0),
            uint8_t(externalGood_?7:0),0x0ec2};
  }
};
}
