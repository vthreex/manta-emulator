// Original surface-clear instructions with explicit isolated software contexts.
// No connected CPU, display surface, ready reply or host rasterizer is used.
#include "cpu/engine.hpp"
#include "board/preserved_bits.hpp"
#include "peripherals/riu.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Case {
  manta::Engine cpu;manta::Riu riu;manta::PreservedBits provenance;
  uint64_t instructions=0;uint32_t stop=0;std::string boundary;unsigned rasterWrites=0;
  std::vector<std::pair<unsigned,unsigned>> writes;
  Case(const std::vector<uint8_t>& image,bool raster=false){
    cpu.map(0x003b0000,0x350000);cpu.map(0x00700000,0x10000);
    cpu.write(0x003b3980,image.data(),image.size());
    riu.candidateGe=true;riu.symbolicInit=true;riu.configureExploratoryGeReset();
    riu.candidateGeRectFill=raster;
    if(raster){
      cpu.map(0x01000000,0x1000);std::vector<uint8_t> sentinel(0x1000,0xa5);cpu.write(0x01000000,sentinel.data(),sentinel.size());
      riu.dramWritable=[](uint32_t address,size_t size){return address>=0x01000000 && uint64_t(address)+size<=0x01001000;};
      riu.dramWrite=[this](uint32_t address,std::span<const uint8_t> bytes){++rasterWrites;cpu.write(address,bytes.data(),bytes.size());};
    }
    riu.event=[](const auto&,auto,auto,auto,const auto&){};
    uc_hook hook;manta::check(uc_hook_add(cpu.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(
      +[](uc_engine* engine,uint64_t pc,uint32_t,void* data){auto& c=*static_cast<Case*>(data);
        if(pc==c.stop){uc_emu_stop(engine);return;}
        need(pc>=0x803b3980 && pc<0x80613000,"clear probe escaped original instructions");
        uint32_t word=0;c.cpu.read(uint32_t(pc)&0x1fffffff,&word,4);
        auto why=c.provenance.observe(word,[&](unsigned reg){return c.cpu.reg(UC_MIPS_REG_0+int(reg));},
          [&](uint32_t address){return c.riu.preservedStoreKey(address);},
          [&](uint32_t address,unsigned width,uint32_t unknown){c.riu.preparePreservedWrite(address,width,unknown);});
        need(why.empty(),why.c_str());++c.instructions;
      }),this,1,0));
    manta::check(uc_mmio_map(cpu.handle,0x1f205000,0x1000,
      +[](uc_engine* engine,uint64_t offset,unsigned width,void* data)->uint64_t{
        auto& c=*static_cast<Case*>(data);auto pc=c.cpu.reg(UC_MIPS_REG_PC);
        need(width==2 && ((offset==4 && (pc==0x8052624c || pc==0x805268dc)) ||
          (offset==0x1c && pc==0x8052639c)),"clear probe accessed an unexamined hardware field");
        auto address=0x1f205000+uint32_t(offset);
        if(pc==0x805268dc){
          c.provenance.seed(4,width,c.riu.knownBits(address,width),[&](unsigned byte){return c.riu.controlByteKey(address+byte);});
          c.riu.prepareOpaqueRead(address);
        }else c.riu.prepareMaskedRead(address,offset==4?2:0xf800);
        auto value=c.riu.read(address,width,pc,0,c.instructions);
        if(!c.riu.stopReason.empty()){c.boundary=c.riu.stopReason;uc_emu_stop(engine);}
        return value;
      },this,+[](uc_engine* engine,uint64_t offset,unsigned width,uint64_t value,void* data){
        auto& c=*static_cast<Case*>(data);auto index=unsigned(offset/4);
        need(width==2 && !(offset&3) && (index==0 || index==1 || index==0x59 || index==0x60 ||
          (index>=0x55 && index<=0x58) || (index>=0x68 && index<=0x6f) || index==0x70 || index==0x71 ||
          index==0x64 || index==0x65 || index==0x5e || index==0x5f ||
          index==0x34 || index==0x26 || index==0x27 || index==0x33 || index==0x20 || index==0x21 || index==0x30),
          "clear probe encountered an unexamined GE command");
        c.writes.emplace_back(index,unsigned(value));
        try{c.riu.write(0x1f205000+uint32_t(offset),width,uint32_t(value),c.cpu.reg(UC_MIPS_REG_PC),0xffff,c.instructions);}
        catch(const std::runtime_error& e){
          need(index==0x60 && std::string(e.what())=="GE raster command 60h unsupported; no pixels or completion fabricated",
            "unexpected clear peripheral exception");
          c.boundary=e.what();uc_emu_stop(engine);
        }
        need(c.riu.stopReason.empty(),"clear configuration submission failed");
      },this));
    write(0x006ada64,0x80701000);write(0x00701538,0x55aabeef);
    write(0x00701524,0);write(0x00701528,1,2);write(0x0070100c,0xbf200000);
  }
  void write(uint32_t address,uint32_t value,unsigned width=4){uint8_t bytes[4]{};
    for(unsigned i=0;i<width;++i)bytes[i]=uint8_t(value>>(8*i));cpu.write(address,bytes,width);}
  uint32_t read(uint32_t address,unsigned width=4){uint8_t bytes[4]{};cpu.read(address,bytes,width);uint32_t value=0;
    for(unsigned i=0;i<width;++i)value|=uint32_t(bytes[i])<<(8*i);return value;}
  void entry(uint32_t pc,uint32_t until){
    for(int reg=UC_MIPS_REG_0;reg<=UC_MIPS_REG_31;++reg)cpu.reg(reg,0);
    cpu.reg(UC_MIPS_REG_SP,0x8070ff00);cpu.reg(UC_MIPS_REG_RA,0x803b0000);stop=until;
    cpu.reg(UC_MIPS_REG_PC,pc);
  }
};
int main(int argc,char** argv){try{
  need(argc==2,"supply original main image; isolated constructor-clear packet tests only");
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> image(std::istreambuf_iterator<char>(input),{});
  need(image.size()==3167072 && manta::sha256(image)=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236",
    "original main image mismatch");
  unsigned cases=0,requests=0,empty=0;uint64_t instructions=0;
  for(auto dimensions:std::vector<std::pair<unsigned,unsigned>>{{1360,248},{4,3},{1,1},{0,3},{4,0}}){
    auto [width,height]=dimensions;auto c=std::make_unique<Case>(image);
    c->write(0x00700080,0,2);c->write(0x00700082,0,2);
    c->write(0x00700084,width,2);c->write(0x00700086,height,2);
    c->entry(0x80520274,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);c->cpu.reg(UC_MIPS_REG_5,0x80700084);
    manta::check(c->cpu.run(0x80520274,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original clear clip setup failed");
    c->writes.clear();auto before=c->riu.geSubmitted();
    // Unchanged constructor fragment creates its own clear descriptor from
    // explicit width/height/scratch register fixtures. No allocation result,
    // framebuffer base or completed operation is supplied.
    c->entry(0x8049e158,0x8049e18c);c->cpu.reg(UC_MIPS_REG_19,0x806fca54);
    c->cpu.reg(UC_MIPS_REG_20,width);c->cpu.reg(UC_MIPS_REG_21,0x806d0000);c->cpu.reg(UC_MIPS_REG_22,height);
    manta::check(c->cpu.run(0x8049e158,50000));
    if(width && height){
      need(c->boundary=="GE raster command 60h unsupported; no pixels or completion fabricated" &&
        c->cpu.reg(UC_MIPS_REG_PC)==0x80526598,"original nonempty clear must stop on its actual unsupported raster submission");
      need(c->writes.size()==9 && !(c->writes[1].second&0x372),"clear must consume only known GE control fields");
      need(c->writes==std::vector<std::pair<unsigned,unsigned>>{{0x59,0},{1,c->writes[1].second},{0x68,0},{0x69,0},
        {0x6a,width-1},{0x6b,height-1},{0x70,0},{0x71,0xff00},{0x60,0x30}},
        "original clear packet differs from recovered rectangle/color/command sequence");
      need(c->riu.geSubmitted()-before==8,"unsupported raster trigger must not enter or retire in the configuration queue");++requests;
    }else{
      // 803EACF0 overwrites the lower API's return with descriptor+28 (0).
      need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->cpu.reg(UC_MIPS_REG_2)==0 && c->boundary.empty() && c->writes.empty(),
        "original zero-size clear must return without a raster request");++empty;
    }
    need(c->riu.knownBits(0x1f205004,2)==(width&&height?0x372u:2u),"clear must preserve unresolved control-bit provenance");
    instructions+=c->instructions;++cases;
  }
  unsigned capabilityCases=0,destinationCases=0;uint64_t destinationInstructions=0;
  for(auto test:std::vector<std::array<unsigned,5>>{{9,1,1,2,2},{12,1,6,4,4},{12,0,1,2,2},{15,0,1,4,4}}){
    auto c=std::make_unique<Case>(image);c->entry(0x80525e28,0x803b0000);
    c->cpu.reg(UC_MIPS_REG_4,0x80701008);c->cpu.reg(UC_MIPS_REG_5,test[0]);
    c->cpu.reg(UC_MIPS_REG_6,test[1]);c->cpu.reg(UC_MIPS_REG_7,0x80700080);
    manta::check(c->cpu.run(0x80525e28,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->cpu.reg(UC_MIPS_REG_2)==test[2] &&
      c->read(0x00700084,1)==test[3] && c->read(0x00700085,1)==test[4] && c->writes.empty(),
      "original format capability must distinguish source9, rejected sourceC, destinationC and destinationF");
    ++capabilityCases;
  }
  for(auto format:{9u,12u,15u,4u})for(auto physical:{0x01000000u,0x09000000u}){
    auto c=std::make_unique<Case>(image);auto pitch=1360*(format==15?4:format==4?1:2);
    // A descriptor fixture is not an allocated/active framebuffer. The
    // pointed-to physical region is deliberately NOT mapped in this probe.
    c->write(0x00700080,physical);c->write(0x00700084,1360);c->write(0x00700088,248);
    c->write(0x0070008c,pitch);c->write(0x00700090,format);
    c->entry(0x8051fef8,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);
    manta::check(c->cpu.run(0x8051fef8,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->cpu.reg(UC_MIPS_REG_2)==1 && c->boundary.empty() &&
      c->read(0x00701528,2)==1 && c->read(0x0070153d,1)==unsigned(format==4),
      "original destination binding must return and restore its nesting; only format4 selects palette handling");
    need(c->writes.size()==5 && c->writes==std::vector<std::pair<unsigned,unsigned>>{{0x34,format<<8},
      {0x26,0},{0x27,physical==0x01000000?0x100u:0x8100u},{0x33,pitch},{1,c->writes.back().second}} &&
      !(c->writes.back().second&0x372) && c->riu.knownBits(0x1f205004,2)==0x372 && c->riu.geSubmitted()==5,
      "original destination API must program its distinct format/address/pitch fields without validating unknown controls");
    destinationInstructions+=c->instructions;++destinationCases;
  }
  // Original CPU-side GOP allocation, not a supplied descriptor/return value.
  // These are isolated ordinary-RAM fixtures. The observed boot pool arguments
  // do not prove a connected surface exists or that its memory contains pixels.
  unsigned allocationCases=0,allocationRejects=0,allocatedBindings=0,allocatedClearRequests=0;uint64_t allocationInstructions=0;
  for(auto extent:{0x72f400u,128u}){
    auto c=std::make_unique<Case>(image);
    constexpr uint32_t local=0x006ff0b0,shared=0x006ffb4c,base=0x00ddd800;
    c->write(0x006d87f8,0x806ff0b0);c->write(local,0x806ffb4c);
    c->write(local+4,0x80700040);c->write(0x00700054,6,2);
    c->write(0x00700080,1920,2);c->write(0x00700082,1080,2);
    c->write(0x00700088,base);c->write(0x0070008c,extent);
    c->entry(0x80533424,0x805334a4);c->cpu.reg(UC_MIPS_REG_20,0x80700080);
    c->cpu.reg(UC_MIPS_REG_21,0x806e0000);c->write(0x0070ff18,1,1);
    manta::check(c->cpu.run(0x80533424,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->writes.empty() &&
      c->read(local+0x1c)==base && c->read(local+0x20)==extent &&
      c->read(local+0x24,2)==0xffff && c->read(local+0x38)==1 &&
      c->read(shared+8,2)==1920 && c->read(shared+10,2)==1080,
      "original GOP initialization fragment must establish its own empty RAM pool");
    const auto width=extent==128?8u:1360u,height=extent==128?8u:248u;
    const auto bytes=width*2*height;
    auto allocate=[&](unsigned id){
      c->entry(0x8052c3c0,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,id);
      c->cpu.reg(UC_MIPS_REG_5,0);c->cpu.reg(UC_MIPS_REG_6,0);c->cpu.reg(UC_MIPS_REG_7,width);
      c->write(0x0070ff10,height,2);c->write(0x0070ff14,12,2);c->write(0x0070ff18,0);
      manta::check(c->cpu.run(0x8052c3c0,50000));
      need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->writes.empty(),
        "original RAM allocation must return without a hardware response or drawing");
      return c->cpu.reg(UC_MIPS_REG_2);
    };
    for(unsigned id=0;id<(extent==128?1u:2u);++id){
      need(allocate(id)==1,"original GOP allocation must create its own record");
      const auto record=local+44*id;
      need((c->read(record+0x3c)&0x300)==0x200 && c->read(record+0x50)==base+bytes*id &&
        c->read(record+0x54)==bytes && c->read(record+0x4a,2)==width*2 &&
        c->read(record+0x46,2)==width && c->read(record+0x48,2)==height &&
        c->read(record+0x4c,2)==0x0c0c && c->read(record+0x5c,2)==0xffff,
        "original allocation record address/dimensions/pitch/format/links differ");
      if(id)need(c->read(local+0x5c,2)==id,"original allocator must link consecutive records");
      ++allocationCases;
    }
    std::array<uint8_t,0xa9c> before{},after{};
    c->cpu.read(local,before.data(),before.size());
    for(auto invalid:{0u,32u,255u}){
      need(allocate(invalid)==255,"duplicate/out-of-range framebuffer IDs must be rejected");
      c->cpu.read(local,after.data(),after.size());
      need(before==after,"rejected allocation must leave GOP metadata unchanged");++allocationRejects;
    }
    // The real selector obtains pitch/address/format from the record above and
    // invokes the original registered 803EE738 callback. No host descriptor is
    // substituted. Only the mutex's ordinary software ownership is a fixture;
    // original 805CB82C returns -1 without any host system-call replacement.
    c->entry(0x805293f8,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x803ee738);
    manta::check(c->cpu.run(0x805293f8,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->read(0x006d880c)==0x803ee738,
      "original framebuffer callback registration failed");
    c->write(local+0xc,0);c->write(local+0x10,0xffffffff);c->write(local+0x14,1);
    c->entry(0x80529430,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0);
    manta::check(c->cpu.run(0x80529430,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->cpu.reg(UC_MIPS_REG_2)==1 &&
      c->read(local+0x14)==1 && c->read(0x00701528,2)==1 && c->boundary.empty(),
      "original allocated-record binding must return and preserve nested software ownership");
    need(c->writes.size()==5 && c->writes==std::vector<std::pair<unsigned,unsigned>>{{0x34,0xc00},
      {0x26,base&0xffff},{0x27,base>>16},{0x33,width*2},{1,c->writes.back().second}} &&
      c->riu.knownBits(0x1f205004,2)==0x372 && c->read(0x0070153d,1)==0,
      "original allocated framebuffer must bind its own GE address/pitch/format without palette or raster work");
    ++allocatedBindings;
    c->write(0x00700080,0,2);c->write(0x00700082,0,2);
    c->write(0x00700084,width,2);c->write(0x00700086,height,2);
    c->entry(0x80520274,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);c->cpu.reg(UC_MIPS_REG_5,0x80700084);
    manta::check(c->cpu.run(0x80520274,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"allocated clear clip setup failed");
    c->writes.clear();auto submitted=c->riu.geSubmitted();
    c->entry(0x8049e158,0x8049e18c);c->cpu.reg(UC_MIPS_REG_19,0x806fca54);
    c->cpu.reg(UC_MIPS_REG_20,width);c->cpu.reg(UC_MIPS_REG_21,0x806d0000);c->cpu.reg(UC_MIPS_REG_22,height);
    manta::check(c->cpu.run(0x8049e158,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==0x80526598 &&
      c->boundary=="GE raster command 60h unsupported; no pixels or completion fabricated" && c->writes.size()==9 &&
      c->writes==std::vector<std::pair<unsigned,unsigned>>{{0x59,0},{1,c->writes[1].second},
        {0x68,0},{0x69,0},{0x6a,width-1},{0x6b,height-1},{0x70,0},{0x71,0xff00},{0x60,0x30}} &&
      c->riu.geSubmitted()-submitted==8 && c->read(local+0x50)==base,
      "allocated original clear must reach the real unsupported raster trigger without completing or writing pixels");
    ++allocatedClearRequests;
    allocationInstructions+=c->instructions;
  }
  unsigned originalRasterCases=0;uint64_t rasterInstructions=0;
  for(auto format:{8u,15u}){
    auto c=std::make_unique<Case>(image,true);constexpr unsigned width=4,height=3,pitch=24;
    // Actual original initializer's GE-enable write (805267C0..CC), using
    // original GE_WriteReg. The input context/descriptor are isolated RAM;
    // no connected UI or successful allocation is manufactured.
    c->entry(0x80526540,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80701008);
    c->cpu.reg(UC_MIPS_REG_5,0);c->cpu.reg(UC_MIPS_REG_6,1);
    manta::check(c->cpu.run(0x80526540,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original GE enable write failed");
    c->write(0x00700080,0x01000000);c->write(0x00700084,width);c->write(0x00700088,height);
    c->write(0x0070008c,pitch);c->write(0x00700090,format);
    c->entry(0x8051fef8,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);
    manta::check(c->cpu.run(0x8051fef8,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original raster destination setup failed");
    c->write(0x00700080,0,2);c->write(0x00700082,0,2);c->write(0x00700084,width-1,2);c->write(0x00700086,height-1,2);
    c->entry(0x80520274,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);c->cpu.reg(UC_MIPS_REG_5,0x80700084);
    manta::check(c->cpu.run(0x80520274,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original raster clipping failed");
    c->writes.clear();c->entry(0x8049e158,0x8049e18c);c->cpu.reg(UC_MIPS_REG_19,0x806fca54);
    c->cpu.reg(UC_MIPS_REG_20,width);c->cpu.reg(UC_MIPS_REG_21,0x806d0000);c->cpu.reg(UC_MIPS_REG_22,height);
    manta::check(c->cpu.run(0x8049e158,50000));
    need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->boundary.empty() && c->riu.geRectangles==0 && c->rasterWrites==0,
      "original asynchronous fill must return with pending work, without immediate completion/pixels");
    c->riu.prepareMaskedRead(0x1f20501c,manta::GeQueue::statusKnown);
    c->riu.read(0x1f20501c,2,0,0,c->instructions+1000);
    need(c->riu.stopReason.empty() && c->riu.geRectangles==1 && c->riu.geRasterPixels==12 && c->rasterWrites==3,
      "queued original clear must retire only after its actual guest RAM row writes");
    std::vector<uint8_t> pixels(0x1000);c->cpu.read(0x01000000,pixels.data(),pixels.size());
    const auto bpp=format==15?4u:2u;
    for(unsigned i=0;i<pixels.size();++i){auto x=i%pitch,y=i/pitch;
      auto expected=y<height && x<width*bpp?(format==15 && x%4==3?0xff:0):0xa5;
      need(pixels[i]==expected,"original clear raster memory differs or overwrites pitch padding/outside rows");}
    ++originalRasterCases;rasterInstructions+=c->instructions;
  }
  unsigned blitCases=0,blitMismatchReturns=0;uint64_t blitInstructions=0;
  for(auto parameters:std::vector<std::array<unsigned,3>>{{42,0,0x40},{44,0,0},{44,0x200,0x8040},{44,0x100200,0xc040}}){
    auto c=std::make_unique<Case>(image);
    c->entry(0x80526540,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80701008);
    c->cpu.reg(UC_MIPS_REG_5,0);c->cpu.reg(UC_MIPS_REG_6,1);
    manta::check(c->cpu.run(0x80526540,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original blit enable setup failed");
    c->entry(0x805254ec,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80701000);
    c->cpu.reg(UC_MIPS_REG_5,9);c->cpu.reg(UC_MIPS_REG_6,7);c->cpu.reg(UC_MIPS_REG_7,42);
    c->write(0x0070ff10,0x03d6e128);c->write(0x0070ff14,14,2);c->write(0x0070ff18,0);
    manta::check(c->cpu.run(0x805254ec,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original blit source setup failed");
    c->write(0x00700080,0x00ddd800);c->write(0x00700084,1360);c->write(0x00700088,248);
    c->write(0x0070008c,2720);c->write(0x00700090,12);
    c->entry(0x8051fef8,0x803b0000);c->cpu.reg(UC_MIPS_REG_4,0x80700080);
    manta::check(c->cpu.run(0x8051fef8,50000));need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop,"original blit destination setup failed");
    for(unsigned i=0;i<2;++i){auto rect=0x00700080+8*i;
      c->write(rect,0,2);c->write(rect+2,0,2);c->write(rect+4,7,2);c->write(rect+6,i?parameters[0]:42,2);}
    c->writes.clear();c->entry(0x80523bfc,0x803b0000);
    c->cpu.reg(UC_MIPS_REG_4,0x80701000);c->cpu.reg(UC_MIPS_REG_5,0x80700080);
    c->cpu.reg(UC_MIPS_REG_6,0x80700088);c->cpu.reg(UC_MIPS_REG_7,parameters[1]);c->write(0x0070ff10,0);
    manta::check(c->cpu.run(0x80523bfc,50000));
    if(parameters[2]){
      need(c->cpu.reg(UC_MIPS_REG_PC)==0x80526598 && c->cpu.reg(UC_MIPS_REG_RA)==0x805242b0 &&
        c->boundary=="GE raster command 60h unsupported; no pixels or completion fabricated" &&
        c->writes.back()==std::pair<unsigned,unsigned>{0x60,parameters[2]},
        "original BitBlt must select its actual unscaled/scaled command bits without completing it");
    }else{
      need(c->cpu.reg(UC_MIPS_REG_PC)==c->stop && c->cpu.reg(UC_MIPS_REG_2)==9 && c->boundary.empty() &&
        std::none_of(c->writes.begin(),c->writes.end(),[](auto v){return v.first==0x60;}),
        "original unequal extents without scaling must return its own stretch error, not submit a raster");
      ++blitMismatchReturns;
    }
    need(c->riu.geRectangles==0 && c->rasterWrites==0,"guarded original BitBlt tests cannot create pixels");
    ++blitCases;blitInstructions+=c->instructions;
  }
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"isolated original clear/allocation instructions; synthetic RAM, conditional GE reset and explicit plain-fill model, not TV execution\","
    <<"\"cases\":"<<cases<<",\"raster_requests_rejected\":"<<requests<<",\"zero_size_returns\":"<<empty
    <<",\"original_instructions\":"<<instructions<<",\"raster_command\":48,\"legacy_guard_cases_pixels_generated\":false,\"pixels_generated\":true,\"framebuffer_allocated\":false"
    <<",\"original_format_capability_cases\":"<<capabilityCases<<",\"original_destination_binding_cases\":"<<destinationCases
    <<",\"original_destination_binding_instructions\":"<<destinationInstructions
    <<",\"isolated_original_GOP_allocations\":"<<allocationCases<<",\"original_GOP_rejections\":"<<allocationRejects
    <<",\"original_GOP_allocation_instructions\":"<<allocationInstructions
    <<",\"original_allocated_record_bindings\":"<<allocatedBindings
    <<",\"original_allocated_clear_requests_rejected\":"<<allocatedClearRequests
    <<",\"isolated_original_plain_fills\":"<<originalRasterCases<<",\"original_plain_fill_instructions\":"<<rasterInstructions
    <<",\"isolated_plain_fill_pixels\":24,\"connected_pixels_generated\":false"
    <<",\"original_BitBlt_mode_cases\":"<<blitCases<<",\"original_BitBlt_stretch_errors\":"<<blitMismatchReturns
    <<",\"original_BitBlt_mode_instructions\":"<<blitInstructions
    <<",\"connected_framebuffer_allocated\":false,\"GOP_window_enabled\":false}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
