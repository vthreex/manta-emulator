#pragma once
#include <cstdint>
#include <utility>
namespace manta {
// Finite, conservative byte abstraction. Correlations between bytes are
// deliberately forgotten: this can lose known bits, never invent them.
struct McuValueMask {
  uint8_t value=0,known=255;
  bool contains(unsigned v)const{return ((v^value)&known)==0;}
};
template<class Visit> void mcuPossibleBytes(McuValueMask v,Visit visit){
  for(unsigned x=0;x<256;++x)if(v.contains(x))visit(x);
}
struct McuMaskMerge {
  unsigned value=0,known=0xffff;bool empty=true;
  void add(unsigned v){if(empty){value=v;empty=false;}else known&=~(value^v);}
  McuValueMask byte(unsigned shift=0)const{return {uint8_t(value>>shift),uint8_t(known>>shift)};}
};
struct McuAddMasks {McuValueMask result;uint8_t flagsKnown;};
enum class McuLogic { And, Or, Xor };
inline McuValueMask mcuLogicMasks(McuValueMask a,McuValueMask b,McuLogic op){
  if(op==McuLogic::And)return {uint8_t(a.value&b.value),uint8_t((a.known&b.known)|(a.known&~a.value)|(b.known&~b.value))};
  if(op==McuLogic::Or)return {uint8_t(a.value|b.value),uint8_t((a.known&b.known)|(a.known&a.value)|(b.known&b.value))};
  return {uint8_t(a.value^b.value),uint8_t(a.known&b.known)};
}
// ADD A,ACC reads the SAME byte twice. Preserve that correlation rather than
// treating the two observations as independent choices of unknown bits.
inline McuAddMasks mcuDoubleMasks(McuValueMask a){
  McuMaskMerge results,flags;
  mcuPossibleBytes(a,[&](unsigned x){
    const auto sum=x+x;results.add(sum&255);
    flags.add((sum>255?128:0)|(((x&15)*2>15)?64:0)|(((x^sum)&128)?4:0));
  });
  return {results.byte(),uint8_t(flags.known)};
}
inline McuAddMasks mcuAddMasks(McuValueMask a,McuValueMask b,bool carry,bool carryKnown){
  McuMaskMerge results,flags;
  mcuPossibleBytes(a,[&](unsigned x){mcuPossibleBytes(b,[&](unsigned y){
    for(unsigned c=0;c<2;++c)if(!carryKnown || c==unsigned(carry)){
      auto sum=x+y+c;results.add(sum&255);
      auto ac=((x&15)+(y&15)+c)>15;auto ov=((~(x^y)&(x^sum))&128)!=0;
      flags.add((sum>255?128:0)|(ac?64:0)|(ov?4:0));
    }
  });});
  return {results.byte(),uint8_t(flags.known)};
}
inline std::pair<McuValueMask,McuValueMask> mcuMultiplyMasks(McuValueMask a,McuValueMask b){
  McuMaskMerge results;
  mcuPossibleBytes(a,[&](unsigned x){mcuPossibleBytes(b,[&](unsigned y){results.add(x*y);});});
  return {results.byte(),results.byte(8)};
}
}
