#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace manta {
// Coarse configurable CPI, not a calibrated pipeline/cache/bus model. All
// instructions still retire in the existing CPU. One shared cycle domain feeds
// peripheral scheduling and CP0 Count (one Count increment per two CPU cycles).
class VirtualTime {
  uint32_t cpi_=1;
public:
  explicit VirtualTime(uint64_t cpi=1){
    if(cpi!=1 && cpi!=2 && cpi!=4 && cpi!=8 && cpi!=16)
      throw std::runtime_error("exploratory CPI must be 1, 2, 4, 8 or 16");
    cpi_=uint32_t(cpi);
  }
  uint64_t cycles(uint64_t instructions)const{
    if(instructions>std::numeric_limits<uint64_t>::max()/cpi_)
      throw std::runtime_error("virtual CPU cycle counter overflow");
    return instructions*cpi_;
  }
  uint32_t countDelta(uint64_t instructions)const{
    return cpi_==1 ? uint32_t((instructions&1)==0) : cpi_/2;
  }
  uint32_t cpi()const{return cpi_;}
};
}
