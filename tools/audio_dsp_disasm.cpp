// Candidate ISA comparison only. Neither DSP execution nor register aliases
// are inferred from a successfully formatted instruction.
#include "third_party/adsp21xx/emu.h"
#include "third_party/adsp21xx/2100dasm.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <map>
#include <stdexcept>
struct Words:util::disasm_interface::data_buffer {
  std::map<uint16_t,uint32_t> data;
  uint32_t r32(offs_t pc)const override {
    auto it=data.find(uint16_t(pc));if(pc>65535 || it==data.end())throw std::runtime_error("unwritten candidate program word");return it->second;
  }
};
int main(int argc,char** argv){try{
  if(argc!=5 || std::string(argv[1])!="--candidate-adsp21xx")throw std::runtime_error("usage: audio_dsp_disasm --candidate-adsp21xx addressed-words.bin start end");
  std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t> b(std::istreambuf_iterator<char>(f),{});
  if(b.empty() || b.size()%5 || b.size()>65536*5)throw std::runtime_error("invalid bounded addressed-word image");
  Words w;for(size_t i=0;i<b.size();i+=5){uint16_t a=uint16_t(b[i]|(b[i+1]<<8));uint32_t v=b[i+2]|(b[i+3]<<8)|(b[i+4]<<16);
    if(!w.data.emplace(a,v).second)throw std::runtime_error("duplicate word address");}
  auto first=std::stoul(argv[3],nullptr,0),end=std::stoul(argv[4],nullptr,0);
  if(first>=end || end>65536)throw std::runtime_error("invalid candidate disassembly range");
  adsp21xx_disassembler d;
  std::cout<<"ARCHITECTURE_HYPOTHESIS ADSP-21xx decoding; MStar extensions and memory map unconfirmed; instructions executed=0\n";
  for(auto pc=first;pc<end;++pc){
    auto it=w.data.find(uint16_t(pc));if(it==w.data.end())continue;
    std::cout<<std::hex<<std::setfill('0')<<std::setw(4)<<pc<<' '<<std::setw(6)<<it->second<<' ';
    d.disassemble(std::cout,uint32_t(pc),w,w);std::cout<<'\n';
  }
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
