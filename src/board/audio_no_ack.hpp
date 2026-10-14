#pragma once
#include <cstdint>
namespace manta {
// Explicit symbolic scenario, NOT a DSP register/reset-value model. The
// reply byte stays invalid; only the verified guest's != E3 predicate is
// constrained. No instruction, PC, register value or hardware reply is set.
class AudioNoAck {
  bool pending_=false;
  uint32_t branch_=0;
public:
  uint64_t samples=0,comparisons=0,timeouts=0,returns=0;
  bool arm(uint32_t pc,uint32_t address,unsigned width,uint32_t caller,uint32_t known){
    if(pending_ || width!=1 || known!=0)return false;
    if(pc==0x804f9098 && address==0x1f225ab4 && caller==0x804f7a28)branch_=caller;
    else if(pc==0x804f9050 && address==0x1f225cb8 && caller==0x804f7b10)branch_=caller;
    else return false;
    pending_=true;++samples;return true;
  }
  bool constrain(uint32_t pc,uint32_t instruction,uint32_t invalidValue,uint32_t expected,
      uint32_t expectedUnknown,bool exactReplyOrigin,uint32_t retry){
    if(!pending_ || pc!=branch_ || instruction!=0x1452fff7 || expected!=0xe3 || expectedUnknown ||
        !exactReplyOrigin || invalidValue==expected || retry<1 || retry>100)return false;
    pending_=false;++comparisons;return true;
  }
  bool pending()const{return pending_;}
};
}
