// Isolated original-instruction fragments/functions, with synthetic samples.
// Not a TV boot, event producer, reset value or invocation of the TV publisher.
#include "cpu/engine.hpp"
#include "cpu/vd_mcu.hpp"
#include "peripherals/avd_mcu_bus.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <tuple>
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
  need(argc==2,"supply original decompressed main image; isolated synthetic timing fixtures only");
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(input),{});
  need(image.size()==3167072 && manta::sha256(image)=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236","original main image mismatch");
  std::vector<uint8_t> avd(image.begin()+0x80627a00-0x803b3980,image.begin()+0x8062ae00-0x803b3980);
  need(manta::sha256(avd)=="48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd","original AVD mismatch");
  unsigned copies=0,partial=0;uint64_t mcuInstructions=0;
  auto transfer=[&](uint8_t low,uint8_t high,uint8_t lowKnown,uint8_t highKnown){
    // A separate test program contains the unchanged sampling fragment at
    // reset and its unchanged original absolute helper. It is NOT the TV
    // image; no original image/reset vector or connected PC is altered.
    manta::VdMcuProgram program;program.write(0,1);program.write(1,0);program.write(5,0x51);program.write(8,0);program.write(9,0);
    for(unsigned i=0xaa9;i<0xabf;++i)program.write(0x18,avd[i]);
    program.write(8,0x5e);program.write(9,7);
    for(unsigned i=0x75e;i<0x78e;++i)program.write(0x18,avd[i]);
    manta::VdMcuCpu cpu;cpu.symbolicCodeTables=true;
    manta::AvdMcuBus bus;bus.symbolicTableConfiguration=true;
    std::map<uint32_t,manta::VdByte> registers;
    registers[*bus.sharedAddress(0x113c)]={low,lowKnown,0};registers[*bus.sharedAddress(0x113d)]={high,highKnown,0};
    std::vector<uint16_t> reads,writes;
    bus.sharedRead=[&](uint32_t a){return registers.at(a);};
    bus.sharedWrite=[&](uint32_t a,manta::VdByte v){registers[a]=v;};
    cpu.busRead=[&](uint16_t a){reads.push_back(a);return bus.read(a);};
    cpu.busWrite=[&](uint16_t a,manta::VdByte v){writes.push_back(a);return bus.write(a,v);};
    cpu.resetProgram(program);
    while(cpu.pc()!=0x16 && cpu.instructions()<100)cpu.step();
    need(cpu.pc()==0x16 && cpu.instructions()<100,"original sampling/helper fragment did not return");
    need(reads==std::vector<uint16_t>{0x113c,0x113d} && writes==std::vector<uint16_t>{0x11c7,0x11c8},"original sample/publish byte ordering differs");
    auto outLow=registers.at(0x1f206b8d),outHigh=registers.at(0x1f206b90);
    need(outLow.value==low && outLow.known==lowKnown && outLow.origin==0x113c,"low publication changed validity/origin");
    need(outHigh.value==(high&7) && outHigh.known==uint8_t(0xf8|(highKnown&7)) && outHigh.origin==0x113d,"high publication failed to mask actual source and retain invalid low bits");
    need(cpu.interruptEntries()==0 && !registers.contains(0x1f206bf8) && !registers.contains(0x1f206bb8),"data publication cannot produce an event or host command");
    mcuInstructions+=cpu.instructions();++copies;if(lowKnown!=255 || (highKnown&7)!=7)++partial;
  };
  for(unsigned sample=0;sample<2048;++sample)transfer(uint8_t(sample),uint8_t(0xf8|(sample>>8)),255,255);
  for(auto low:{0u,0xa5u,255u})for(auto high:{0u,7u,0xa5u,255u})
    for(auto lowMask:{0u,1u,0x80u,0xf0u,255u})for(auto highMask:{0u,1u,7u,0xf8u,255u})
      transfer(uint8_t(low),uint8_t(high),uint8_t(lowMask),uint8_t(highMask));

  manta::Engine cpu;cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());uint32_t stopAt=0;unsigned instructions=0;
  std::array<uint8_t,2> published{};std::vector<uint32_t> readOrder;
  std::pair mmio{&published,&readOrder};
  manta::check(uc_mmio_map(cpu.handle,0x1f206000,0x1000,
    +[](uc_engine*,uint64_t offset,unsigned width,void* data)->uint64_t{
      auto& state=*static_cast<decltype(mmio)*>(data);
      need(width==1 && (offset==0xb8d || offset==0xb90),"getter accessed an unrelated device");
      state.second->push_back(uint32_t(offset));return (*state.first)[offset==0xb8d?0:1];
    },&mmio,nullptr,nullptr));
  uc_hook hook;struct Execution{uint32_t* stop;unsigned* count;} execution{&stopAt,&instructions};
  manta::check(uc_hook_add(cpu.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* engine,uint64_t pc,uint32_t,void* data){auto& e=*static_cast<Execution*>(data);
      if(pc==*e.stop){uc_emu_stop(engine);return;}++*e.count;
    }),&execution,1,0));
  auto write=[&](uint32_t a,uint32_t v,unsigned n=4){uint8_t b[4]{};for(unsigned i=0;i<n;++i)b[i]=uint8_t(v>>(8*i));cpu.write(a,b,n);};
  auto read=[&](uint32_t a,unsigned n=4){uint8_t b[4]{};cpu.read(a,b,n);uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(b[i])<<(8*i);return v;};
  auto reset=[&]{instructions=0;readOrder.clear();for(int r=UC_MIPS_REG_0;r<=UC_MIPS_REG_31;++r)cpu.reg(r,0);
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);};
  write(0x006ad974,0xbf200000);write(0x006ad978,1);
  unsigned getterCases=0;
  for(unsigned high=0;high<256;++high)for(auto low:{0u,255u}){
    reset();published={uint8_t(low),uint8_t(high)};stopAt=0x803b0000;
    manta::check(cpu.run(0x805030e0,100));
    need(cpu.reg(UC_MIPS_REG_PC)==stopAt && cpu.reg(UC_MIPS_REG_2)==((high<<8)|low),"original MIPS getter is a 16-bit byte assembly, not an implicit 11-bit mask");
    need(readOrder==std::vector<uint32_t>{0xb90,0xb8d},"MIPS getter must read high before low");++getterCases;
  }
  unsigned stabilityCases=0;
  for(auto previous:{0u,909u,1135u,2047u})for(auto older:{0u,909u,1135u,2047u})
    for(auto sample:{0u,909u,1135u,2047u})for(auto count:{0u,3u,4u,5u,255u}){
      reset();write(0x006d83cc,previous,2);write(0x006d83ce,older,2);write(0x006d83d0,0x1234,2);
      write(0x006d83d4,count,1);write(0x006ad960,0x5678,2);cpu.reg(UC_MIPS_REG_7,sample);stopAt=0x80501758;
      manta::check(cpu.run(0x80501728,100));
      bool equal=previous==older && older==sample;
      auto expected=equal?(count<4?count+1:count):0;
      need(cpu.reg(UC_MIPS_REG_PC)==stopAt && instructions<40,"original stability paths must join without a sample wait loop");
      need(read(0x006d83cc,2)==sample && read(0x006d83ce,2)==previous && read(0x006d83d0,2)==older,"original HTotal history shift differs");
      need(read(0x006d83d4,1)==expected && read(0x006ad960,2)==(equal && count>=4?sample:0x5678),"original stable-sample counter threshold differs");++stabilityCases;
    }
  std::vector<uint8_t> after(0x80613000-0x803b3980);cpu.read(0x003b3980,after.data(),after.size());
  need(std::equal(after.begin(),after.end(),image.begin()),"original instruction image changed");
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"ISOLATED original fragments/functions with synthetic samples, NOT TV boot/publisher/event production\","
    <<"\"sample_copy_cases\":"<<copies<<",\"invalid_sample_cases\":"<<partial<<",\"fragment_instructions\":"<<mcuInstructions
    <<",\"MIPS_getter_cases\":"<<getterCases<<",\"MIPS_stability_cases\":"<<stabilityCases
    <<",\"TV_sample_supplied\":false,\"TV_publisher_executed\":false,\"pixels_generated\":false}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
