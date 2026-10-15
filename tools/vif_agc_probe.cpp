#include "cpu/engine.hpp"
#include "storage/sha256.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Fixture {
  unsigned mean=0,pga=0,vga=0,reads=0,writes=0,instructions=0;
  std::string failure;
};
int main(int argc,char** argv){try{
  need(argc==2,"supply the original decompressed MIPS image; isolated synthetic VIF measurements only");
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> image(std::istreambuf_iterator<char>(file),{});
  need(image.size()==3167072 && manta::sha256(image)==
    "149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236","original MIPS size/hash mismatch");
  manta::Engine cpu;cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());
  Fixture fixture;
  auto read=+[](uc_engine* engine,uint64_t offset,unsigned width,void* context)->uint64_t{
    auto& state=*static_cast<Fixture*>(context);++state.reads;
    if(offset==0x644 && width==1)return state.mean;
    if(offset==0x65d && width==1)return state.pga;
    if(offset==0x650 && width==2)return state.vga;
    if(offset==0x440 && width==1)return 0xff;
    state.failure="unexpected register read in isolated fixture";uc_emu_stop(engine);return 0;
  };
  auto write=+[](uc_engine* engine,uint64_t offset,unsigned width,uint64_t value,void* context){
    auto& state=*static_cast<Fixture*>(context);++state.writes;
    if(width==1 && ((offset==0x604 && value==4) || (offset==0x605 && value==0) ||
        (offset==0x440 && value==0xbf)))return;
    state.failure="unexpected register write in isolated fixture";uc_emu_stop(engine);
  };
  manta::check(uc_mmio_map(cpu.handle,0x1f224000,0x1000,read,&fixture,write,&fixture));
  uc_hook observer;manta::check(uc_hook_add(cpu.handle,&observer,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* engine,uint64_t pc,uint32_t,void* context){
      auto& state=*static_cast<Fixture*>(context);
      if(pc==0x803b0000){uc_emu_stop(engine);return;}++state.instructions;
    }),&fixture,1,0));
  auto word=[&](uint32_t address,uint32_t value){cpu.write(address,&value,sizeof(value));};
  auto byte=[&](uint32_t address,uint8_t value){cpu.write(address,&value,sizeof(value));};
  unsigned cases=0,acquired=0,held=0,maxInstructions=0;
  for(unsigned initialState:{0u,1u})for(unsigned mean=0;mean<256;++mean)
    for(unsigned pga:{0u,31u})for(unsigned vga:{0x1000u,0x1001u}){
      fixture={mean,pga,vga,0,0,0,{}};
      word(0x006ad97c,0xbf200000);word(0x006ad980,1);
      word(0x006fe900,0x10000000);
      byte(0x006b9218,uint8_t(initialState));byte(0x006b921a,0);byte(0x006b9221,0);
      for(int reg=UC_MIPS_REG_0;reg<=UC_MIPS_REG_31;++reg)cpu.reg(reg,0);
      cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);
      manta::check(cpu.run(0x8050962c,256));
      need(fixture.failure.empty(),fixture.failure.c_str());
      need(cpu.reg(UC_MIPS_REG_PC)==0x803b0000 && cpu.reg(UC_MIPS_REG_SP)==0x8070ff00,
        "original VIF handler must return using its saved RA and stack");
      bool advance=(mean>=20 && mean<140) || pga==31 || vga==0x1000;
      uint8_t actualState=0;cpu.read(0x006b9218,&actualState,1);
      need(actualState==(advance?2:initialState),"original acquisition state differs from decoded predicate");
      need(fixture.reads==(advance?4u:3u) && fixture.writes==(advance?3u:0u),
        "original return/acquisition path has unexpected device effects");
      ++cases;if(advance)++acquired;else ++held;
      maxInstructions=std::max(maxInstructions,fixture.instructions);
    }
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"isolated original VIF handler with synthetic measurements; no TV boot or hardware state established\","
    <<"\"original_entry\":\"0x8050962c\",\"original_sha256\":\""<<manta::sha256(image)<<"\","
    <<"\"measurement_cases\":"<<cases<<",\"guest_acquisition_advances\":"<<acquired
    <<",\"guest_nonacquisition_returns\":"<<held<<",\"maximum_original_instructions_per_call\":"<<maxInstructions
    <<",\"tv_measurements_supplied\":false}\n";
  return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
