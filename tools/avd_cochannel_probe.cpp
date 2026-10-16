// Isolated unchanged original fragments, not TV startup or a detector stimulus.
#include "cpu/engine.hpp"
#include "cpu/vd_mcu.hpp"
#include "peripherals/avd_mcu_bus.hpp"
#include "storage/sha256.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
  need(argc==2,"supply original main image; isolated firmware-output tests only");
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(f),{});
  need(image.size()==3167072 && manta::sha256(image)=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236","original main image mismatch");
  std::vector<uint8_t> avd(image.begin()+0x80627a00-0x803b3980,image.begin()+0x8062ae00-0x803b3980);
  need(manta::sha256(avd)=="48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd","original AVD mismatch");
  manta::Engine mips;mips.map(0x003b0000,0x350000);mips.map(0x00700000,0x10000);
  mips.write(0x003b3980,image.data(),image.size());
  auto write=[&](uint32_t a,uint32_t value){uint8_t bytes[4];for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(value>>(8*i));mips.write(a,bytes,4);};
  write(0x006ad974,0xbf200000);write(0x006ad978,1);
  struct Wire {manta::VdByte byte;unsigned reads=0;} wire;
  manta::check(uc_mmio_map(mips.handle,0x1f206000,0x1000,
    +[](uc_engine*,uint64_t offset,unsigned width,void* data)->uint64_t{
      auto& w=*static_cast<Wire*>(data);need(offset==0xb94 && width==1 && w.byte.known==255,
        "isolated getter may read only a genuinely published fully-known fixture byte");
      ++w.reads;return w.byte.value;
    },&wire,nullptr,nullptr));
  uc_hook hook;manta::check(uc_hook_add(mips.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* e,uint64_t pc,uint32_t,void*){if(pc==0x803b0000)uc_emu_stop(e);}),nullptr,1,0));
  unsigned getterCases=0;
  auto getter=[&](unsigned expected){
    for(int r=UC_MIPS_REG_0;r<=UC_MIPS_REG_31;++r)mips.reg(r,0);
    mips.reg(UC_MIPS_REG_SP,0x8070ff00);mips.reg(UC_MIPS_REG_RA,0x803b0000);
    manta::check(mips.run(0x80503080,100));
    need(mips.reg(UC_MIPS_REG_PC)==0x803b0000 && mips.reg(UC_MIPS_REG_2)==expected,
      "original MIPS co-channel getter disagrees with the actual MCU-published flag");
    ++getterCases;
  };
  unsigned cases=0,unknownCases=0;uint64_t instructions=0;
  for(int latch:{-2,-1,0,1}){bool set=latch==1;
    wire={};need(wire.byte.known==0,"status must start INVALID until original MCU instructions write it");
    manta::VdMcuProgram program;program.write(0,1);program.write(1,0);program.write(5,0x51);program.write(8,0);program.write(9,0);
    unsigned end=0;
    auto append=[&](unsigned first,unsigned last){for(unsigned i=first;i<last;++i){program.write(0x18,avd[i]);++end;}};
    append(0x241,0x247); // original startup's CLR/MOV/ACALL publishes zero
    if(latch>=0)append(set?0x2bb1:0x2bbc,set?0x2bb3:0x2bbe); // isolated original detector-latch fixture
    else{
      // Use unchanged original MOVX/MOV instructions to taint IRAM2C with an
      // INVALID fixture byte. Do not rely on the engine's standard-reset RAM.
      append(0xaa9,0xaad);append(0x275d,0x275f);
    }
    append(0x1130,0x1136); // original shared-byte read into shadow50
    append(0x1183,0x1195); // original bit3 publication decision and helper call
    for(auto range:{std::pair{0x2aau,0x2b7u},std::pair{0x75eu,0x768u}}){
      program.write(8,uint8_t(range.first));program.write(9,uint8_t(range.first>>8));
      for(unsigned i=range.first;i<range.second;++i)program.write(0x18,avd[i]);
    }
    manta::AvdMcuBus bus;unsigned stores=0;
    bus.sharedRead=[&](uint32_t a){
      if(latch<0 && a==0x1f206a78)return manta::VdByte{0xa5,0,0};
      need(a==0x1f206b94,"fragment read an unrelated register");return wire.byte;
    };
    bus.sharedWrite=[&](uint32_t a,manta::VdByte value){
      need(a==0x1f206b94 && value.known==255 && value.value==(stores?(set?8u:0u):0u),
        "original startup/flag publisher wrote unexpected state");
      wire.byte=value;getter(stores && set?1:0);++stores;
    };
    manta::VdMcuCpu cpu;cpu.symbolicCodeTables=latch!=-2;
    cpu.busRead=[&](uint16_t a){return bus.read(a);};
    cpu.busWrite=[&](uint16_t a,manta::VdByte value){return bus.write(a,value);};cpu.resetProgram(program);
    bool rejected=false;std::string boundary;
    try{while(cpu.pc()!=end && cpu.instructions()<150)cpu.step();}
    catch(const manta::VdHardwareBoundary& e){rejected=true;boundary=e.what();}
    if(latch<0){
      if(!(rejected && cpu.pc()==(latch==-2?10:18) && stores==1 && wire.reads==1))
        throw std::runtime_error("unknown latch fixture: PC="+std::to_string(cpu.pc())+" stores="+std::to_string(stores)+
          " getter reads="+std::to_string(wire.reads)+" boundary="+boundary);
      ++unknownCases;
    }else{
      need(!rejected && cpu.pc()==end && stores==2 && wire.reads==2,
        "original co-channel output fragments failed to complete both publications");++cases;
    }
    need(cpu.interruptEntries()==0,"co-channel fixture cannot generate interrupts");instructions+=cpu.instructions();
  }
  // The isolated getter's ordinary linked RAM context is the only explicit
  // data fixture inside this loaded image. All code/resources/other data must
  // remain byte-identical, and the original input file is never written.
  auto expected=image;std::array<uint8_t,8> context{0,0,0x20,0xbf,1,0,0,0};
  std::copy(context.begin(),context.end(),expected.begin()+0x806ad974-0x803b3980);
  std::vector<uint8_t> after(image.size());mips.read(0x003b3980,after.data(),after.size());
  need(after==expected,"original code/resources/data changed outside the explicit isolated RAM context fixture");
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"ISOLATED concatenated unchanged original fragments with detector-latch fixtures; NOT connected startup or physical signal detection\","
    <<"\"latch_cases\":"<<cases<<",\"unknown_latch_cases\":"<<unknownCases<<",\"original_MIPS_getters\":"<<getterCases<<",\"original_AVD_fragment_instructions\":"<<instructions
    <<",\"initial_status_invalid\":true,\"TV_detector_status_supplied\":false,\"TV_AVD_advanced\":false,\"pixels_generated\":false}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
