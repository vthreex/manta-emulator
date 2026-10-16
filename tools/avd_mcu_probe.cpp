// Executes the original AVD upload independently. No guessed XDATA/SFRs,
// DSP readiness or television boot progress are supplied by this diagnostic.
#include "cpu/vd_mcu.hpp"
#include "peripherals/avd_mcu_bus.hpp"
#include "storage/sha256.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <set>
static void check(bool c,const char* s){if(!c)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
  bool symbolic=false;
  if(argc>2 && std::string(argv[argc-1])=="--symbolic-code-tables"){symbolic=true;--argc;}
  if(argc==5 && std::string(argv[1])=="--disassemble-only"){
    std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t> code(std::istreambuf_iterator<char>(f),{});
    check(!code.empty() && code.size()<=65536,"candidate 8051 image must contain 1..65536 bytes");
    auto pc=std::stoul(argv[3],nullptr,0),end=std::stoul(argv[4],nullptr,0);
    check(pc<end && end<=code.size(),"disassembly range outside candidate program");
    auto storage=code;storage.resize(65536);em8051 dis{};
    dis.mCodeMem=storage.data();dis.mCodeMemMaxIdx=65535;reset(&dis,false);
    std::cout<<"STATIC_8051_CANDIDATE; no auxiliary instructions executed; image SHA256 "<<manta::sha256(code)<<'\n';
    for(;pc<end;){char text[128];auto n=decode(&dis,uint16_t(pc),text);
      check(n && pc+n<=end,"truncated candidate instruction at requested boundary");
      std::cout<<std::hex<<pc<<' '<<text<<'\n';pc+=n;}
    return 0;
  }
  bool synthetic=argc==6 && std::string(argv[2])=="--deferred-shared-prefix" &&
    std::string(argv[3])=="--synthetic-startup-selector";
  bool syntheticFlag=argc==5 && std::string(argv[2])=="--deferred-shared-prefix" &&
    std::string(argv[3])=="--synthetic-11db-bit7";
  bool shared=(argc==3 && std::string(argv[2])=="--deferred-shared-prefix") || synthetic || syntheticFlag;
  check(argc==2 || argc==4 || shared,"supply original AVD payload [disassembly start end | --deferred-shared-prefix [--synthetic-startup-selector VALUE MASK]]; synthetic fixture is isolated, never TV input");
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
  cpu.symbolicCodeTables=symbolic;
  if(symbolic)std::cout<<"SYMBOLIC_CODE_TABLES: all possible uploaded ROM data bytes merged; no hardware input supplied\n";
  manta::AvdMcuBus bus;std::map<uint32_t,manta::VdByte> registers;
  bus.symbolicTableConfiguration=symbolic;
  if(syntheticFlag){
    size_t used=0;auto value=std::stoul(argv[4],&used,0);
    check(symbolic && used==std::string(argv[4]).size() && value<=1,"synthetic 11DB bit must be 0 or 1 and requires symbolic CODE tracking");
    registers[0x1f206bb5]={uint8_t(value<<7),0x80,0};
    std::cout<<"SYNTHETIC_ISOLATED_11DB_BIT7="<<value<<"; unknown meaning/reset, not connected to television boot\n";
  }
  if(synthetic){
    auto byte=[](const char* text){size_t used=0;auto v=std::stoul(text,&used,0);
      check(used==std::string(text).size() && v<=255,"synthetic selector/mask must be bytes");return uint8_t(v);};
    auto value=byte(argv[4]),known=byte(argv[5]);registers[0x1f206b38]={value,known,0};
    std::cout<<"SYNTHETIC_ISOLATED_STARTUP_SELECTOR value="<<unsigned(value)<<" known="<<unsigned(known)
      <<"; original reset/program unchanged; not measured hardware, not connected to television boot\n";
  }
  if(shared){
    std::cout<<"DIAGNOSTIC original startup writes retained; no asynchronous reset/clock disturbance during bounded prefix is an EXPLORATORY assumption; "
      <<((synthetic || syntheticFlag)?"only the labeled synthetic fixture is supplied; all other inputs remain invalid\n":
        "no input/readback/ready value supplied\n");
    bus.configureDeferredStartup();
    bus.sharedRead=[&](uint32_t a){return registers[a];};
    bus.sharedWrite=[&](uint32_t a,manta::VdByte v){registers[a]=v;};
    bus.event=[](const auto& k,auto a,auto,auto v,const auto& d){std::cout<<k<<" address 0x"<<std::hex<<a<<" value 0x"<<v<<" "<<d<<'\n';};
    cpu.busRead=[&](uint16_t a){return bus.read(a);};cpu.busWrite=[&](uint16_t a,manta::VdByte v){return bus.write(a,v);};
    cpu.unsupportedSfrWrite=[&](uint8_t a,manta::VdByte v){return bus.deferSfr(a,v);};
    cpu.standardInterrupts=true;
  }
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
  if(shared)std::cout<<"AVD_SHARED_PREFIX instructions="<<std::dec<<cpu.totalInstructions()<<" configurations="<<bus.configurationWrites
    <<" shared_reads="<<bus.sharedReads<<" shared_writes="<<bus.sharedWrites<<" ram_writes="<<bus.ramWrites
    <<" interrupts="<<cpu.interruptEntries()<<" state="<<manta::sha256(bus.stateBytes())<<'\n';
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
