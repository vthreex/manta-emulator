#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
namespace manta {
// Explicit single-observation path constraint, not a PM register/reset model.
// Original instructions execute with an INVALID representative. No hardware
// byte, architectural register, PC, return value or instruction is assigned.
// Only the SHA-verified getter's boolean predicate may become known under
// the user-selected assumption that this observation is not RTC resume.
class PmActivityScenario {
  friend void ioClass(snap::Ar&, PmActivityScenario&);
  unsigned stage_=0;
public:
  uint64_t samples=0,predicates=0,branches=0;
  bool arm(uint32_t pc,uint32_t address,unsigned width,uint32_t caller,uint32_t known){
    if(stage_ || samples || pc!=0x80404bf4 || address!=0x1f001cd1 || width!=1 ||
       caller!=0x8046a198 || known)return false;
    stage_=1;++samples;return true;
  }
  bool xorPredicate(uint32_t pc,uint32_t word,uint32_t representative,bool exactOrigin){
    if(stage_!=1 || pc!=0x80404bfc || word!=0x38420008 || !exactOrigin ||
       representative>15 || representative==8)return false;
    stage_=2;return true;
  }
  bool resolvePredicate(uint32_t pc,uint32_t word,uint32_t representative,bool exactOrigin){
    if(stage_!=2 || pc!=0x80404c04 || word!=0x2c420001 || !exactOrigin ||
       !representative || representative>15)return false;
    stage_=3;++predicates;return true;
  }
  bool observeBranch(uint32_t pc,uint32_t word,uint32_t value,uint32_t unknown){
    if(stage_!=3 || pc!=0x8046a198 || word!=0x1040fec7 || value || unknown)return false;
    stage_=4;++branches;return true;
  }
};
}
