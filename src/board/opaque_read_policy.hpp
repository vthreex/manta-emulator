#pragma once
#include "storage/sha256.hpp"
#include <sstream>
#include <vector>
#include <stdexcept>
#include <set>
#include <tuple>
namespace manta {
// Debugger policy grants INVALID observation only. There is deliberately no
// value, known-mask, branch result, PC override, write or acknowledgment field.
struct OpaqueReadRule {
  uint32_t pc,address,width,start,size;
  std::string hash;
};
inline std::vector<OpaqueReadRule> parseOpaqueReadPolicy(const std::string& text){
  if(text.size()>65536)throw std::runtime_error("opaque policy exceeds64KiB");
  auto number=[](const std::string& token){size_t used=0;auto n=std::stoull(token,&used,0);
    if(used!=token.size() || n>0xffffffffu)throw std::runtime_error("invalid opaque policy number");
    return uint32_t(n);};
  std::istringstream in(text);std::string line;std::vector<OpaqueReadRule> rules;
  std::set<std::tuple<uint32_t,uint32_t,uint32_t>> keys;
  while(std::getline(in,line)){
    auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos || line[first]=='#')continue;
    std::istringstream row(line);std::string p,a,w,s,n,h,extra;
    if(!(row>>p>>a>>w>>s>>n>>h) || row>>extra)throw std::runtime_error("opaque policy requires PC address width code-start code-size SHA256 only");
    OpaqueReadRule r{number(p),number(a),number(w),number(s),number(n),h};
    auto end=uint64_t(r.start)+r.size;
    bool code=(r.start>=0x803b0180 && end<=0x806b8ce0) || (r.start>=0xbfc00000 && end<=0xbfc20000);
    bool digest=h.size()==64 && h.find_first_not_of("0123456789abcdef")==std::string::npos;
    if(!code || !digest || !r.size || r.size>32768 || (r.start&3) || (r.pc&3) || (r.size&3) ||
       r.pc<r.start || uint64_t(r.pc)+4>end || (r.width!=1 && r.width!=2 && r.width!=4) ||
       r.address<0x1f000000 || uint64_t(r.address)+r.width>0x1f400000 ||
       !keys.emplace(r.pc,r.address,r.width).second || rules.size()>=256)
      throw std::runtime_error("opaque policy range/hash/identity invalid or duplicate");
    rules.push_back(r);
  }
  return rules;
}
inline bool validateOpaqueReadRule(const OpaqueReadRule& rule,uint32_t pc,uint32_t address,unsigned width,
    const std::vector<uint8_t>& code){
  if(rule.pc!=pc || rule.address!=address || rule.width!=width || code.size()!=rule.size ||
     pc<rule.start || uint64_t(pc)+4>uint64_t(rule.start)+code.size() || sha256(code)!=rule.hash)return false;
  auto off=pc-rule.start;uint32_t word=0;
  for(unsigned i=0;i<4;++i)word|=uint32_t(code.at(off+i))<<(8*i);
  auto op=word>>26,rt=(word>>16)&31;
  return rt && ((op==36 && width==1) || (op==37 && width==2) || (op==35 && width==4));
}
}
