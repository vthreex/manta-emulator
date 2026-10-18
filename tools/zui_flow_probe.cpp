// Isolated function tests of hash-verified original MIPS code. Synthetic RAM
// fixtures never enter the television machine; no UI event is injected there.
#include "cpu/engine.hpp"
#include "board/zui_flow.hpp"
#include "peripherals/riu.hpp"
#include "board/preserved_bits.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <memory>
static void need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
struct Fixture {uint32_t stopAt=0,paintWindow=0xffff;unsigned instructions=0;std::set<uint32_t> visits,alternatives;
  manta::PreservedBits* preserved=nullptr;manta::Riu* riu=nullptr;};
int main(int argc,char** argv){try{
  need(argc==2 || argc==4,"supply original MIPS image [decoded resource816 original runtime prefix]; isolated tests only");
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(f),{});
  need(image.size()==3167072 && manta::sha256(image)=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236",
    "original MIPS image size/hash mismatch");
  manta::Engine cpu;cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
  cpu.write(0x003b3980,image.data(),image.size());Fixture fixture;
  uc_hook hook;manta::check(uc_hook_add(cpu.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* engine,uint64_t address,uint32_t,void* data){
      auto& f=*static_cast<Fixture*>(data);
      if(address==0x803b0000 || address==f.stopAt || f.alternatives.contains(uint32_t(address))){uc_emu_stop(engine);return;}
      need(address>=0x803b3980 && address<0x80613000,"fixture escaped original instruction range");
      if(f.preserved){uint32_t word=0;manta::check(uc_mem_read(engine,address&0x1fffffff,&word,4));
        auto why=f.preserved->observe(word,[&](unsigned reg){uint32_t value=0;uc_reg_read(engine,UC_MIPS_REG_0+int(reg),&value);return value;},
          [&](uint32_t target){return f.riu->preservedStoreKey(target);},
          [&](uint32_t target,unsigned width,uint32_t unknown){f.riu->preparePreservedWrite(target,width,unknown);});
        need(why.empty(),why.c_str());
      }
      if(address==0x804a071c){uint32_t message=0,window=0;
        uc_reg_read(engine,UC_MIPS_REG_5,&message);uc_reg_read(engine,UC_MIPS_REG_4,&window);
        if(message==0x1e)f.paintWindow=window;
      }
      ++f.instructions;f.visits.insert(uint32_t(address));
    }),&fixture,1,0));
  auto write=[&](uint32_t address,uint32_t value,unsigned width=4){uint8_t b[4]{};
    for(unsigned i=0;i<width;++i)b[i]=uint8_t(value>>(8*i));cpu.write(address,b,width);};
  auto read=[&](uint32_t address,unsigned width=4){uint8_t b[4]{};cpu.read(address,b,width);uint32_t value=0;
    for(unsigned i=0;i<width;++i)value|=uint32_t(b[i])<<(8*i);return value;};
  auto run=[&](uint32_t entry,uint32_t argument=0,uint32_t stopAt=0,std::set<uint32_t> alternatives={},uint32_t argument1=0,uint32_t argument2=0,uint64_t budget=50000){
    fixture={};fixture.stopAt=stopAt;fixture.alternatives=alternatives;
    for(int r=UC_MIPS_REG_0;r<=UC_MIPS_REG_31;++r)cpu.reg(r,0);
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);cpu.reg(UC_MIPS_REG_4,argument);
    cpu.reg(UC_MIPS_REG_5,argument1);cpu.reg(UC_MIPS_REG_6,argument2);
    manta::check(cpu.run(entry,budget));auto pc=cpu.reg(UC_MIPS_REG_PC);
    need(pc==(stopAt?stopAt:0x803b0000) || alternatives.contains(pc),"original function failed to reach its own return or explicit fixture observation boundary");
  };
  struct GeFixture {std::unique_ptr<manta::Riu> riu;Fixture* execution;
    std::vector<std::pair<uint32_t,uint16_t>> writes;unsigned reads=0;
    manta::PreservedBits preserved;bool symbolicControl=false,paintParameters=false;std::string boundary;} ge{nullptr,&fixture,{},0,{},false,false,{}};
  manta::check(uc_mmio_map(cpu.handle,0x1f205000,0x1000,
    +[](uc_engine* engine,uint64_t offset,unsigned width,void* data)->uint64_t{
      auto& g=*static_cast<GeFixture*>(data);uint32_t pc=0;uc_reg_read(engine,UC_MIPS_REG_PC,&pc);
      need(g.riu && width==2 && ((g.paintParameters && pc==0x805268dc && offset<0x200 && !(offset&3)) ||
        (offset==4 && (pc==0x8052624c || pc==0x805268dc)) || (offset==0x1c && pc==0x8052639c)),
        "isolated clipping accessed an unexamined GE status field");
      auto mask=pc==0x805268dc?0xfc8fu:offset==4?2u:0xf800u;auto address=0x1f205000+uint32_t(offset);
      if(g.symbolicControl && pc==0x805268dc){
        g.preserved.seed(4,width,g.riu->knownBits(address,width),[&](unsigned byte){return g.riu->controlByteKey(address+byte);});
        g.riu->prepareOpaqueRead(address);
      }
      g.riu->prepareMaskedRead(address,mask);++g.reads;
      auto value=g.riu->read(address,width,pc,mask,g.execution->instructions);
      if(!g.riu->stopReason.empty())uc_emu_stop(engine);
      return value;
    },&ge,+[](uc_engine* engine,uint64_t offset,unsigned width,uint64_t value,void* data){
      auto& g=*static_cast<GeFixture*>(data);uint32_t pc=0;uc_reg_read(engine,UC_MIPS_REG_PC,&pc);
      need(g.riu && width==2 && ((g.paintParameters && pc==0x80526598 && offset<0x200 && !(offset&3)) ||
        offset==0x154 || offset==0x158 || offset==0x15c || offset==0x160 ||
        offset==0xd0 || offset==0x80 || offset==0x84 || offset==0xc0 || (offset==4 && g.symbolicControl)),
        "isolated clipping submitted an unexpected GE operation");
      auto address=0x1f205000+uint32_t(offset);
      try{g.riu->write(address,width,uint32_t(value),pc,0xffff,g.execution->instructions);}
      catch(const std::runtime_error& e){
        need(g.paintParameters && offset==0x180 && std::string(e.what())==
          "GE raster command 60h unsupported; no pixels or completion fabricated","unexpected original paint peripheral exception");
        g.boundary=e.what();uc_emu_stop(engine);
      }
      need(g.riu->stopReason.empty(),"original clipping command was rejected");
      g.writes.emplace_back(address,uint16_t(value));
    },&ge));
  auto resetGe=[&](bool exploratory){ge.riu=std::make_unique<manta::Riu>();ge.writes.clear();ge.reads=0;
    ge.preserved={};ge.symbolicControl=false;ge.paintParameters=false;ge.boundary.clear();
    ge.riu->event=[](const auto&,auto,auto,auto,const auto&){};ge.riu->candidateGe=true;
    if(exploratory)ge.riu->configureExploratoryGeReset();
    // Explicit ordinary-RAM, nested-call context fixture. No mutex response
    // or hardware context is generated, and no production machine uses it.
    write(0x006ada64,0x80701000);write(0x00701538,0x55aabeef);
    write(0x00701524,0);write(0x00701528,1,2);write(0x0070100c,0xbf200000);
  };
  using Z=manta::ZuiFlow;unsigned cases=0,queueInstructions=0;
  write(0x006e561c,0x12345678);
  auto initialize=[&]{auto initializedTick=read(0x006e561c);run(0x8049f160);need(cpu.reg(UC_MIPS_REG_2)==1,"original queue initializer must return true");
    need(read(Z::headAddress)==0 && read(Z::tailAddress)==0,"original queue indices must clear");
    for(unsigned i=0;i<40;++i)need(read(Z::queueAddress+12*i,2)==0xffff,"original initializer must invalidate all 40 handles");
    for(unsigned i=0;i<8;++i)need(read(0x006d51b8+16*i,2)==0xffff,"original initializer must invalidate all eight timer handles");
    need(read(0x006d5238)==initializedTick,"original timing getter must read the supplied isolated RAM fixture");
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
  // Execute the entire original source-service prologue and its timeout gate.
  // Stop BEFORE any selected worker, OSD opener or ordinary acquisition poll.
  // The RAM combinations below are isolated function fixtures, not TV inputs.
  unsigned timeoutCases=0,timeoutTransitions=0;uint64_t timeoutInstructions=0;
  for(auto source:{0u,19u,20u,21u,22u,23u})for(auto active:{0u,3u,17u})
    for(auto timestamp:{0u,1u,0xfffffff0u})for(auto elapsed:{0u,200u,3000u,3001u,3002u,0xffffffffu})
      for(auto flags:{0u,1u,4u,5u,0xffffffffu}){
        write(0x006f8f8c,source);write(Z::activeAddress,active);
        write(0x006b9050,timestamp);write(0x006e561c,timestamp+elapsed);
        write(0x006bedf0,flags);write(0x006c0931,0,1);
        auto eligible=source>=20 && source<=22 && active==17 && timestamp!=0 && elapsed>=3001;
        auto expected=!eligible?0x80476a84u:flags&4?0x8048848cu:0x80476bbcu;
        run(0x80476a10,0,0,{0x80476a84,0x80476bbc,0x8048848c});
        need(cpu.reg(UC_MIPS_REG_PC)==expected,"original timeout gate selected the wrong worker at a boundary");
        need(!fixture.visits.contains(0x804c3684) && !fixture.visits.contains(0x804e0dc4),
          "isolated timeout test must not activate a TV OSD or dispatch it");
        need(read(0x006b9050)==timestamp && read(0x006bedf0)==flags && read(Z::activeAddress)==active,
          "timeout gate cannot manufacture an armed timer, software flag or active screen");
        timeoutInstructions+=fixture.instructions;++timeoutCases;
        if(expected==0x8048848c)++timeoutTransitions;
      }
  // The actual getter implements unsigned modulo-2^32 elapsed time. No clock
  // hook or host-written return substitutes for its original instructions.
  unsigned elapsedCases=0;
  for(auto start:{0u,1u,0x7fffffffu,0xfffffff0u,0xffffffffu})
    for(auto elapsed:{0u,1u,3000u,3001u,0x80000000u,0xffffffffu}){
      write(0x006e561c,start+elapsed);run(0x803e6bb0,start);
      need(cpu.reg(UC_MIPS_REG_2)==elapsed,"original elapsed getter must preserve unsigned wraparound");++elapsedCases;
    }
  unsigned sourcePhaseCases=0,sourcePhaseInstructions=0;
  for(auto phase:{0u,1u,2u,3u,4u})for(auto active:{0u,3u,17u}){
    write(0x006d4a08,phase);write(Z::activeAddress,active);
    write(0x006d4a10,0xdeadbeef);write(0x006d4a14,100);
    write(0x006b92d0,3000);write(0x006e561c,100);write(0x006b910c,255,1);
    auto target=phase==0?(active==3?0x804c29e0u:0x804c3684u):0u;
    run(0x80480ca8,0,target);
    if(phase==0){need(cpu.reg(UC_MIPS_REG_4)==(active==3?0x105u:3u),
      "original source-phase zero must select resource3 or existing-screen action105");}
    else need(cpu.reg(UC_MIPS_REG_2)==0,"noninitial source phase cannot invent an OSD-open result");
    need(read(Z::activeAddress)==active && read(0x006d4a10)==0,
      "source worker must clear its own result without forcing an active OSD");
    sourcePhaseInstructions+=fixture.instructions;++sourcePhaseCases;
  }
  unsigned wrapperCases=0;
  for(auto state:{0u,3u,10u,11u,14u,16u,19u,21u,0xffffffffu}){
    write(0x006c0918,state);write(0x006d7812,0,1);write(0x006b910c,255,1);
    write(Z::stateAddress,1);write(Z::activeAddress,0);write(0x006d4a08,0);
    auto target=state==0?0x80482da8u:state==3?0x8046f960u:state==11?0x804c3684u:0x804e0dc4u;
    run(0x8047602c,0,target);
    need(cpu.reg(UC_MIPS_REG_PC)==target,"original UI wrapper selected the wrong state-table worker");
    if(state==11)need(fixture.visits.contains(0x80480ca8) && cpu.reg(UC_MIPS_REG_4)==3,
      "original state11 worker must reach its own resource3 selection without a message injection");
    if(state==16 || state==19 || state>=21)need(read(0x006c0918)==3,
      "original unsupported wrapper states normalize themselves to3");
    need(read(Z::activeAddress)==0,"wrapper selection test stops before changing active OSD");++wrapperCases;
  }
  unsigned startupSourceCases=0;
  for(auto phase:{0u,1u,3u}){
    write(0x006d4b90,0);write(0x006b910c,255,1);write(0x006bedf0,0);
    write(0x006d4a08,phase);write(Z::activeAddress,0);write(0x006fbdfd,0,1);
    auto target=phase==1?0x804c3684u:phase==3?0x80481704u:0u;
    run(0x80482da8,0,target);
    if(phase==0)need(read(0x006d4a08)==3 && cpu.reg(UC_MIPS_REG_2)==0 &&
      !fixture.visits.contains(0x804c3684),"original initial source phase must enter configuration phase3 without opening an OSD");
    if(phase==1)need(cpu.reg(UC_MIPS_REG_4)==3,"original source phase1 must select OSD3 through its own getter chain");
    need(read(Z::activeAddress)==0,"isolated startup source test must stop before OSD activation");++startupSourceCases;
  }
  unsigned openCases=0,openInstructions=0,openAssertCases=0;
  // A synthetic RAM graphics-context object supplies the ordinary RAM getter
  // used by the original opener. No graphics callback or MMIO is executed.
  write(0x006d87f8,0x80700800);write(0x00700e34,1,1);
  for(unsigned id=1;id<30;++id){
    auto branch=read(0x0061f140+4*id)&0x1fffffff;
    auto instruction=read(branch);
    auto originalAssert=branch==0x004c3718;
    need(originalAssert || instruction>>26==3,"original OSD switch entry must select its own worker or assertion");
    auto target=originalAssert?0x804c3718u:0x80000000u|((instruction&0x03ffffff)<<2);
    if(originalAssert)++openAssertCases;
    write(Z::stateAddress,1);write(Z::activeAddress,0);
    write(Z::headAddress,5);write(Z::tailAddress,3);
    run(0x804c3684,id,target);
    need(read(Z::stateAddress)==2 && read(Z::activeAddress)==id &&
      read(Z::headAddress)==0 && read(Z::tailAddress)==0,
      "original opener must enter active state and clear the queue before selecting its own resource worker");
    need(fixture.visits.contains(0x8049f0dc),"original resource selection must use its own queue clear");
    openInstructions+=fixture.instructions;++openCases;
    // The same ID in state2 is an actual no-op; state0 rejects opening.
    write(Z::headAddress,5);write(Z::tailAddress,3);
    run(0x804c3684,id);need(cpu.reg(UC_MIPS_REG_2)==1 && read(Z::headAddress)==5 && read(Z::tailAddress)==3,
      "already-active original OSD must not be recreated or discard pending messages");
    write(Z::stateAddress,0);run(0x804c3684,id);
    need(cpu.reg(UC_MIPS_REG_2)==0 && read(Z::stateAddress)==0 && read(Z::activeAddress)==id,
      "uninitialized original ZUI must reject OSD activation");
    openCases+=2;
  }
  // Run the original ID3 resource worker up to its actual graphics-surface
  // entry. It, not fixture code, selects tables and creates 113 window states.
  write(Z::windowCountAddress,0,2);write(Z::windowListAddress,0);
  run(0x804afdfc,0,0x8049dfc4);
  need(read(Z::windowCountAddress,2)==113 && read(Z::windowListAddress)==0x806798b4 &&
    fixture.visits.contains(0x8049ee1c),"original resource3 worker must populate its own windows before surface creation");
  auto rectangle=cpu.reg(UC_MIPS_REG_4)&0x1fffffff;
  need(read(rectangle,2)==3 && read(rectangle+2,2)==520 && read(rectangle+4,2)==1360 && read(rectangle+6,2)==248,
    "original resource3 worker must pass its original surface rectangle");
  auto resourceWorkerInstructions=fixture.instructions;
  unsigned creationRoutes=0,creationInstructions=0;
  for(unsigned hwnd=0;hwnd<113;++hwnd){
    auto procedure=read(0x006798b4+20*hwnd,1);
    run(0x804a071c,hwnd,0x804c2dac,{},0x14,0);
    auto message=cpu.reg(UC_MIPS_REG_5)&0x1fffffff;
    need(cpu.reg(UC_MIPS_REG_4)==procedure && read(message,2)==hwnd &&
      read(message+4)==0x14 && read(message+8)==0 && fixture.visits.contains(0x804a0640),
      "original direct-message constructor must route each actual window to its own control procedure");
    creationInstructions+=fixture.instructions;++creationRoutes;
  }
  // Original root creation invalidates a rectangle directly; it does not need
  // a fabricated menu/key message in the queue. Stop the subsequent original
  // dispatcher BEFORE its paint worker: no surface or raster is supplied.
  write(Z::stateAddress,2);write(Z::activeAddress,3);
  write(Z::headAddress,0);write(Z::tailAddress,0);write(0x006d4fb4,0,1);
  run(0x804a071c,0,0,{},0x14,0);
  need(read(0x006d4fb4,1)==1 && fixture.visits.contains(0x804a2fd8) &&
    fixture.visits.contains(0x8049f78c) && fixture.visits.contains(0x8049f48c),
    "original root creation must mark the actual invalid region through its own default procedure");
  need(read(Z::headAddress)==0 && read(Z::tailAddress)==0,
    "original invalidation must not require or inject an event queue message");
  auto rootCreationInstructions=fixture.instructions;
  run(0x804e0dc4,0,0x804a2ff4);
  need(read(0x006d4fb4,1)==0 && fixture.visits.contains(0x8049f040) && fixture.visits.contains(0x8049f180),
    "original active dispatcher must consume the invalid region after observing an empty queue");
  auto paintBoundaryInstructions=fixture.instructions;
  // Continue the stopped original dispatcher with its registers and stack
  // intact. Observe the first resource primitive BEFORE its backend runs.
  fixture={};fixture.stopAt=0x8049d8d8;
  manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),50000));
  need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt && fixture.visits.contains(0x8049e820),
    "original dirty-region paint must reach its own resource primitive");
  auto paintPrimitiveInstructions=fixture.instructions;
  auto paintPrimitiveType=cpu.reg(UC_MIPS_REG_4),paintPrimitiveId=cpu.reg(UC_MIPS_REG_5);
  auto paintRectangle=cpu.reg(UC_MIPS_REG_7)&0x1fffffff;
  auto paintWindow=fixture.paintWindow;
  need(paintPrimitiveType==4 && paintPrimitiveId==65 && read(paintRectangle,2)==531 &&
    read(paintRectangle+2,2)==42 && read(paintRectangle+4,2)==7 && read(paintRectangle+6,2)==44,
    "original resource3 paint must select its own component and original rectangle");
  fixture={};fixture.stopAt=0x8049da4c;
  manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),50000));
  need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt && fixture.visits.contains(0x8049cd90),
    "original resource primitive must request the graphics-surface geometry");
  auto paintGeometryInstructions=fixture.instructions;
  auto paintResource=read((cpu.reg(UC_MIPS_REG_SP)&0x1fffffff)+0x5c);
  need(paintResource==0x80686ad0 && !(read(0x006d4f9c)&1),
    "original paint must select its actual descriptor; no surface has been allocated in this fixture");
  // The missing surface makes further geometry unusable. Separate synthetic
  // RAM cases explicitly describe a logical surface and panel width, without
  // allocating any framebuffer, supplying GWIN IDs, or executing the backend.
  unsigned paintClipCases=0,paintClipInstructions=0,paintGeInstructions=0,paintGeWrites=0;
  for(auto origin:{0u,3u})for(auto width:{530u,531u,532u,537u,538u,541u,1366u,1920u}){
    write(0x006d4f9c,1);write(0x006d4fa4,origin,2);write(0x006d4fa6,520,2);
    write(0x006d4fa8,1360,2);write(0x006d4faa,248,2);write(0x006f6a5c,width,2);
    write(0x006d4fb4,0,1);run(0x804a071c,0,0,{},0x14,0);
    run(0x804e0dc4,0,0x80520274);
    auto first=cpu.reg(UC_MIPS_REG_4)&0x1fffffff, last=cpu.reg(UC_MIPS_REG_5)&0x1fffffff;
    auto clippedWidth=width<origin+538?uint16_t(width-origin-531):7u;
    need(read(first,2)==531 && read(first+2,2)==42 &&
      read(last,2)==uint16_t(531+clippedWidth-1) && read(last+2,2)==85,
      "original geometry callback must clamp the resource rectangle and form inclusive GE clip endpoints");
    need(fixture.visits.contains(0x805e4974) && fixture.visits.contains(0x803ea14c) &&
      !fixture.visits.contains(0x80522b64) && read(Z::headAddress)==0 && read(Z::tailAddress)==0,
      "isolated paint must use original callbacks and stop before GE context access without a queued event");
    paintClipInstructions+=fixture.instructions;++paintClipCases;
    resetGe(true);fixture={};fixture.stopAt=0x80529430;
    manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),50000));
    need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt && fixture.visits.contains(0x80522b64) &&
      fixture.visits.contains(0x80526540),"original paint must program GE clipping then select its framebuffer");
    need(ge.writes==std::vector<std::pair<uint32_t,uint16_t>>{{0x1f205154,531},{0x1f20515c,42},
      {0x1f205158,uint16_t(531+clippedWidth-1)},{0x1f205160,85}} && ge.reads==8 &&
      ge.riu->geSubmitted()==4 && read(0x00701528,2)==1,
      "original GE context must submit four ordered clip coordinates and restore its nesting depth");
    need((ge.riu->knownBits(0x1f205004,2)&~2u)==0 &&
      ge.riu->knownBits(0x1f20501c,2)==manta::GeQueue::statusKnown,
      "conditional queue occupancy must not validate unrelated GE reset bits");
    paintGeInstructions+=fixture.instructions;paintGeWrites+=ge.writes.size();
  }
  resetGe(false);write(0x00700080,531,2);write(0x00700082,42,2);
  write(0x00700084,537,2);write(0x00700086,85,2);
  run(0x80520274,0x80700080,0x8052624c,{},0x80700084);
  fixture={};manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),100));
  need(!ge.riu->stopReason.empty() && ge.reads==1 && ge.writes.empty() && ge.riu->geSubmitted()==0,
    "without the existing conditional GE reset, original clipping must stop at unknown hardware status");
  cpu.write(0x006ada64,image.data()+0x806ada64-0x803b3980,4);
  run(0x8049e2ac); // original surface-variable reset, not a hardware reset
  unsigned timerCreationCases=0,timerCreationInstructions=0;
  for(auto start:{1234u,0xfffffff0u})for(auto elapsed:{99u,100u,101u}){
    write(0x006e561c,start);initialize();write(0x006d4fb4,0,1);
    run(0x804a071c,1,0,{},0x14,0);timerCreationInstructions+=fixture.instructions;
    need(fixture.visits.contains(0x804ae6c8) && fixture.visits.contains(0x8049ebec),
      "original window1 creation must install its own timers");
    for(unsigned i=0;i<8;++i){auto slot=0x006d51b8+16*i;
      if(i<4)need(read(slot,2)==1 && read(slot+4)==i && read(slot+8)==100 && read(slot+12)==0,
        "original creation must install four window1 timers with IDs0..3 and period100");
      else need(read(slot,2)==0xffff,"original creation must leave the other timer handles unused");
    }
    write(0x006e561c,start+elapsed);
    run(0x804a0770,0,elapsed>=100?0x804a071cu:0u);
    if(elapsed<100)need(read(0x006d51c4)==99 && !fixture.visits.contains(0x804a071c),
      "original software timer cannot dispatch before its actual period");
    else need(cpu.reg(UC_MIPS_REG_4)==1 && cpu.reg(UC_MIPS_REG_5)==0x64 && cpu.reg(UC_MIPS_REG_6)==0 &&
      read(0x006d51c4)==0,"original timer service must select window1/message64/timer0 and reset its own elapsed field");
    need(read(Z::headAddress)==0 && read(Z::tailAddress)==0,"timer service must not fabricate a queue event");
    ++timerCreationCases;
  }
  // Separate complete creation-callback phase. Resource selection/state setup
  // still runs originally; no successful surface constructor is substituted.
  run(0x804afdfc,0,0x8049dfc4);write(0x006e561c,38589);initialize();write(Z::stateAddress,2);write(Z::activeAddress,3);
  write(0x006f8f8c,20);write(0x006c0918,11);write(0x006d4a08,0);write(0x006d4fb4,0,1);
  write(0x006bedf0,4);write(0x006b910c,255,1);
  unsigned completeCreationWindows=0;uint64_t completeCreationInstructions=0;
  for(unsigned hwnd=0;hwnd<113;++hwnd){
    try{run(0x804a071c,hwnd,0,{},0x14,0);}
    catch(const std::exception& e){throw std::runtime_error("original creation HWND="+std::to_string(hwnd)+
      " PC="+std::to_string(cpu.reg(UC_MIPS_REG_PC))+": "+e.what());}
    completeCreationInstructions+=fixture.instructions;++completeCreationWindows;
  }
  need(read(Z::windowCountAddress,2)==113 && read(0x006d4fb4,1)==1,
    "original creation callbacks must retain their resource set and invalidate it for painting");
  auto completeCreationHead=read(Z::headAddress),completeCreationTail=read(Z::tailAddress);
  run(0x804e0dc4,0,0x8049d8d8);
  auto completePaintWindow=fixture.paintWindow,completePaintType=cpu.reg(UC_MIPS_REG_4),completePaintId=cpu.reg(UC_MIPS_REG_5);
  auto completePaintInstructions=fixture.instructions;
  unsigned bitmapMetadataCases=0;uint64_t bitmapCopyInstructions=0,bitmapRegistrationInstructions=0;
  uint32_t bitmap196Address=0;std::string bitmap196Sha;unsigned bitmapSourceWrites=0,bitmapSourceInstructions=0;
  unsigned bitmapSymbolicSourceWrites=0,bitmapSymbolicSourceInstructions=0;
  unsigned allocatedPaintInstructions=0,allocatedPaintWrites=0,allocatedEnableInstructions=0;uint32_t allocatedPaintPC=0,allocatedPaintCommand=0,allocatedPaintRA=0;
  std::vector<std::array<uint32_t,3>> allocatedPaintRegisters;
  bool allocatedPaintDither=false;
  if(argc==4){
    auto load=[](const char* path){std::ifstream input(path,std::ios::binary);
      return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),{});};
    auto resource=load(argv[2]),prefix=load(argv[3]);
    need(resource.size()==2310744 && manta::sha256(resource)=="67811a4bd3b115e8221a8d233646f4f036be76e0d3cc585353fd92bed675f0aa",
      "original decoded bitmap resource mismatch");
    need(prefix.size()==0x3800 && manta::sha256(prefix)=="006c07f45891c9f85cd92af34b175854a3db8e6d66212e7a94c7661b782d2cea",
      "original runtime directory prefix mismatch");
    cpu.write(0x003b0180,prefix.data(),prefix.size());
    cpu.map(0x027aa000,0x10000);cpu.map(0x028aa000,0x235000);cpu.map(0x03c3d000,0x234000);
    cpu.write(0x028aa000,resource.data(),resource.size());
    // Diagnostic starts AFTER decompression with exact hash-verified data.
    // Execute the original split/copy instructions, without claiming that
    // this fixture performed connected firmware loading or decompression.
    run(0x803e57c8,0,0x803e58cc,{},0,0,30000000);bitmapCopyInstructions=fixture.instructions;
    std::vector<uint8_t> copied(resource.size()-1816);cpu.read(0x03c3d000,copied.data(),copied.size());
    need(std::equal(copied.begin(),copied.end(),resource.begin()+1816),"original bitmap pixel-pool copy differs");
    std::vector<uint8_t> header(1816);cpu.read(0x027aa000,header.data(),header.size());
    need(std::equal(header.begin(),header.end(),resource.begin()),"original bitmap metadata split differs");
    run(0x803e457c); // original pool-cursor initialization
    run(0x803e5498,0x80702000,0,{},0,0,1000000);bitmapRegistrationInstructions=fixture.instructions;
    uint32_t cursor=0x03c3d000;
    for(unsigned id=0;id<350;++id){
      auto width=(uint32_t(header[66+4*id])<<8)|header[67+4*id];
      auto height=(uint32_t(header[68+4*id])<<8)|header[69+4*id];
      need(header[66+1400+id]==1,"unexpected original bitmap encoding in this pinned image");
      need(read(0x00702000+2*id,2)==id,"original allocator must assign bitmap handles in original order");
      run(0x803ee5c0,id,0,{},0x80700080);
      need(cpu.reg(UC_MIPS_REG_2)==0 && read(0x00700080)==cursor && read(0x00700084)==width*height*2 &&
        read(0x00700088,1)==1 && read(0x0070008c)==width && read(0x00700090)==width*2 &&
        read(0x00700094)==height && read(0x00700098)==9,
        "original bitmap callback must describe its actual allocated source bytes and native format");
      if(id==196){bitmap196Address=cursor;auto offset=cursor-0x03c3d000;
        need(width==7 && height==42,"first paint bitmap metadata must be original 7x42 asset196");
        bitmap196Sha=manta::sha256(std::span(copied).subspan(offset,width*height*2));
      }
      cursor=(cursor+width*height*2+7)&~7u;++bitmapMetadataCases;
    }
    need(cursor==0x03c3d000+copied.size(),"original bitmap allocations must account for the complete pixel pool");
    for(auto id:{350u,0xffffu,0xffffffffu}){
      write(0x00700080,0xdeadbeef);run(0x803ee5c0,id,0,{},0x80700080);
      need(cpu.reg(UC_MIPS_REG_2)==6 && read(0x00700080)==0xdeadbeef,
        "original metadata callback must reject invalid IDs without inventing an output descriptor");
    }
    // The original initialization fragment registers both real resource
    // callbacks. No host callback returns bitmap data or reports success.
    run(0x804044ec,0,0x805cbbd8);
    need(read(0x006ada4c)==0x803ee5c0 && read(0x006ada50)==0x803ee508,
      "original graphics setup must register its own bitmap and font callbacks");
    resetGe(true);write(0x0070107c,0,2); // explicit synthetic software-shadow format, not a hardware reset
    run(0x8051ff80,196,0x805254ec,{},0x80700080);
    auto sp=cpu.reg(UC_MIPS_REG_SP)&0x1fffffff;
    need(cpu.reg(UC_MIPS_REG_5)==9 && cpu.reg(UC_MIPS_REG_6)==7 && cpu.reg(UC_MIPS_REG_7)==42 &&
      read(sp+0x10)==bitmap196Address && read(sp+0x14)==14 && read(sp+0x18)==0 &&
      fixture.visits.contains(0x803ee5c0),
      "original bitmap frontend must obtain real asset196 through the registered callback before source-buffer binding");
    fixture={};manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),50000));
    need(!ge.riu->stopReason.empty() && cpu.reg(UC_MIPS_REG_PC)==0x805268dc &&
      ge.writes==std::vector<std::pair<uint32_t,uint16_t>>{{0x1f2050d0,9},
        {0x1f205080,uint16_t(bitmap196Address)},{0x1f205084,uint16_t(bitmap196Address>>16)},{0x1f2050c0,14}},
      "original source binding must program actual asset format/address/pitch then stop on unresolved GE control bits");
    bitmapSourceWrites=ge.writes.size();bitmapSourceInstructions=fixture.instructions;
    need(ge.riu->knownBits(0x1f205004,2)==2 && ge.riu->geSubmitted()==4,
      "source setup cannot turn unknown GE control fields into valid bits or raster work");
    // A second isolated continuation uses the EXISTING bit-origin tracker.
    // Unchanged unknown control bits may only return to their original RIU
    // identities; decisions, changed unknown bits and new destinations reject.
    resetGe(true);ge.symbolicControl=true;ge.riu->symbolicInit=true;
    run(0x8051ff80,196,0x805254ec,{},0x80700080);
    fixture={};fixture.stopAt=0x8051fff8;fixture.preserved=&ge.preserved;fixture.riu=ge.riu.get();
    manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),50000));
    need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt && ge.riu->stopReason.empty() &&
      cpu.reg(UC_MIPS_REG_2)==1 && !ge.preserved.mask(2) && read(0x00701528,2)==1,
      "original source binding must return itself with preserved unknown control bits and restored nesting");
    need(ge.writes.size()==5 && ge.writes.back().first==0x1f205004 &&
      ge.riu->knownBits(0x1f205004,2)==0x372 && ge.riu->geSubmitted()==5,
      "same-register original control RMW must validate only its known fields, never unrelated hardware bits");
    bitmapSymbolicSourceWrites=ge.writes.size();bitmapSymbolicSourceInstructions=fixture.instructions;
    // A separate isolated continuation joins original allocation with the
    // original dirty-region painter. Surface geometry and software mutex
    // ownership remain explicit RAM fixtures; no production CPU is affected.
    constexpr uint32_t local=0x006ff0b0;
    write(0x006d87f8,0x806ff0b0);write(local,0x806ffb4c);write(local+4,0x80700040);
    write(0x00700054,6,2);write(0x00700080,1920,2);write(0x00700082,1080,2);
    write(0x00700088,0x00ddd800);write(0x0070008c,0x72f400);
    fixture={};fixture.stopAt=0x805334a4;
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_20,0x80700080);cpu.reg(UC_MIPS_REG_21,0x806e0000);
    write(0x0070ff18,1,1);manta::check(cpu.run(0x80533424,50000));
    need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt,"paint fixture original pool initialization failed");
    fixture={};fixture.stopAt=0x803b0000;cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);
    cpu.reg(UC_MIPS_REG_4,0);cpu.reg(UC_MIPS_REG_5,0);cpu.reg(UC_MIPS_REG_6,0);cpu.reg(UC_MIPS_REG_7,1360);
    write(0x0070ff10,248,2);write(0x0070ff14,12,2);write(0x0070ff18,0);
    manta::check(cpu.run(0x8052c3c0,50000));
    need(cpu.reg(UC_MIPS_REG_PC)==fixture.stopAt && cpu.reg(UC_MIPS_REG_2)==1 &&
      read(local+0x50)==0x00ddd800 && read(local+0x4a,2)==2720,
      "paint fixture must use an original allocated framebuffer record");
    run(0x805293f8,0x803ee738);write(local+0xc,0);write(local+0x10,0xffffffff);write(local+0x14,1);
    write(0x006d4f9c,1);write(0x006d4fa4,0,2);write(0x006d4fa6,520,2);
    write(0x006d4fa8,1360,2);write(0x006d4faa,248,2);write(0x006f6a5c,1920,2);
    resetGe(true);ge.symbolicControl=true;ge.paintParameters=true;ge.riu->symbolicInit=true;
    // Use the original GE initializer's enable write through GE_WriteReg.
    // This defines the software shadow and device bit causally; do not assume
    // an enabled engine from the empty-queue hypothesis or a zeroed context.
    run(0x80526540,0x80701008,0,{},0,1);allocatedEnableInstructions=fixture.instructions;
    write(0x006d4fb4,0,1);run(0x804a071c,0,0,{},0x14,0);
    run(0x804e0dc4,0,0x80520274);
    fixture={};fixture.preserved=&ge.preserved;fixture.riu=ge.riu.get();
    manta::check(cpu.run(cpu.reg(UC_MIPS_REG_PC),100000));
    allocatedPaintInstructions=fixture.instructions;allocatedPaintWrites=ge.writes.size();allocatedPaintPC=cpu.reg(UC_MIPS_REG_PC);
    allocatedPaintRA=cpu.reg(UC_MIPS_REG_RA);
    allocatedPaintDither=fixture.visits.contains(0x8052272c);
    if(!ge.writes.empty() && ge.writes.back().first==0x1f205180)allocatedPaintCommand=ge.writes.back().second;
    need(allocatedPaintPC==0x80526598 && allocatedPaintCommand==0x8040 && allocatedPaintWrites==52 &&
      ge.boundary=="GE raster command 60h unsupported; no pixels or completion fabricated" && ge.riu->stopReason.empty() &&
      fixture.visits.contains(0x80529430) && fixture.visits.contains(0x803ee738) && fixture.visits.contains(0x803ea1a4) &&
      fixture.visits.contains(0x803ee5c0) && allocatedPaintDither && ge.riu->geSubmitted()==51,
      "original resource3 bitmap painting must reach its own unsupported raster request through its real callbacks");
    for(auto [address,value]:ge.writes)allocatedPaintRegisters.push_back({(address-0x1f205000)/4,value,ge.riu->knownBits(address,2)});
    need(ge.riu->knownBits(0x1f205080,2)==0xffff && ge.riu->knownBits(0x1f205084,2)==0xffff &&
      read(local+0x50)==0x00ddd800,"original bitmap paint must retain valid source binding and its allocated destination");
    ge.riu->prepareMaskedRead(0x1f205000,1);
    need((ge.riu->read(0x1f205000,2,0,1,fixture.instructions+1000)&1) && ge.riu->stopReason.empty(),
      "original enable/configuration sequence must retain enabled engine bit without completing the unsupported raster");
    // These two linked RAM flags are changed by the original graphics setup:
    // blend mode 80520100 and drawing mode 8051F9F8. Restore only these known
    // mutable data bytes before checking the entire code/resource image.
    for(auto address:{0x006ada54u,0x006ada5au})cpu.write(address,image.data()+address-0x003b3980,1);
    cpu.write(0x006ada64,image.data()+0x806ada64-0x803b3980,4);
    cpu.write(0x006ada4c,image.data()+0x806ada4c-0x803b3980,8);
  }
  std::vector<uint8_t> after(image.size());cpu.read(0x003b3980,after.data(),after.size());
  need(after==image,"original instruction/resource image changed");
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"ISOLATED original-function execution with synthetic RAM, not television boot or events\","
    <<"\"queue_cases\":"<<cases-4<<",\"standby_default_cases\":4,\"active_dispatch_cases\":"<<runningCases
    <<",\"queue_original_instructions\":"<<queueInstructions<<",\"window_resource_cases\":"<<resourceCases
    <<",\"original_window_records\":"<<resourceWindows<<",\"window_state_instructions\":"<<resourceInstructions
    <<",\"source_timeout_cases\":"<<timeoutCases<<",\"source_timeout_transition_boundaries\":"<<timeoutTransitions
    <<",\"source_timeout_original_instructions\":"<<timeoutInstructions<<",\"elapsed_getter_cases\":"<<elapsedCases
    <<",\"source_phase_cases\":"<<sourcePhaseCases<<",\"source_phase_original_instructions\":"<<sourcePhaseInstructions
    <<",\"wrapper_selection_cases\":"<<wrapperCases<<",\"resource3_worker_instructions\":"<<resourceWorkerInstructions
    <<",\"startup_source_cases\":"<<startupSourceCases
    <<",\"resource3_windows_before_surface\":113"
    <<",\"OSD_open_selection_cases\":"<<openCases<<",\"OSD_open_original_instructions\":"<<openInstructions
    <<",\"OSD_open_original_assert_ids\":"<<openAssertCases
    <<",\"resource3_creation_routes\":"<<creationRoutes<<",\"creation_route_instructions\":"<<creationInstructions
    <<",\"root_creation_instructions\":"<<rootCreationInstructions<<",\"paint_boundary_instructions\":"<<paintBoundaryInstructions
    <<",\"isolated_empty_queue_reaches_paint_boundary\":true"
    <<",\"paint_primitive_instructions\":"<<paintPrimitiveInstructions
    <<",\"paint_primitive_type\":"<<paintPrimitiveType<<",\"paint_primitive_id\":"<<paintPrimitiveId
    <<",\"paint_geometry_instructions\":"<<paintGeometryInstructions
    <<",\"paint_window\":"<<paintWindow<<",\"paint_resource_address\":"<<paintResource
    <<",\"paint_clip_synthetic_RAM_cases\":"<<paintClipCases<<",\"paint_clip_original_instructions\":"<<paintClipInstructions
    <<",\"paint_GE_context_cases\":"<<paintClipCases<<",\"paint_GE_original_instructions\":"<<paintGeInstructions
    <<",\"paint_GE_clip_writes\":"<<paintGeWrites<<",\"paint_strict_GE_unknown_stops\":true"
    <<",\"paint_GE_context_executed\":true,\"paint_surface_allocated\":false,\"paint_GE_raster_issued\":false"
    <<",\"original_window_timer_cases\":"<<timerCreationCases<<",\"window_timer_creation_instructions\":"<<timerCreationInstructions
    <<",\"complete_creation_callback_windows\":"<<completeCreationWindows
    <<",\"complete_creation_original_instructions\":"<<completeCreationInstructions
    <<",\"complete_creation_queue_head\":"<<completeCreationHead<<",\"complete_creation_queue_tail\":"<<completeCreationTail
    <<",\"post_creation_first_paint_window\":"<<completePaintWindow<<",\"post_creation_first_paint_type\":"<<completePaintType
    <<",\"post_creation_first_paint_resource\":"<<completePaintId<<",\"post_creation_paint_original_instructions\":"<<completePaintInstructions
    <<",\"bitmap_metadata_cases\":"<<bitmapMetadataCases<<",\"bitmap_copy_original_instructions\":"<<bitmapCopyInstructions
    <<",\"bitmap_registration_original_instructions\":"<<bitmapRegistrationInstructions
    <<",\"bitmap196_source_address\":"<<bitmap196Address<<",\"bitmap196_source_sha256\":\""<<bitmap196Sha<<"\""
    <<",\"bitmap_source_GE_writes\":"<<bitmapSourceWrites<<",\"bitmap_source_original_instructions\":"<<bitmapSourceInstructions
    <<",\"bitmap_symbolic_source_GE_writes\":"<<bitmapSymbolicSourceWrites
    <<",\"bitmap_symbolic_source_original_instructions\":"<<bitmapSymbolicSourceInstructions
    <<",\"allocated_paint_original_instructions\":"<<allocatedPaintInstructions
    <<",\"allocated_paint_original_enable_instructions\":"<<allocatedEnableInstructions
    <<",\"allocated_paint_GE_writes\":"<<allocatedPaintWrites<<",\"allocated_paint_stop_PC\":"<<allocatedPaintPC
    <<",\"allocated_paint_submit_RA\":"<<allocatedPaintRA
    <<",\"allocated_paint_original_dither_helper\":"<<(allocatedPaintDither?"true":"false")
    <<",\"allocated_paint_command\":"<<allocatedPaintCommand<<",\"allocated_paint_GE_registers\":[";
  for(unsigned i=0;i<allocatedPaintRegisters.size();++i){if(i)std::cout<<',';auto v=allocatedPaintRegisters[i];
    std::cout<<'['<<v[0]<<','<<v[1]<<','<<v[2]<<']';}
  std::cout<<']'
    <<",\"TV_dispatcher_invoked\":false,\"TV_events_injected\":false,\"pixels_generated\":false,"
    <<"\"program_sha256\":\""<<manta::sha256(image)<<"\"}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
