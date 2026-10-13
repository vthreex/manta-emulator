// Executes the original AVD upload independently. No guessed XDATA/SFRs,
// DSP readiness or television boot progress are supplied by this diagnostic.
#include "cpu/vd_mcu.hpp"
#include "storage/sha256.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <set>
static void check(bool c,const char* s){if(!c)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
  check(argc==2 || argc==4,"supply the original 13312-byte AVD payload [disassembly start end]");
  std::ifstream f(argv[1],std::ios::binary);
  std::vector<uint8_t> code(std::istreambuf_iterator<char>(f),{});
  check(code.size()==13312 && manta::sha256(code)=="48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd",
    "original AVD size/hash mismatch");
  if(argc==4){
    auto storage=code;storage.resize(65536);
    em8051 dis{};dis.mCodeMem=storage.data();dis.mCodeMemMaxIdx=65535;reset(&dis,false);
    auto pc=std::stoul(argv[2],nullptr,0),end=std::stoul(argv[3],nullptr,0);
    check(pc<end && end<=code.size(),"disassembly range outside original AVD program");
    for(;pc<end;){char text[128];auto n=decode(&dis,uint16_t(pc),text);
      check(n && pc+n<=code.size(),"invalid disassembly range");std::cout<<std::hex<<pc<<' '<<text<<'\n';pc+=n;}
    return 0;
  }
  manta::AvdMcuProgram p;p.write(1,0,255);p.write(5,0x51,255);p.write(8,0,255);p.write(9,0,255);
  for(auto b:code)p.write(0x18,b,255);
  p.write(8,0,255);p.write(9,0,255);for(auto b:code)check(p.read()==b,"AVD port readback mismatch");
  p.write(5,0,255);check(p.write(1,1,255),"original program release missing");
  manta::VdMcuCpu cpu;std::set<uint16_t> visits;
  cpu.instruction=[&](uint16_t pc,uint8_t opcode,const std::string& s){
    check(pc<code.size() && opcode==code[pc],"AVD opcode differs from uploaded original");
    if(visits.insert(pc).second)std::cout<<std::hex<<std::setfill('0')<<std::setw(4)<<pc<<' '<<std::setw(2)<<unsigned(opcode)<<' '<<s<<'\n';
  };
  cpu.external=[](uint16_t a,bool w,uint8_t v){std::cout<<"AVD_XDATA "<<(w?"write":"read")<<" 0x"<<std::hex<<a<<" value 0x"<<unsigned(v)<<'\n';};
  cpu.specialRegister=[](uint8_t a,bool w,uint8_t v){std::cout<<"AVD_SFR "<<(w?"write":"read")<<" 0x"<<std::hex<<unsigned(a)<<" value 0x"<<unsigned(v)<<'\n';};
  cpu.resetProgram(p);std::string boundary;
  try{for(unsigned i=0;i<1000000;++i)cpu.step();}
  catch(const manta::VdHardwareBoundary& e){boundary=e.what();}
  check(!boundary.empty(),"AVD did not reach an observable hardware boundary within budget");
  std::cout<<"AVD_BOUNDARY "<<boundary<<'\n';
  std::cout<<"AVD_ORIGINAL_INSTRUCTIONS "<<std::dec<<cpu.totalInstructions()<<" PC 0x"<<std::hex<<cpu.pc()<<" SP 0x"<<unsigned(cpu.stack())<<'\n';
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
