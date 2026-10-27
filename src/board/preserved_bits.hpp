#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cstdio>
#include <bitset>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
namespace manta {
// Narrow diagnostic provenance for unchanged register bits. This observes
// original instructions; it never executes or patches them. Each unknown bit
// retains its exact banked byte and bit identity. Only a write back to that
// identity can preserve it. Decisions requiring unknown bits, addresses and
// new device uses reject; a known unequal bit may prove an equality predicate.
class PreservedBits {
  friend void ioClass(snap::Ar&, PreservedBits&);
  struct Bit { uint32_t byte=0; uint8_t bit=0; bool inverted=false; bool operator==(const Bit &) const=default; };
  using Value=std::array<Bit,32>;
  std::array<Value,32> registers_{};
  std::map<uint32_t,std::array<Bit,8>> memory_;
  // Negative filter in front of memory_: a set bit means a 4 KB page (hashed) MAY hold an entry; a clear bit proves it holds none. Never cleared.
  std::bitset<65536> pageMaybe_;
  static size_t pageSlot(uint32_t a){return (a>>12)&0xffff;}
  bool maybeUnknown(uint32_t a)const{return pageMaybe_[pageSlot(a)];}
  void noteUnknown(uint32_t a){pageMaybe_[pageSlot(a)]=true;}
  bool started_=false;
  bool insPropagation_=false;   // opt-in (--exploratory-provenance-extensions): INS keeps the identity of untouched bits
  uint32_t live_=0;   // bit r set when registers_[r] holds any unknown bit (fast path when zero)
  void refreshLive(){live_=0;for(unsigned r=0;r<32;++r)if(unknown(registers_[r]))live_|=1u<<r;}
public:
  void enableInsPropagation(){insPropagation_=true;}
  static uint32_t backingAddress(uint32_t a) {
    if(a>=0x40000000 && a<0x58000000)return 0x08000000+a-0x40000000;
    if(a>=0x80000000 && a<0xc0000000)return a&0x1fffffff;
    return a;
  }
private:
  static bool unknown(const Value &v) { for(auto b:v) if(b.byte) return true; return false; }
public:
  void seed(unsigned rt,unsigned width,uint32_t known,const std::function<uint32_t(unsigned)> &key) {
    started_=true;
    auto &v=registers_.at(rt); v={};
    for(unsigned i=0;i<width*8;++i) if(!(known&(uint32_t(1)<<i))) v[i]={key(i/8),uint8_t(i%8)};
    if(unknown(v))live_|=1u<<rt; else live_&=~(1u<<rt);
  }
  void restored(){refreshLive();pageMaybe_.reset();for(auto &kv:memory_)noteUnknown(kv.first);}   // after the snapshot loader rewrote registers_
  uint32_t mask(unsigned rt) const {
    if(!(live_>>rt&1))return 0;
    uint32_t m=0; for(unsigned i=0;i<32;++i) if(registers_.at(rt)[i].byte) m|=uint32_t(1)<<i;
    return m;
  }
  // DIAGNOSTIC join support: mark a RAM byte as holding an unknown value with the given identity.
  void markMemoryUnknown(uint32_t address,uint32_t key){
    started_=true;
    std::array<Bit,8> v{};for(unsigned bit=0;bit<8;++bit)v[bit]={key,uint8_t(bit)};
    memory_[backingAddress(address)]=v;noteUnknown(backingAddress(address));
  }
  // Observation only: per-bit origin (originating register-byte key and bit) of an unknown RAM byte, for diagnostics.
  std::string describeMemory(uint32_t address) const {
    auto it=memory_.find(backingAddress(address));if(it==memory_.end())return {};
    std::string out;char buf[48];
    for(unsigned bit=0;bit<8;++bit){const auto &x=it->second[bit];if(!x.byte)continue;
      snprintf(buf,sizeof buf,"%sbit%u<-%08x.%u%s",out.empty()?"":" ",bit,x.byte,unsigned(x.bit),x.inverted?"(inv)":"");out+=buf;}
    return out;
  }
  bool memoryUnknown(uint32_t address) const { return maybeUnknown(backingAddress(address)) && memory_.contains(backingAddress(address)); }
  void overwriteKnownMemory(uint32_t address,size_t size) {
    for(size_t i=0;i<size;++i){auto k=backingAddress(address+uint32_t(i));if(maybeUnknown(k))memory_.erase(k);}
  }
  bool exactByteOrigin(unsigned rt,uint32_t byte)const {
    if(mask(rt)!=255)return false;
    for(unsigned i=0;i<8;++i)if(registers_.at(rt)[i]!=Bit{byte,uint8_t(i)})return false;
    return true;
  }
  bool exactFieldOrigin(unsigned rt,uint32_t byte,uint32_t field,uint32_t inverted=0)const{
    if(mask(rt)!=field)return false;
    for(unsigned i=0;i<32;++i)if(field&(uint32_t(1)<<i))
      if(registers_.at(rt)[i]!=Bit{byte,uint8_t(i),bool(inverted&(uint32_t(1)<<i))})return false;
    return true;
  }
  void scenarioXorImmediate(unsigned rt,uint16_t bits){
    for(unsigned i=0;i<16;++i)if(registers_.at(rt)[i].byte && (bits&(1u<<i)))
      registers_.at(rt)[i].inverted=!registers_.at(rt)[i].inverted;
  }
  auto unknownMemoryBytes() const { return memory_.size(); }
  // Fast-path support for the per-instruction hook (identical results, no state change): true when observe() can neither change state nor reject
  // for a NON-memory instruction (no register holds an unknown bit), resp. for a memory instruction at raw effective address ea of `width` bytes.
  bool quiet() const { return !started_ || !live_; }
  bool memoryClean(uint32_t ea, unsigned width) const {
    uint32_t a = backingAddress(ea);
    return !maybeUnknown(a) && !maybeUnknown(a + width - 1);
  }
  std::string observe(uint32_t w,const std::function<uint32_t(unsigned)> &reg,
      const std::function<uint32_t(uint32_t)> &deviceKey,
      const std::function<void(uint32_t,unsigned,uint32_t)> &preserveWrite) {
    if(!started_)return {};
    unsigned op=w>>26,rs=(w>>21)&31,rt=(w>>16)&31,rd=(w>>11)&31,fn=w&63,shift=(w>>6)&31;
    // No register holds an unknown bit: every instruction below except EXT and loads/stores leaves the state unchanged and cannot reject.
    if(!live_ && op!=31 && op!=35 && op!=36 && op!=37 && op!=40 && op!=41 && op!=43)return {};
    auto use=[&](unsigned r){return (live_>>r&1)!=0;};
    auto reject=[](){return std::string("unknown preserved register bit affects guest decision/address/device");};
    auto assign=[&](unsigned dest,const Value &v){if(dest){registers_[dest]=v;if(unknown(v))live_|=1u<<dest;else live_&=~(1u<<dest);}};
    Value result{};
    if(op==31 && fn==0) {
      unsigned length=rd+1;
      if(shift+length>32)return "invalid EXT during preserved-bit provenance";
      for(unsigned i=0;i<length;++i)result[i]=registers_[rs][shift+i];
      assign(rt,result);return {};
    }
    if(op==31 && fn==4 && insPropagation_) {
      // INS rt, rs, pos, size: bits outside [pos,pos+size) keep rt's identity; the inserted field comes from rs, which must be known.
      unsigned msb=rd,lsb=shift;
      if(msb<lsb)return "invalid INS during preserved-bit provenance";
      if(use(rs))return reject();
      result=registers_[rt];
      for(unsigned i=lsb;i<=msb && i<32;++i)result[i]={};
      assign(rt,result);return {};
    }
    if(op==31 && rs==0 && (w&0x7ff)==0x620) {
      for(unsigned i=0;i<32;++i)result[i]=registers_[rt][i<16?i:15];
      assign(rd,result);return {};
    }
    if(op==0 && (fn==4 || fn==6 || fn==7)) {
      if(use(rs))return reject();
      shift=reg(rs)&31;
      fn=fn==4?0:fn==6?2:3;
      for(unsigned i=0;i<32;++i) {
        if(fn==0) { if(i>=shift)result[i]=registers_[rt][i-shift]; }
        else if(i+shift<32)result[i]=registers_[rt][i+shift];
        else if(fn==3)result[i]=registers_[rt][31];
      }
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
        } else if(a<0x1f000000) {
          for(unsigned byte=0;byte<width;++byte) {
            std::array<Bit,8> v{};
            for(unsigned bit=0;bit<8;++bit) v[bit]=registers_[rt][byte*8+bit];
            if(m&(255u<<(byte*8))){memory_[a+byte]=v;noteUnknown(a+byte);}
            else if(maybeUnknown(a+byte))memory_.erase(a+byte);
          }
        } else if(m)return reject();
      } else {
        for(unsigned byte=0;byte<width;++byte) if(maybeUnknown(a+byte) && memory_.contains(a+byte))
          for(unsigned bit=0;bit<8;++bit)result[byte*8+bit]=memory_.at(a+byte)[bit];
        assign(rt,result); // MMIO source is seeded separately after this dispatch
      }
      return {};
    }
    if((op==4 || op==5 || op==20 || op==21) &&
       ((reg(rs)^reg(rt)) & ~(mask(rs)|mask(rt)))) {
      // A known unequal bit proves inequality for EVERY completion of the
      // unknown bits. Let the CPU execute its original branch; retain all
      // invalid origins for later stores/uses. Poison alone is never proof.
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
    if(dest>0){registers_[dest]={};live_&=~(1u<<dest);}
    return {};
  }
};
}
