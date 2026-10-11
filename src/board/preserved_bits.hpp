#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
namespace manta {
// Narrow diagnostic provenance for unchanged register bits. This observes
// original instructions; it never executes or patches them. Each unknown bit
// retains its exact banked byte and bit identity. Only a write back to that
// identity can preserve it. Decisions, addresses and new device uses reject.
class PreservedBits {
  struct Bit { uint32_t byte=0; uint8_t bit=0; bool operator==(const Bit &) const=default; };
  using Value=std::array<Bit,32>;
  std::array<Value,32> registers_{};
  std::map<uint32_t,std::array<Bit,8>> memory_;
  bool started_=false;
  static uint32_t backingAddress(uint32_t a) {
    if(a>=0x40000000 && a<0x48000000)return 0x08000000+a-0x40000000;
    if(a>=0x80000000 && a<0xc0000000)return a&0x1fffffff;
    return a;
  }
  static bool unknown(const Value &v) { for(auto b:v) if(b.byte) return true; return false; }
public:
  void seed(unsigned rt,unsigned width,uint32_t known,const std::function<uint32_t(unsigned)> &key) {
    started_=true;
    auto &v=registers_.at(rt); v={};
    for(unsigned i=0;i<width*8;++i) if(!(known&(uint32_t(1)<<i))) v[i]={key(i/8),uint8_t(i%8)};
  }
  uint32_t mask(unsigned rt) const {
    uint32_t m=0; for(unsigned i=0;i<32;++i) if(registers_.at(rt)[i].byte) m|=uint32_t(1)<<i;
    return m;
  }
  bool memoryUnknown(uint32_t address) const { return memory_.contains(address); }
  auto unknownMemoryBytes() const { return memory_.size(); }
  std::string observe(uint32_t w,const std::function<uint32_t(unsigned)> &reg,
      const std::function<uint32_t(uint32_t)> &deviceKey,
      const std::function<void(uint32_t,unsigned,uint32_t)> &preserveWrite) {
    if(!started_)return {};
    unsigned op=w>>26,rs=(w>>21)&31,rt=(w>>16)&31,rd=(w>>11)&31,fn=w&63,shift=(w>>6)&31;
    auto use=[&](unsigned r){return unknown(registers_[r]);};
    auto reject=[](){return std::string("unknown preserved register bit affects guest decision/address/device");};
    auto assign=[&](unsigned dest,const Value &v){if(dest)registers_[dest]=v;};
    Value result{};
    if(op==31 && fn==0) {
      unsigned length=rd+1;
      if(shift+length>32)return "invalid EXT during preserved-bit provenance";
      for(unsigned i=0;i<length;++i)result[i]=registers_[rs][shift+i];
      assign(rt,result);return {};
    }
    if(op==31 && rs==0 && (w&0x7ff)==0x620) {
      for(unsigned i=0;i<32;++i)result[i]=registers_[rt][i<16?i:15];
      assign(rd,result);return {};
    }
    if(op==0 && (fn==0 || fn==2 || fn==3)) {
      for(unsigned i=0;i<32;++i) {
        if(fn==0) { if(i>=shift) result[i]=registers_[rt][i-shift]; }
        else if(i+shift<32) result[i]=registers_[rt][i+shift];
        else if(fn==3) result[i]=registers_[rt][31];
      }
      assign(rd,result);return {};
    }
    if(op==12 || op==13) {
      result=registers_[rs];
      for(unsigned i=0;i<32;++i) {
        bool bit=i<16 && (w&(1u<<i));
        if(op==12 ? !bit : bit) result[i]={};
      }
      assign(rt,result);return {};
    }
    if(op==0 && (fn==36 || fn==37 || fn==38 || fn==39)) {
      if((fn==38 || fn==39) && (use(rs)||use(rt))) return reject();
      for(unsigned i=0;i<32;++i) {
        auto a=registers_[rs][i],b=registers_[rt][i];
        bool av=reg(rs)&(uint32_t(1)<<i),bv=reg(rt)&(uint32_t(1)<<i);
        if(fn==36 && ((!a.byte&&!av)||(!b.byte&&!bv))) continue;
        if(fn==37 && ((!a.byte&&av)||(!b.byte&&bv))) continue;
        if(a.byte && b.byte && a!=b) return reject();
        result[i]=a.byte?a:b;
      }
      assign(rd,result);return {};
    }
    if(op==0 && (fn==33 || fn==35) && (use(rs)||use(rt))) {
      if(reg(rt)==0 && !use(rt)) assign(rd,registers_[rs]);
      else if(fn==33 && reg(rs)==0 && !use(rs)) assign(rd,registers_[rt]);
      else if(fn==33 && !((mask(rs)|(reg(rs)&~mask(rs))) & (mask(rt)|(reg(rt)&~mask(rt))))) {
        // Disjoint possible-one sets cannot carry. This is the original
        // byte-pair concatenation, retaining each sampled bit's identity.
        for(unsigned i=0;i<32;++i) result[i]=registers_[rs][i].byte ? registers_[rs][i]:registers_[rt][i];
        assign(rd,result);
      }
      else return reject();
      return {};
    }
    if(op==0 && (fn==10 || fn==11)) {
      if(use(rt)) return reject();
      if(fn==10 ? reg(rt)==0 : reg(rt)!=0) assign(rd,registers_[rs]);
      return {};
    }
    unsigned width=op==36 || op==40 ? 1 : op==37 || op==41 ? 2 : op==35 || op==43 ? 4 : 0;
    if(width) {
      if(use(rs)) return reject();
      auto a=backingAddress(reg(rs)+uint32_t(int32_t(int16_t(w))));
      bool store=op>=40;
      if(store) {
        uint32_t m=mask(rt)&(width==4?0xffffffffu:(1u<<(width*8))-1);
        if(a>=0x1f000000 && a<0x1f400000 && m) {
          for(unsigned i=0;i<width*8;++i) if(m&(uint32_t(1)<<i)) {
            if(registers_[rt][i]!=Bit{deviceKey(a+i/8),uint8_t(i%8)}) return reject();
          }
          preserveWrite(a,width,m);
        } else if(a<0x10000000) {
          for(unsigned byte=0;byte<width;++byte) {
            std::array<Bit,8> v{};
            for(unsigned bit=0;bit<8;++bit) v[bit]=registers_[rt][byte*8+bit];
            if(m&(255u<<(byte*8)))memory_[a+byte]=v;
            else memory_.erase(a+byte);
          }
        } else if(m)return reject();
      } else {
        for(unsigned byte=0;byte<width;++byte) if(memory_.contains(a+byte))
          for(unsigned bit=0;bit<8;++bit)result[byte*8+bit]=memory_.at(a+byte)[bit];
        assign(rt,result); // MMIO source is seeded separately after this dispatch
      }
      return {};
    }
    int dest=-1; bool useRs=false,useRt=false;
    if(op==0) {
      if(fn==16 || fn==18)dest=rd;
      else if(fn==8 || fn==9){useRs=true;if(fn==9)dest=rd;}
      else if(fn==12 || fn==13 || fn==15){}
      else {useRs=useRt=true;dest=rd;}
    } else if(op==2 || op==3){if(op==3)dest=31;}
    else if(op==15)dest=rt;
    else if(op==1 || op==6 || op==7 || op==22 || op==23)useRs=true;
    else if(op==4 || op==5 || op==20 || op==21)useRs=useRt=true;
    else if(op>=8 && op<=14){useRs=true;dest=rt;}
    else if(op==16){if(rs==0)dest=rt;else if(rs==4)useRt=true;}
    else if(op==47 || op==51)useRs=true;
    else if(op==28){useRs=useRt=true;dest=rd;}
    else if(op==31){useRs=useRt=true;dest=(fn==32?rd:rt);}
    else {for(unsigned r=1;r<32;++r)if(use(r))return "unsupported instruction during preserved-bit provenance";}
    if((useRs&&use(rs)) || (useRt&&use(rt)))return reject();
    if(dest>0)registers_[dest]={};
    return {};
  }
};
}
