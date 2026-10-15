// Isolated original MIPS AVD register-service routine. All request fixtures
// below are synthetic, never connected to television boot or labeled MCU output.
#include "cpu/engine.hpp"
#include "board/preserved_bits.hpp"
#include "storage/sha256.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Request {
  uint8_t command=0,known=255,mask=0,index=0,data=0,target=0;
  uint32_t address=0,current=0;
  unsigned writes=0,targetWrites=0,instructions=0;
  manta::PreservedBits provenance;
  std::string boundary;
};
int main(int argc,char** argv){try{
  need(argc==2,"supply hash-verified original decompressed main image; isolated request fixtures only");
  std::ifstream input(argv[1],std::ios::binary);
  std::vector<uint8_t> image(std::istreambuf_iterator<char>(input),{});
  need(image.size()==3167072 && manta::sha256(image)==
    "149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236","original image mismatch");
  manta::Engine cpu;cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());Request fixture;
  auto read=+[](uc_engine* engine,uint64_t offset,unsigned width,void* context)->uint64_t{
    auto& f=*static_cast<Request*>(context);auto address=0x1f206000u+uint32_t(offset);
    if(width!=1){f.boundary="unexpected width";uc_emu_stop(engine);return 0;}
    if(offset==0xbb8){
      f.provenance.seed((f.current>>16)&31,1,f.known,[](unsigned){return 0x1f206bb8u;});
      return f.command;
    }
    if(offset==0xbb9)return f.mask;
    if(offset==0xbbc)return f.index;
    if(offset==0xbbd)return f.data;
    if(address==f.address)return f.target;
    f.boundary="unexpected register read";uc_emu_stop(engine);return 0;
  };
  auto write=+[](uc_engine* engine,uint64_t offset,unsigned width,uint64_t value,void* context){
    auto& f=*static_cast<Request*>(context);++f.writes;
    if(width==1 && offset==0xbb8){f.command=uint8_t(value);return;}
    if(width==1 && offset==0xbbd){f.data=uint8_t(value);return;}
    if(width==1 && 0x1f206000u+offset==f.address){f.target=uint8_t(value);++f.targetWrites;return;}
    f.boundary="unexpected register write";uc_emu_stop(engine);
  };
  manta::check(uc_mmio_map(cpu.handle,0x1f206000,0x1000,read,&fixture,write,&fixture));
  uc_hook hook;manta::check(uc_hook_add(cpu.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* engine,uint64_t pc,uint32_t,void* context){
      auto& f=*static_cast<Request*>(context);
      if(pc==0x803b0000){uc_emu_stop(engine);return;}
      ++f.instructions;manta::check(uc_mem_read(engine,pc&0x1fffffff,&f.current,4));
      auto why=f.provenance.observe(f.current,[&](unsigned r){uint32_t value=0;
        manta::check(uc_reg_read(engine,UC_MIPS_REG_0+int(r),&value));return value;
      },[](uint32_t a){return a&0x1fffffff;},[](uint32_t,unsigned,uint32_t){});
      if(!why.empty()){f.boundary=why;uc_emu_stop(engine);}
    }),&fixture,1,0));
  const uint32_t base=0xbf200000,enabled=1;cpu.write(0x006ad974,&base,4);cpu.write(0x006ad978,&enabled,4);
  auto run=[&]{
    for(int r=UC_MIPS_REG_0;r<=UC_MIPS_REG_31;++r)cpu.reg(r,0);
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);
    manta::check(cpu.run(0x8050328c,128));
  };
  unsigned cases=0,unknownStops=0,maximumInstructions=0;
  for(unsigned mode:{0u,4u,8u,12u})for(unsigned bank=0;bank<4;++bank)
    for(unsigned mask:{0u,1u,0x5au,255u})for(unsigned data:{0u,255u})
      for(unsigned index:{0u,0x44u,0x7fu})for(unsigned target:{0u,0xa5u,255u}){
        fixture={};auto& f=fixture;
        f.command=uint8_t(0xa0|mode|bank);f.mask=uint8_t(mask);f.index=uint8_t(index);
        f.data=uint8_t(data);f.target=uint8_t(target);
        auto logical=(bank?0x3500u:0x3600u)+index;
        f.address=0x1f200000u+(logical<<1)-(logical&1);run();
        need(f.boundary.empty() && cpu.reg(UC_MIPS_REG_PC)==0x803b0000,"original request handler must return");
        if(mode==8)need(f.target==((target&~mask)|(data&mask)) && f.targetWrites==1 &&
          f.command==(0xa0|bank) && f.writes==2,"original write request or command clear differs");
        else if(mode==4)need(f.data==(target&mask) && f.targetWrites==0 &&
          f.command==(0xac|bank) && f.writes==2,"original read request or returned-data marker differs");
        else need(f.target==target && f.data==data && f.writes==0,"inactive command must cause no transaction");
        maximumInstructions=std::max(maximumInstructions,f.instructions);++cases;
      }
  for(unsigned mode:{0u,4u,8u,12u}){
    fixture={};fixture.command=uint8_t(mode|0xa1);fixture.known=0;run();
    need(fixture.boundary.find("unknown preserved register bit")!=std::string::npos &&
      cpu.reg(UC_MIPS_REG_PC)==0x805032b0 && fixture.writes==0,
      "unknown MCU request cannot be substituted with a convenient idle command");++unknownStops;
  }
  for(unsigned mode:{0u,4u,8u,12u}){
    fixture={};fixture.command=uint8_t(mode|0xa1);fixture.known=12;run();
    if(mode==0 || mode==12)need(fixture.boundary.empty() && cpu.reg(UC_MIPS_REG_PC)==0x803b0000 &&
      fixture.writes==0,"known inactive command bits must not require unused bank/high fields");
    else need(fixture.boundary.find("unknown preserved register bit")!=std::string::npos &&
      cpu.reg(UC_MIPS_REG_PC)==(mode==8?0x805032dcu:0x80503344u) && fixture.writes==0,
      "pending request cannot choose an unknown register bank");
  }
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"isolated synthetic requests executed by original MIPS; no AVD reply or TV boot input\","
    <<"\"entry\":\"0x8050328c\",\"request_address\":\"0xbf206bb8\",\"request_cases\":"<<cases
    <<",\"unknown_request_stops\":"<<unknownStops<<",\"maximum_original_instructions\":"<<maximumInstructions
    <<",\"partial_command_field_cases\":4"
    <<",\"original_sha256\":\""<<manta::sha256(image)<<"\",\"TV_request_supplied\":false}\n";
  return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
