// Small standalone interface adapter for the unmodified BSD-3-Clause MAME
// disassembler. This supplies formatting/buffers only; no CPU is emulated.
#pragma once
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <vector>
using u32=uint32_t;
using offs_t=uint32_t;
inline uint32_t BIT(uint32_t v,unsigned first,unsigned count=1){return (v>>first)&uint32_t((uint64_t(1)<<count)-1);}
namespace util {
template<class... A>void stream_format(std::ostream& s,const char* format,A... args){
  auto n=std::snprintf(nullptr,0,format,args...);
  if(n<0)return;std::vector<char> b(size_t(n)+1);std::snprintf(b.data(),b.size(),format,args...);s<<b.data();
}
class disasm_interface {
public:
  struct data_buffer {virtual ~data_buffer()=default;virtual uint32_t r32(offs_t pc)const=0;};
  static constexpr uint32_t SUPPORTED=0x80000000,STEP_OUT=0x40000000,STEP_OVER=0x20000000,STEP_COND=0x10000000;
  virtual ~disasm_interface()=default;
  virtual u32 opcode_alignment()const=0;
  virtual offs_t disassemble(std::ostream&,offs_t,const data_buffer&,const data_buffer&)=0;
};
}
