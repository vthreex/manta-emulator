// Isolated function tests of hash-verified original MIPS code. Synthetic RAM
// fixtures never enter the television machine; no UI event is injected there.
#include "cpu/engine.hpp"
#include "board/zui_flow.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
static void need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct Fixture {uint32_t stopAt=0;unsigned instructions=0;std::set<uint32_t> visits;};
int main(int argc,char** argv){try{
  need(argc==2,"supply original decompressed MIPS image; isolated synthetic-RAM function tests only");
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(f),{});
  need(image.size()==3167072 && manta::sha256(image)=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236",
    "original MIPS image size/hash mismatch");
  manta::Engine cpu;cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());Fixture fixture;
  uc_hook hook;manta::check(uc_hook_add(cpu.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* engine,uint64_t address,uint32_t,void* data){
      auto& f=*static_cast<Fixture*>(data);
      if(address==0x803b0000 || address==f.stopAt){uc_emu_stop(engine);return;}
      need(address>=0x803b3980 && address<0x80613000,"fixture escaped original instruction range");
      ++f.instructions;f.visits.insert(uint32_t(address));
    }),&fixture,1,0));
  auto write=[&](uint32_t address,uint32_t value,unsigned width=4){uint8_t b[4]{};
    for(unsigned i=0;i<width;++i)b[i]=uint8_t(value>>(8*i));cpu.write(address,b,width);};
  auto read=[&](uint32_t address,unsigned width=4){uint8_t b[4]{};cpu.read(address,b,width);uint32_t value=0;
    for(unsigned i=0;i<width;++i)value|=uint32_t(b[i])<<(8*i);return value;};
  auto run=[&](uint32_t entry,uint32_t argument=0,uint32_t stopAt=0){
    fixture={};fixture.stopAt=stopAt;
    for(int r=UC_MIPS_REG_0;r<=UC_MIPS_REG_31;++r)cpu.reg(r,0);
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);cpu.reg(UC_MIPS_REG_4,argument);
    manta::check(cpu.run(entry,50000));need(cpu.reg(UC_MIPS_REG_PC)==(stopAt?stopAt:0x803b0000),"original function failed to reach its own return or explicit fixture observation boundary");
  };
  using Z=manta::ZuiFlow;unsigned cases=0,queueInstructions=0;
  write(0x006e561c,0x12345678);
  auto initialize=[&]{run(0x8049f160);need(cpu.reg(UC_MIPS_REG_2)==1,"original queue initializer must return true");
    need(read(Z::headAddress)==0 && read(Z::tailAddress)==0,"original queue indices must clear");
    for(unsigned i=0;i<40;++i)need(read(Z::queueAddress+12*i,2)==0xffff,"original initializer must invalidate all 40 handles");
    for(unsigned i=0;i<8;++i)need(read(0x006d51b8+16*i,2)==0xffff,"original initializer must invalidate all eight timer handles");
    need(read(0x006d5238)==0x12345678,"original timing getter must read the supplied isolated RAM fixture");
    queueInstructions+=fixture.instructions;
  };
  initialize();
  for(unsigned i=0;i<40;++i){
    initialize();write(Z::tailAddress,i);write(Z::headAddress,(i+1)%40);
    auto slot=Z::queueAddress+12*i;write(slot,0x55aa0000|(i+1));write(slot+4,0x100+i);write(slot+8,0x98760000+i);
    std::array<uint8_t,12> expected{},actual{};cpu.read(slot,expected.data(),12);
    run(0x8049f040,0x80700080);queueInstructions+=fixture.instructions;
    cpu.read(0x00700080,actual.data(),12);
    need(cpu.reg(UC_MIPS_REG_2)==1 && expected==actual,"original GetMessage must copy the entire 12-byte message");
    need(read(slot,2)==0xffff && read(slot+2,2)==0x55aa && read(Z::tailAddress)==(i+1)%40,
      "original dequeue must invalidate only HWND and wrap its actual 40-slot index");
    run(0x8049f040,0x80700080);queueInstructions+=fixture.instructions;
    need(cpu.reg(UC_MIPS_REG_2)==0 && read(Z::tailAddress)==(i+1)%40,"original empty dequeue must return false without advancing");
    cpu.read(0x00700080,actual.data(),12);need(expected==actual,"empty dequeue cannot invent a message");++cases;
  }
  for(auto state:{0u,1u,6u,0xffffffffu}){
    write(Z::stateAddress,state);write(Z::previousAddress,0xdeadbeef);write(Z::activeAddress,0);
    run(0x804e0dc4);
    need(cpu.reg(UC_MIPS_REG_2)==0 && read(Z::previousAddress)==state,"original standby/default dispatcher must return none and retain previous state");
    need(!fixture.visits.contains(0x8049f040) && !fixture.visits.contains(0x804a0640),"standby cannot consume or dispatch a message");++cases;
  }
  unsigned runningCases=0;
  for(auto state:{2u,3u,4u,5u}){
    initialize();write(Z::stateAddress,state);write(Z::previousAddress,1);
    auto target=state==2?0x8049f180u:state==3?0x804ee128u:state==4?0x804ee10cu:0x804c3574u;
    run(0x804e0dc4,0,target);
    if(state==2 || state==3)need(fixture.visits.contains(0x8049f040),"running/transition dispatcher must poll the genuine queue before its next worker");
    else need(!fixture.visits.contains(0x8049f040),"end-effect and shutdown select their distinct original handlers");
    ++runningCases;
  }
  unsigned resourceCases=0,resourceWindows=0;uint64_t resourceInstructions=0;
  for(unsigned id=1;id<30;++id){
    auto descriptor=0x00686030+48*id;
    auto count=read(descriptor,2),windows=read(descriptor+4);
    run(0x8049ea20,id);need(cpu.reg(UC_MIPS_REG_2)==count,"original resource count getter differs from original descriptor");
    run(0x8049ea58,id);need(cpu.reg(UC_MIPS_REG_2)==windows,"original window table getter differs from original descriptor");
    run(0x8049eabc,id);auto rectangles=cpu.reg(UC_MIPS_REG_2);
    need(rectangles==read(0x00695780+4*id),"original rectangle table getter differs from original table");
    write(Z::windowCountAddress,count,2);write(Z::windowListAddress,windows);write(0x006b918c,rectangles);
    run(0x8049ee1c);resourceInstructions+=fixture.instructions;
    need(cpu.reg(UC_MIPS_REG_2)==1,"original window-state initializer must return success");
    auto wordCount=(count+32)>>5,visible=read(0x006d4fac),disabled=read(0x006d4fb0);
    need(visible==0x806d523c && disabled==visible+wordCount*4,"original visibility/disable bitmap layout differs");
    for(unsigned hwnd=0;hwnd<count;++hwnd){
      auto flags=read((windows&0x1fffffff)+20*hwnd+8,1);
      need(((read((visible&0x1fffffff)+4*(hwnd>>5))>>(hwnd&31))&1)==(flags&1),"original visibility must come from actual window flag bit0");
      need(((read((disabled&0x1fffffff)+4*(hwnd>>5))>>(hwnd&31))&1)==bool(flags&4),"original disabled state must come from actual window flag bit2");
    }
    if(rectangles)for(unsigned i=0;i<1100;++i)for(unsigned lane=0;lane<2;++lane)
      need(read(0x006d534c+8*i+4*lane)==read((rectangles&0x1fffffff)+12*i+4+4*lane),
        "original unaligned LWL/LWR rectangle copies must use the original resource bytes");
    resourceWindows+=count;++resourceCases;
  }
  for(auto id:{0u,30u,0xffffffffu})for(auto getter:{0x8049ea20u,0x8049ea58u,0x8049eabcu}){
    run(getter,id);need(cpu.reg(UC_MIPS_REG_2)==0,"original invalid OSD resource ID must return no resource");
  }
  std::vector<uint8_t> after(image.size());cpu.read(0x003b3980,after.data(),after.size());need(after==image,"original instruction/resource image changed");
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"ISOLATED original-function execution with synthetic RAM, not television boot or events\","
    <<"\"queue_cases\":"<<cases-4<<",\"standby_default_cases\":4,\"active_dispatch_cases\":"<<runningCases
    <<",\"queue_original_instructions\":"<<queueInstructions<<",\"window_resource_cases\":"<<resourceCases
    <<",\"original_window_records\":"<<resourceWindows<<",\"window_state_instructions\":"<<resourceInstructions
    <<",\"TV_dispatcher_invoked\":false,\"TV_events_injected\":false,\"pixels_generated\":false,"
    <<"\"program_sha256\":\""<<manta::sha256(image)<<"\"}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
