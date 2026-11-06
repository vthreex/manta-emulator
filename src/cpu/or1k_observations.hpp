#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace manta {
// Optional conservative shadow for the existing multimedia CPU. It never
// changes architectural values or executes an instruction. Invalid bits may
// be copied/masked/stored as data; decisions, addresses and control writes
// require known operands. Unsupported invalid arithmetic stops conservatively.
class Or1kObservations {
  std::array<uint32_t,32> known_{};
  std::array<uint32_t,32> origins_{};
  static constexpr uint32_t all=0xffffffffu;
  void require(unsigned r,const char* use)const{
    if(known_[r]!=all){char message[160];std::snprintf(message,sizeof(message),"INVALID coprocessor r%u bits (known=%08x origin=%08x) affect %s",r,known_[r],origins_[r],use);throw std::runtime_error(message);}
  }
  void result(unsigned r,uint32_t known,uint32_t origin=0){if(r){known_[r]=known;origins_[r]=known==all?0:origin;}}
  static uint32_t extend(uint32_t known,unsigned bits,bool sign){
    const uint32_t low=bits==32?all:(1u<<bits)-1;
    return (known&low)|((!sign || (known&(1u<<(bits-1))))?~low:0);
  }
public:
  struct Access { unsigned kind=0,dest=0,size=0;bool sign=false;uint32_t known=all,origin=0; } access;
  Or1kObservations(){reset();}
  void reset(){known_.fill(all);origins_.fill(0);access={};}
  uint32_t known(unsigned r)const{return known_[r];}
  void serialize(snap::Ar& a){a.tag("orobserv");a(known_,origins_);}
  void loaded(uint32_t mask,uint32_t origin=0){result(access.dest,extend(mask,access.size*8,access.sign),origin);}
  void begin(uint32_t w,const std::function<uint32_t(unsigned)>& reg,bool flag){
    access={};
    const unsigned op=w>>26,D=(w>>21)&31,A=(w>>16)&31,B=(w>>11)&31;
    const uint32_t imm=w&0xffff,ka=known_[A],kb=known_[B],va=reg(A),vb=reg(B);
    const uint32_t oa=origins_[A],ob=origins_[B],combined=ka==all?ob:kb==all?oa:oa==ob?oa:0;
    const auto bitAnd=[&]{return (ka&kb)|(ka&~va)|(kb&~vb);};
    const auto bitOr=[&]{return (ka&kb)|(ka&va)|(kb&vb);};
    const auto shift=[&](unsigned sub,unsigned n){
      n&=31;if(!n)return ka;
      if(sub==0)return uint32_t((ka<<n)|((1u<<n)-1));
      if(sub==1)return uint32_t((ka>>n)|(all<<(32-n)));
      if(sub==2)return uint32_t((ka>>n)|((ka&0x80000000u)?all<<(32-n):0));
      return uint32_t((ka>>n)|(ka<<(32-n)));
    };
    switch(op){
    case 0x01:result(9,all);break;
    case 0x06:result(D,all);break;
    case 0x11:require(B,"jump address");break;
    case 0x12:require(B,"call address");result(9,all);break;
    case 0x13:require(A,"MAC arithmetic");break;
    case 0x21:case 0x22:case 0x23:case 0x24:case 0x25:case 0x26:
      require(A,"load address");access={1,D,(op==0x23||op==0x24)?1u:(op==0x25||op==0x26)?2u:4u,op==0x22||op==0x24||op==0x26,all};result(D,0);break;
    case 0x27:case 0x28:
      if(op==0x27 && imm==0)result(D,ka,oa);else{require(A,"arithmetic/flags");result(D,all);}break;
    case 0x29:result(D,ka|~imm,oa);break;
    case 0x2a:result(D,ka|imm,oa);break;
    case 0x2b:result(D,ka,oa);break;
    case 0x2c:require(A,"multiply");result(D,all);break;
    case 0x2d:{require(A,"SPR address");const auto spr=va|imm;result(D,spr>=1024 && spr<1056?known_[spr-1024]:all);break;}
    case 0x2e:if((w&63)>31)require(A,"reserved shift");result(D,shift((w>>6)&3,w&31),oa);break;
    case 0x2f:require(A,"comparison");break;
    case 0x30:require(A,"SPR address");require(B,"control register write");break;
    case 0x31:require(A,"MAC arithmetic");require(B,"MAC arithmetic");break;
    case 0x35:case 0x36:case 0x37:
      require(A,"store address");access={2,0,op==0x35?4u:op==0x36?1u:2u,false,kb,ob};break;
    case 0x38:{
      const unsigned fn=w&15,sub=(w>>6)&3;
      switch(fn){
      case 3:result(D,bitAnd(),combined);break;
      case 4:result(D,bitOr(),combined);break;
      case 5:result(D,ka&kb,combined);break;
      case 8:if((kb&31)!=31)throw std::runtime_error("INVALID coprocessor shift count");result(D,shift(sub,vb),oa);break;
      case 12:result(D,extend(ka,(sub==0||sub==2)?16:8,sub<2),oa);break;
      case 13:result(D,ka,oa);break;
      case 14:result(D,flag?ka:kb,flag?oa:ob);break;
      case 15:require(A,"bit scan");result(D,all);break;
      default:require(A,"arithmetic/flags");require(B,"arithmetic/flags");result(D,all);break;
      }break;
    }
    case 0x39:require(A,"comparison");require(B,"comparison");break;
    default:break;
    }
    known_[0]=all;
  }
};
}
