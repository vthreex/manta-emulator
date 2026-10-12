#pragma once
#include <cstdint>
#include <optional>
#include <span>
namespace manta {
// Prove the loaded value is destroyed by ANDI/EXT before any other use.
// Intervening instructions may neither read nor replace that register.
inline std::optional<uint32_t> immediateReadMask(std::span<const uint32_t> words) {
  if(words.empty())return {};
  auto op=words[0]>>26, rt=(words[0]>>16)&31;
  if((op!=36 && op!=37) || !rt)return {};
  if(words.size()>=3 && words[1]==0x03e00008 && words[2]>>26==12 &&
      ((words[2]>>21)&31)==rt && ((words[2]>>16)&31)==rt)return words[2]&65535;
  for(size_t i=1;i<words.size();++i) {
    auto word=words[i], nextOp=word>>26, rs=(word>>21)&31, target=(word>>16)&31;
    if(nextOp==12 && rs==rt && target==rt)return word&65535;
    if(nextOp==31 && (word&63)==0 && rs==rt && target==rt) {
      unsigned count=((word>>11)&31)+1, shift=(word>>6)&31;
      if(count+shift<=32)return count==32?0xffffffffu:((1u<<count)-1)<<shift;
      return {};
    }
    bool independent=(nextOp==9 || nextOp==10 || nextOp==11 || nextOp==12 || nextOp==13 || nextOp==15) && target!=rt &&
        (nextOp==15 || rs!=rt);
    auto function=word&63;
    if(nextOp==0 && (function==0 || function==2 || function==3 || function==10 ||
         function==11 || function==33 || function==36 || function==37 || function==42 || function==43))
      independent=rs!=rt && target!=rt && ((word>>11)&31)!=rt;
    if(nextOp==43 || nextOp==41 || nextOp==40 || nextOp==35 || nextOp==36 || nextOp==37)
      independent=rs!=rt && target!=rt;
    if(!independent)return {};
  }
  return {};
}
}
