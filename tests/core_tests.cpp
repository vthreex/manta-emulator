#include "board/htotal_join.hpp"
#include "video/ge_stretch_request.hpp"
#include "cpu/engine.hpp"
#include "cpu/mcu_value_mask.hpp"
#include "board/zui_flow.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "storage/sha256.hpp"
#include "board/unknown_observations.hpp"
#include "board/preserved_bits.hpp"
#include "board/audio_no_ack.hpp"
#include "board/pm_activity_scenario.hpp"
#include "board/virtual_time.hpp"
#include "board/mmio_read_mask.hpp"
#include "board/opaque_read_policy.hpp"
#include "video/gop.hpp"
#include <array>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static int checks = 0;
void require(bool v, const char *s) {
  ++checks;
  if (!v)
    throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
  bool did = false;
  try {
    f();
  } catch (const std::exception &) {
    did = true;
  }
  require(did, "operation must reject");
}
void cpuTests() {
  manta::Engine e;
  e.map(0, 0x2000);
  // BEQ taken executes delay slot; instruction after slot is skipped. Alias
  // read.
  const uint32_t code[] = {0x24080001, 0x11080002, 0x24090007, 0x24090063,
                           0x240a0009, 0xac090100, 0x3c0b8000, 0x8d6c0100};
  e.write(0, code, sizeof(code));
  require(e.run(0x80000000, 7) == UC_ERR_OK, "engine run");
  require(e.reg(UC_MIPS_REG_9) == 7, "taken delay slot");
  require(e.reg(UC_MIPS_REG_10) == 9, "branch target");
  require(e.reg(UC_MIPS_REG_12) == 7, "KSEG0 data alias");
  uint8_t b[4];
  e.read(0x100, b, 4);
  require(b[0] == 7 && b[1] == 0, "little endian store");
  // CP0 Status round trip executes actual mtc0/mfc0. Avoid enabling interrupts.
  const uint32_t cp0[] = {0x3c080040, 0x35080004, 0x40886000, 0, 0x40096000, 0};
  e.write(0x200, cp0, sizeof(cp0));
  require(e.run(0x80000200, 6) == UC_ERR_OK, "CP0 run");
  require(e.reg(UC_MIPS_REG_9) == 0x00400004, "CP0 writable BEV/ERL round trip (24Kc has no CU1)");
  // Branch likely not taken annuls its delay slot.
  const uint32_t likely[] = {0x24080001, 0x24090002, 0x51090001, 0x240a0063, 0x240b0005};
  e.write(0x300, likely, sizeof(likely));
  e.reg(UC_MIPS_REG_10, 0);
  require(e.run(0x80000300, 5) == UC_ERR_OK, "likely run");
  require(e.reg(UC_MIPS_REG_10) == 0, "annulled slot");
  require(e.reg(UC_MIPS_REG_11) == 5, "likely fallthrough");
  // Regression for a valid JR store delay slot returning directly to BEQ.
  // Generic Unicorn 2.1.4 memory hooks break this; production does not install
  // them.
  const uint32_t caller[] = {0x3c028000, 0x34420500, 0x0040f809, 0x24060040, 0x10400003,
                             0x00408021, 0x240a0005, 0,          0};
  const uint32_t callee[] = {0x24020001, 0x03e00008, 0xac020600};
  e.write(0x400, caller, sizeof(caller));
  e.write(0x500, callee, sizeof(callee));
  require(e.run(0x80000400, 12) == UC_ERR_OK, "JR/store then BEQ regression");
  require(e.reg(UC_MIPS_REG_16) == 1, "return branch delay slot");
  e.read(0x600, b, 4);
  require(b[0] == 1, "JR delay slot store occurred");
}
void openRiscTests(){
  manta::OpenRisc cpu;std::array<uint8_t,0x1000> memory{};
  cpu.read=[&](uint32_t a,unsigned n,bool){
    if(a+n>memory.size())throw std::runtime_error("test bus bounds");
    uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(memory[a+i])<<(i*8);
    return manta::OrWord{v,0xffffffffu,{}};
  };
  cpu.write=[&](uint32_t a,unsigned n,manta::OrWord v){
    if(a+n>memory.size() || v.known!=0xffffffffu)throw std::runtime_error("test store invalid");
    for(unsigned i=0;i<n;++i)memory[a+i]=uint8_t(v.value>>(i*8));
  };
  auto program=[&](uint32_t pc,std::initializer_list<uint32_t> code){
    for(auto w:code){for(unsigned i=0;i<4;++i)memory[pc+i]=uint8_t(w>>(i*8));pc+=4;}
  };
  cpu.reset(0x100);
  require(cpu.reg(0).known==0,"OR reset does not invent initial GPR values");
  cpu.reset(0x100,0x8001,0);
  require(cpu.reg(0).known==0xffffffffu && cpu.reg(0).value==0 && cpu.reg(1).known==0,
    "explicit platform reset-r0 hypothesis affects only the configured register");
  cpu.reset(0x100);
  require(cpu.reg(0).known==0,"subsequent default reset cannot retain the probe's configured initial r0");
  program(0x100,{0x18000000,0x9c200007,0x04000004,0x9c400009,0x9c400063,0x15000000,
    0x9c60000b,0xd4031800});
  cpu.step();cpu.step();cpu.step();
  require(cpu.pc()==0x10c && cpu.reg(9).value==0x110,"OR JAL links PC+8 and schedules delay slot");
  cpu.step();require(cpu.pc()==0x118 && cpu.reg(2).value==9,"OR branch delay slot executes before target");
  cpu.step();require(cpu.reg(3).value==11,"OR original target executes");
  // Writable r0 (OpenRISC), signed immediate and carry/overflow.
  program(0x200,{0x18000000,0x9c00ffff,0x18200000,0xa8210001,0xe0400800});cpu.reset(0x200);
  for(int i=0;i<5;++i)cpu.step();
  require(cpu.reg(0).value==0xffffffffu && cpu.reg(2).value==0 && (cpu.sr()&0x400),"OR writable r0 and arithmetic carry");
  program(0x240,{0x18000000,0x9c200340,0x9c40001f,0x48000800,0x9c600006});
  program(0x340,{0x15000000});cpu.reset(0x240);for(int i=0;i<5;++i)cpu.step();
  require(cpu.pc()==0x340 && cpu.reg(9).value==0x254 && cpu.reg(3).value==6,"OR JALR encoding 12h with delay slot");
  program(0x280,{0x18000000,0x9c200380,0x44000800,0x9c400003});cpu.reset(0x280);for(int i=0;i<4;++i)cpu.step();
  require(cpu.pc()==0x380 && cpu.reg(9).known==0,"OR JR encoding 11h does not write link register");
  program(0x300,{0x18000000,0x9c200800,0x18401234,0xa8425678,0xd4011000,0x84610000});
  cpu.reset(0x300);for(int i=0;i<6;++i)cpu.step();
  require(memory[0x800]==0x78 && memory[0x803]==0x12 && cpu.reg(3).value==0x12345678,"OR little-endian load/store shared bus");
  program(0x400,{0x18000000,0x9c200001,0x84410000});cpu.reset(0x400);for(int i=0;i<3;++i)cpu.step();
  require(cpu.pc()==0x600 && cpu.exceptions()==1,"OR alignment exception vector");
  program(0x600,{0x24000000});cpu.step();require(cpu.pc()==0x408,"OR RFE restores faulting PC");
  program(0x440,{0x9c210001});cpu.reset(0x440);rejects([&]{cpu.step();});
  program(0x480,{0xfc000000});cpu.reset(0x480);rejects([&]{cpu.step();});
  program(0x4c0,{0x18000000,0xb4200000});cpu.reset(0x4c0);cpu.step();rejects([&]{cpu.step();});
  // Preserve opaque bits only through logic; comparisons must reject them.
  cpu.read=[&](uint32_t a,unsigned n,bool){
    if(a==0x900)return manta::OrWord{0xa5a5a5a5,0,a};
    uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(memory.at(a+i))<<(i*8);
    return manta::OrWord{v,0xffffffffu,{}};
  };
  program(0x500,{0x18000000,0x9c200900,0x84410000,0xa8420001,0xbc020000});cpu.reset(0x500);
  for(int i=0;i<4;++i)cpu.step();
  require(cpu.reg(2).known==1 && cpu.reg(2).origin==0x900,"OR logic keeps unknown-bit origin and only forced bit becomes known");
  rejects([&]{cpu.step();});
  cpu.halt();auto count=cpu.instructions();cpu.step();require(cpu.instructions()==count,"OR halt dispatches no instructions");
}
void irDmaTests(){
  {
    manta::Riu fifo;fifo.event=[](auto&,auto,auto,auto,auto&){};fifo.symbolicInit=true;fifo.configurePmIrFifo();
    fifo.prepareMaskedRead(0x1f007b59,2);fifo.read(0x1f007b59,1,0,0,0);
    require(!fifo.stopReason.empty() && !fifo.pmIrFifo().empty().has_value(),"candidate PM FIFO supplies no unverified reset occupancy");fifo.stopReason.clear();
    fifo.write(0x1f007b45,1,15,0,0,1);
    require(!fifo.pmIrFifo().known(),"original depth/enable0F configuration is not a FIFO clear or an empty reset proof");
    rejects([&]{fifo.write(0x1f007b59,1,2,0,0,2);});
    fifo.write(0x1f007b45,1,0x8f,0,0,3);fifo.prepareMaskedRead(0x1f007b59,2);
    require((fifo.read(0x1f007b59,1,0,0,4)&2) && fifo.pmIrFifo().clears()==1 && fifo.knownBits(0x1f007b59,1)==2,
      "documented original FIFO_CLR command causally produces EMPTY only, with no key/repeat/IRQ invented");
    fifo.prepareMaskedRead(0x1f007b58,0x200);
    require((fifo.knownBits(0x1f007b58,2)&0x300)==0x200 && (fifo.read(0x1f007b58,2,0,0,4)&0x200),
      "PM IR byte and halfword aliases preserve little-endian EMPTY-field position without defining repeat or adjacent bits");
    rejects([&]{fifo.receivePmIrDecoded(0x5a,true);});
    fifo.write(0x1f007b00,1,0xbf,0,0,5);
    fifo.write(0x1f007b4d,1,0x38,0,0,5);
    require(fifo.receivePmIrDecoded(0x5a,true),"explicit synthetic decoded packet is accepted only by a configured receiver/FIFO");
    fifo.prepareMaskedRead(0x1f007b59,1);fifo.read(0x1f007b59,1,0,0,6);
    require(!fifo.stopReason.empty() && !(fifo.knownBits(0x1f007b59,1)&1),
      "external repeat metadata cannot invent whether RPT reports a consumed entry, new head or separate latch");fifo.stopReason.clear();
    fifo.prepareMaskedRead(0x1f007b59,2);
    require(!(fifo.read(0x1f007b59,1,0,0,6)&2) && fifo.read(0x1f007b51,1,0,0,6)==0x5a &&
      fifo.read(0x1f007b51,1,0,0,6)==0x5a && fifo.pmIrFifo().pops()==0 && fifo.stopReason.empty(),
      "PM EMPTY/key reflect an actual queued fixture; data reads do not consume packets");
    fifo.write(0x1f007b60,1,1,0,0,7);fifo.prepareMaskedRead(0x1f007b59,2);
    require((fifo.read(0x1f007b59,1,0,0,8)&2) && fifo.pmIrFifo().pops()==1 && !(fifo.knownBits(0x1f007b60,1)&1),
      "write-one read pulse pops exactly one packet; pulse readback is not assigned an invented reset value");
    fifo.read(0x1f007b51,1,0,0,8);require(!fifo.stopReason.empty(),"empty PM FIFO does not fabricate a key byte");fifo.stopReason.clear();
    for(unsigned i=0;i<8;++i)require(fifo.receivePmIrDecoded(uint8_t(i),false),"bounded PM FIFO accepts an external fixture within capacity");
    require(!fifo.receivePmIrDecoded(9,false) && fifo.pmIrFifo().received()==9,"FIFO overflow cannot fabricate a ninth retained packet or interrupt");
    fifo.preparePreservedWrite(0x1f007b60,1,1);fifo.write(0x1f007b60,1,0xa5,0,0,9);
    require(!fifo.pmIrFifo().known(),"unknown possible pop invalidates nonempty queue state instead of guessing occupancy");
    fifo.write(0x1f007b45,1,0x8f,0,0,10);
    require(fifo.pmIrFifo().empty()==true && fifo.pmIrFifo().clears()==2,"a subsequent actual clear can recover known empty state");
    manta::Engine cpu;cpu.map(0,0x1000);cpu.map(0x004f1000,0x1000);cpu.map(0x006b8000,0x1000);
    constexpr uint32_t original[]={0x3c07806c,0x00002821,0x00e04021,0x24060008,0x8ce48d0c,
      0x24a30001,0x306500ff,0x90827b59,0x30420002,0x1440000a,0};
    constexpr uint32_t tail[]={0x03e00008,0};
    cpu.write(0x004f1b80,original,sizeof(original));cpu.write(0x004f1bd0,tail,sizeof(tail));
    uint32_t pm=0xbf000000;cpu.write(0x006b8d0c,&pm,4);cpu.reg(UC_MIPS_REG_RA,0x80000100);
    struct FifoBus{manta::Riu* fifo;unsigned reads=0;} context{&fifo};
    auto read=+[](uc_engine*,uint64_t off,unsigned n,void* user)->uint64_t{
      auto& c=*static_cast<FifoBus*>(user);++c.reads;auto a=0x1f007000u+uint32_t(off);
      c.fifo->prepareMaskedRead(a,2);return c.fifo->read(a,n,0x804f1b9c,0,11);
    };
    manta::check(uc_mmio_map(cpu.handle,0x1f007000,0x1000,read,&context,nullptr,nullptr));
    require(cpu.run(0x804f1b80,13)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_PC)==0x80000100 && context.reads==1 && fifo.pmIrFifo().pops()==1,
      "original PM FIFO drain instructions return naturally on an actual empty queue without host keys, data reads, forced PC or success return");
  }
  manta::IrDma dma;std::array<uint8_t,96> ram{};std::array<uint8_t,48> record{};
  record.fill(0x35);auto store=[&](uint32_t a,std::span<const uint8_t> b){
    require(a>=0x1000 && a+b.size()<=0x1060,"IR capture addresses actual configured ring");
    std::copy(b.begin(),b.end(),ram.begin()+(a-0x1000));
  };
  require(!dma.producer(),"IR producer remains unknown before descriptor/reset experiment");
  require(!dma.capturePresent().has_value(),"IR capture activity is unknown before a valid original descriptor command");
  rejects([&]{dma.capture(record,store);});dma.configure(0x1000,2);
  require(dma.producer()==0 && dma.captures()==0,"IR empty cursor derives from configured capture state, not reads");
  require(dma.capturePresent()==false,"newly configured ring contains no externally captured records");
  dma.capture(record,store);require(dma.producer()==1 && ram[0]==0x35 && ram[47]==0x35,"IR captured record causes actual DMA bytes and cursor advance");
  record.fill(0x62);dma.capture(record,store);
  require(dma.producer()==0 && ram[48]==0x62 && dma.captures()==2,"IR capture ring wraps after actual second transfer");
  require(dma.capturePresent()==true,"wrapped index zero cannot erase actual capture history");
  dma.configure(0x1000,2);require(dma.capturePresent()==false && dma.captures()==2,"new descriptor resets current activity while retaining total real capture count");
  rejects([&]{dma.capture(std::span<const uint8_t>(record).first(47),store);});
  rejects([&]{dma.configure(3,2);});rejects([&]{dma.configure(0x10000000,2);});
  manta::Riu r;r.symbolicInit=true;r.event=[](auto&,auto,auto,auto,auto&){};
  r.configureExploratoryIrDmaReset();
  for(auto [a,v]:{std::pair{0x1f206e70u,0u},{0x1f206e71u,0x8eu},{0x1f206e74u,0x70u},
      {0x1f206e75u,0x68u},{0x1f206e78u,1u},{0x1f206e39u,0u}})r.write(a,1,v,0,255,1);
  require(!r.irDmaProducer(),"IR descriptor staging alone cannot produce a cursor");
  r.write(0x1f206e3d,1,0x82,0,255,2);
  require(r.irDmaStarts()==1 && r.irDmaAddress()==0x3847000 && r.irDmaCapacity()==360,
      "original IR address/length/MIU fields are decoded by actual guest command write");
  require(r.read(0x1f206e79,1,0,255,3)==0 && r.read(0x1f206e7c,1,0,255,4)==0 && r.irDmaCaptures()==0,
      "IR cursor is stable with no external records and no fake keys or completions");
  require((r.knownBits(0x1f206e41,1)&0x80)==0,"DMA-index experiment alone cannot authorize a new status-bit interpretation");
  r.configureExploratoryIrCapturePresent();r.prepareMaskedRead(0x1f206e41,0x80);
  require(!(r.read(0x1f206e41,1,0,0,5)&0x80) && r.stopReason.empty() && r.knownBits(0x1f206e40,2)==0x8000,
    "separate activity model supplies only bit7 from actual empty capture state, with correct word alias");
  r.read(0x1f206e41,1,0,0,5);require(!r.stopReason.empty(),"other IR status fields stay invalid");r.stopReason.clear();
  std::array<uint8_t,48> actual{};actual.fill(0x53);unsigned written=0;
  r.dramWrite=[&](uint32_t address,std::span<const uint8_t> bytes){
    require(address==0x3847000 && bytes.size()==48 && bytes[47]==0x53,"IR activity follows bytes committed to guest DMA memory");++written;
  };
  r.captureIrRecord(actual);r.prepareMaskedRead(0x1f206e41,0x80);
  require((r.read(0x1f206e41,1,0,0,6)&0x80) && written==1 && r.irDmaProducer()==1 && r.irDmaCaptures()==1,
    "external record commits before activity and producer become visible, no invented key decoding");
  r.write(0x1f206e3d,1,0x82,0,255,7);r.prepareMaskedRead(0x1f206e41,0x80);
  require(!(r.read(0x1f206e41,1,0,0,8)&0x80) && r.irDmaProducer()==0 && r.irDmaCaptures()==1,
    "original descriptor command resets current ring activity, not total capture evidence");
  manta::Engine original;original.map(0,0x20000);original.map(0x00527000,0x1000);original.map(0x006d8000,0x1000);
  constexpr uint32_t caller[]={0x27bdffe8,0xafbf0014,0x0c149fc3,0,0x14400005,0,0x24020001,0x8fbf0014,0x03e00008,0x27bd0018};
  constexpr uint32_t reader[]={0x3c02806e,0x8c4387f4,0x90626e41,0x03e00008,0x7c4201c0};
  original.write(0x005276b0,caller,sizeof(caller));original.write(0x00527f0c,reader,sizeof(reader));
  uint32_t base=0xbf200000;original.write(0x006d87f4,&base,4);
  struct IrBus{manta::Riu* r;unsigned reads=0;} context{&r};
  auto read=+[](uc_engine*,uint64_t off,unsigned n,void* user)->uint64_t{
    auto& b=*static_cast<IrBus*>(user);++b.reads;auto a=0x1f206000u+uint32_t(off);
    if(a!=0x1f206e41)throw std::runtime_error("empty IR path must not read a producer or capture record");
    b.r->prepareMaskedRead(a,0x80);return b.r->read(a,n,0x80527f14,0,9);
  };
  manta::check(uc_mmio_map(original.handle,0x1f206000,0x1000,read,&context,nullptr,nullptr));
  original.reg(UC_MIPS_REG_SP,0x80010000);original.reg(UC_MIPS_REG_RA,0x80000100);
  require(original.run(0x805276b0,15)==UC_ERR_OK && original.reg(UC_MIPS_REG_PC)==0x80000100 &&
    original.reg(UC_MIPS_REG_2)==1 && context.reads==1 && r.stopReason.empty(),
    "original IR HAL and ring predicate compute no-data themselves from stateful activity; no host-generated key, acknowledgment or producer read");
}
void gopResetTests(){
  manta::Riu r;r.symbolicInit=true;r.event=[](auto&,auto,auto,auto,auto&){};
  r.configureExploratoryGopResetWindows();r.configureExploratoryGopBank(0);
  require((r.gopByte(1,0).known&1)==0,"GWIN enable is not assigned a fabricated power-on state");
  r.write(0x1f203e00,2,1,0,1,1);
  for(unsigned w=0;w<4;++w)require((r.gopByte(1,w*0x80).known&1) && !(r.gopByte(1,w*0x80).value&1),"original top-bank reset pulse disables corresponding GWIN under explicit hypothesis");
  require(r.gopByte(1,0).known==1 && r.gopByte(1,4).known==0 && r.gopByte(4,0).known==0,
      "reset experiment defines only window-enable fields in its own plane");
  r.write(0x1f203e00,2,1,0,1,2);require(r.gopResetPulses()==1,"held reset does not invent repeated reset edges");
  r.write(0x1f203e00,2,0,0,1,3);r.write(0x1f203e00,2,1,0,1,4);
  require(r.gopResetPulses()==2,"new original assert after release creates a new reset pulse");
}
void flashTests() {
  std::vector<uint8_t> b(0x800000, 255);
  manta::Flash f(b);
  std::array<uint8_t, 2> data = {0xf0, 0x0f};
  rejects([&] { f.program(0, data); });
  f.writeEnable();
  f.program(0, data);
  require(f.read(0) == 0xf0 && f.stock()[0] == 255, "COW preserves stock");
  f.writeEnable();
  std::array<uint8_t, 1> ones = {255};
  rejects([&] { f.program(0, ones); });
  f.writeEnable();
  rejects([&] { f.program(255, data); });
  f.erase4k(0);
  require(f.read(0) == 255, "erase");
  f.protect(true);
  f.writeEnable();
  rejects([&] { f.program(0, data); });
  f.protect(false);
  f.writeEnable();
  f.program(4096, data);
  rejects([&] { f.read(0x800000); });
  auto path = std::filesystem::temp_directory_path() / "manta-test-overlay.cow";
  f.saveOverlay(path);
  manta::Flash g(b);
  g.loadOverlay(path);
  require(g.read(4096) == 0xf0, "overlay reload");
  require(g.read(0) == 255, "erased sector reload");
  std::filesystem::remove(path);
}
void bdmaCopyTests() {
  manta::Riu r;r.candidateBdma=true;
  std::vector<uint8_t> flash(0x100,0xff),ram(0x100,0xcc);
  flash[0x20]=0xff;flash[0x21]=0xd8;flash[0x22]=0x31;flash[0x23]=0x80;
  unsigned writes=0;
  r.event=[](const auto&,auto,auto,auto,const auto&){};
  r.flashRead=[&](uint32_t a,size_t n){return std::vector<uint8_t>(flash.begin()+a,flash.begin()+a+n);};
  r.dramWrite=[&](uint32_t a,std::span<const uint8_t> b){
    if(uint64_t(a)+b.size()>ram.size())throw std::runtime_error("fixture DRAM bounds");
    std::copy(b.begin(),b.end(),ram.begin()+a);++writes;
  };
  auto set=[&](uint32_t a,unsigned n,uint32_t v){r.write(a,n,v,0,0,0);};
  set(0x1f201208,2,0x3035);set(0x1f201210,2,0x20);set(0x1f201214,2,0);
  set(0x1f201218,2,0x40);set(0x1f20121c,2,0);set(0x1f201220,2,4);set(0x1f201224,2,0);
  set(0x1f201228,2,0x40);set(0x1f20122c,2,0);set(0x1f201230,2,0);set(0x1f201234,2,0);
  set(0x1f20120c,2,0x400);
  r.write(0x1f201200,1,1,0x80547350,0,100);
  require(r.stopReason.empty() && writes==0 && ram[0x40]==0xcc,"BDMA schedules actual copy, not immediate readiness");
  require(r.read(0x1f201204,1,0,255,163)==1 && writes==0,"BDMA remains busy before deadline");
  require(r.read(0x1f201204,1,0,255,164)==8 && writes==1,"BDMA DONE follows committed destination bytes");
  require(std::equal(flash.begin()+0x20,flash.begin()+0x24,ram.begin()+0x40) && ram[0x3f]==0xcc && ram[0x44]==0xcc,
          "BDMA copies exact original bytes and preserves adjacent memory");
  r.read(0x1f201204,1,0,255,200);require(writes==1,"BDMA completion is not repeated");
  set(0x1f201210,2,0xffff);set(0x1f201214,2,0x7f);
  r.write(0x1f201200,1,1,0,0,300);
  require(!r.stopReason.empty() && writes==1,"BDMA rejects chip-capacity overrun without fabricated completion");
}
void riuTests() {
  manta::Riu r;
  r.event = [](const auto &, auto, auto, auto, const auto &) {};
  r.read(0x1f001c74, 1, 0xbfc00c60, 1, 0);
  require(!r.stopReason.empty(), "strict unknown MMIO stops");
  r.stopReason.clear();
  r.symbolicInit = true;
  auto poison = r.read(0x1f001c74, 1, 0xbfc00c60, 1, 1);
  r.write(0x1f001c74, 1, poison & ~1u, 0xbfc00c7c, 0, 2);
  r.read(0x1f001c74, 1, 0x1234, 0, 3);
  require(!r.stopReason.empty(), "partial taint does not become known status");
  r.stopReason.clear();
  r.read(0x1f001c74, 1, 0xbfc00c60, 255, 4);
  r.write(0x1f001c74, 1, 0, 0xbfc00c7c, 0, 5);
  require(r.read(0x1f001c74, 1, 0x1234, 0, 6) == 0 && r.stopReason.empty(),
          "zero-valued full mask defines all bits");
  rejects([&] { r.read(0x1f001c74, 8, 0, 0, 0); });
  r.enableCandidateUart();
  r.write(0x1f201300, 1, 'X', 0, 0, 10);
  require(r.read(0x1f201328, 1, 0, 0, 11) == 0, "UART transmitting before deadline");
  require(r.read(0x1f201328, 1, 0, 0, 26) == 0x60, "UART emptiness follows TX completion");
  r.write(0x1f201318, 1, 0x80, 0, 0, 30);
  r.write(0x1f201300, 1, 0x42, 0, 0, 31);
  require(r.uart == "X", "DLAB divisor write is not a transmitted character");
}
void gpioTests() {
  manta::GpioI2c g;
  std::vector<std::string> events;
  g.event = [&](const auto &kind, auto, auto value, const auto &detail) {
    events.push_back(kind + ":" + std::to_string(value) + ":" + detail);
  };
  uint8_t known;
  g.input(0x1f203d05, known);
  require(known == 0, "uninitialized pin direction is not a known high input");
  g.write(0x1f203d0d, 6, 6);
  require(g.input(0x1f203d05, known) == 6 && known == 6,
          "released GPIO65/66 resolve through pull-ups");
  g.write(0x1f203d09, 0, 6);
  g.write(0x1f203d0d, 2, 6);
  require(g.input(0x1f203d05, known) == 2, "driven SDA low changes sampled input");
  g.write(0x1f203d0d, 6, 6);
  g.driveLow(0x1f203d05, 4, true);
  require(g.input(0x1f203d05, known) == 2, "external SDA low dominates released master");
  g.driveLow(0x1f203d05, 4, false);
  require(g.input(0x1f203d05, known) == 6, "external release restores pull-up level");
  g.write(0x1f203d09, 4, 6);
  g.write(0x1f203d0d, 2, 6);
  g.driveLow(0x1f203d05, 4, true);
  g.input(0x1f203d05, known);
  require(!(known & 4), "push-pull high against external low is unresolved contention");
  g.driveLow(0x1f203d05, 4, false);
  g.write(0x1f203d0d, 6, 6);
  g.write(0x1f203d09, 0, 6);
  rejects([&] { g.driveLow(0x1f203d05, 1, true); });
  // START, master clocks an address, then releases SDA for ACK. An absent
  // slave must NACK. Verify decoding depends on real pin transitions.
  events.clear();
  g.write(0x1f203d0d, 2, 6);
  for (int bit = 7; bit >= 0; --bit) {
    auto sda = uint8_t((0xa0 & (1 << bit)) ? 4 : 0);
    g.write(0x1f203d0d, sda, 6);
    g.write(0x1f203d0d, uint8_t(sda | 2), 6);
  }
  g.write(0x1f203d0d, 4, 6);
  g.write(0x1f203d0d, 6, 6);
  require(std::any_of(events.begin(), events.end(),
                      [](auto &s) { return s.starts_with("i2c_address:160:NACK"); }),
          "GPIO-clocked address decoded and absent slave NACK observed");
  g.write(0x1f203d19, 12, 12);
  require(g.input(0x1f203d11, known) == 12 && known == 12,
          "second bus has independently verified descriptor masks");

  manta::Riu r;
  r.event = [](const auto &, auto, auto, auto, const auto &) {};
  r.gpioI2c = true;
  r.write(0x1f203d0d, 1, 6, 0, 0, 0);
  require((r.read(0x1f203d05, 1, 0x8053bde4, 4, 1) & 4) && r.stopReason.empty(),
          "guest GPIO sample receives electrically defined SDA bit");
  r.read(0x1f203d05, 1, 0x8053bde4, 1, 2);
  require(!r.stopReason.empty(), "unrelated GPIO bit remains unsupported");
}
void miuTests() {
  manta::Riu r;
  r.event = [](const auto &, auto, auto, auto, const auto &) {};
  r.candidateMiu = true;
  r.resetMiuModel();
  require(r.knownBits(0x1f2025e0, 2) == 0x8000,
          "BIST reset defines only the inferred bank-select bit");
  r.write(0x1f202400, 4, 31, 0, 0, 0);
  require(!(r.read(0x1f202400, 4, 0, 0, 100) & 0x8000),
          "MIU ready requires complete guest command sequence");
  for (auto command : {0, 8, 12, 14, 31})
    r.write(0x1f202400, 4, command, 0, 0, 200);
  require(!(r.read(0x1f202400, 4, 0, 0, 201) & 0x8000), "MIU command completion is delayed");
  require(r.read(0x1f202400, 4, 0, 0, 264) & 0x8000, "MIU ready follows completed sequence");
  r.write(0x1f2025c4, 2, 0, 0, 0, 300);
  r.write(0x1f2025c8, 2, 16, 0, 0, 300);
  r.write(0x1f2025cc, 2, 0, 0, 0, 300);
  r.write(0x1f2025d0, 2, 0x5aa5, 0, 0, 300);
  r.write(0x1f2025e0, 2, 0, 0, 0, 300);
  bool visited = false;
  r.dramTest = [&](auto a, auto n, auto pattern) {
    visited = true;
    return a == 0 && n == 256 && pattern == 0x5aa5;
  };
  r.write(0x1f2025c0, 2, 3, 0, 0, 300);
  require(visited, "BIST invokes programmed guest DRAM comparison");
  require(!(r.read(0x1f2025c0, 2, 0, 0, 301) & 0xe000), "BIST remains busy before deadline");
  require((r.read(0x1f2025c0, 2, 0, 0, 364) & 0xe000) == 0x8000,
          "successful BIST completes without failure");
  r.dramTest = [](auto, auto, auto) { return false; };
  r.write(0x1f2025c0, 2, 3, 0, 0, 400);
  require((r.read(0x1f2025c0, 2, 0, 0, 464) & 0xe000) == 0xa000,
          "failed memory comparison cannot report BIST success");
  r.write(0x1f2025c8, 2, 0, 0, 0, 500);
  r.write(0x1f2025c0, 2, 3, 0, 0, 500);
  require(!r.stopReason.empty(), "invalid BIST bounds fail explicitly");
}
void unknownObservationTests() {
  manta::UnknownObservations u;
  std::array<uint32_t, 32> regs{};
  regs[2] = 0x806c0000;
  auto reg = [&](unsigned n) { return regs[n]; };
  u.seed(3);
  require(!u.observe(0x1234, 0x10600001, reg).empty(),
          "unknown observation cannot select a branch");
  require(!u.observe(0x1234, 0xa443914a, reg).empty(),
          "unknown observation cannot escape through an unverified store");
  require(u.observe(0x804698a8, 0xa443914a, reg).empty(),
          "verified original SH may cache an invalid observation");
  require(u.bytes().size() == 2 && u.bytes().at(0x6b914a) == 0x1f200a38,
          "invalid observation retains MMIO origin in both RAM bytes");
  require(u.observe(0x1234, 0x3c038000, reg).empty() && !u.registerUnknown(3),
          "known LUI overwrites tainted register");
  require(!u.observe(0x1234, 0x9443914a, reg).empty(),
          "cached invalid value stops before original LHU consumes it");
  require(u.observe(0x1234, 0xa440914a, reg).empty() && u.bytes().empty(),
          "known guest SH clears only overwritten unknown bytes");
  require(u.observe(0x1234, 0x9443914a, reg).empty(), "known overwritten RAM is readable again");
  u.seed(3);
  require(u.observe(0x1234, 0x0085180a, reg).empty() && !u.registerUnknown(3),
          "taken MOVZ replaces unknown destination");
  u.seed(3);
  regs[5] = 1;
  require(u.observe(0x1234, 0x0085180a, reg).empty() && u.registerUnknown(3),
          "untaken MOVZ must preserve invalid destination");
}
void rtcTests() {
  manta::Rtc r;
  require(!r.snapshot(false), "RTC snapshot is invalid before guest READ_EN");
  r.command(0, 0, 12000000, true, 0, false);
  r.command(3, 0, 12000000, true, 0, false);
  require(r.command(0x13, 312000000, 12000000, true, 0, false) == 3, "RTC read strobe self-clears");
  require(r.snapshot(false) == 1 && r.snapshot(true) == 0,
          "RTC counter advances from programmed divider and virtual oscillator");
  r.advance(624000000, 12000000);
  require(r.snapshot(false) == 1, "RTC low/high reads use one atomic snapshot");
  r.command(0x1b, 624000000, 12000000, true, 0xffff, true);
  require(r.snapshot(false) == 0xffff, "RTC LOAD_EN consumes original programmed load value");
  r.command(0x13, 936000000, 12000000, true, 0, false);
  require(r.snapshot(false) == 0 && r.snapshot(true) == 1,
          "RTC 32-bit carry reaches high counter half");
  r.command(1, 936000000, 12000000, true, 0, false);
  r.command(0x11, 1248000000, 12000000, true, 0, false);
  require(r.snapshot(true) == 1, "disabled RTC counter does not advance");
  manta::Rtc missing;
  missing.command(3, 0, 0, false, 0, false);
  require(!missing.error.empty(), "RTC cannot manufacture time without known nonzero divider");
  manta::Rtc badLoad;
  badLoad.command(0xb, 0, 12000000, true, 0, false);
  require(!badLoad.error.empty(), "RTC rejects unknown load input");
}
#ifdef UC_MIPS_MANTA_TIMER_API
static void timerHook(uc_engine *, uint64_t, uint32_t, void *p) {
  auto &e = *static_cast<manta::Engine *>(p);
  e.tickCount(1);
}
void timerTests() {
  manta::Engine e;
  e.map(0, 0x1000);
  e.reg(UC_MIPS_REG_CP0_COUNT, 0);
  e.reg(UC_MIPS_REG_CP0_COMPARE, 10);
  e.tickCount(9);
  require(e.reg(UC_MIPS_REG_CP0_COUNT) == 9 && !(e.reg(UC_MIPS_REG_CP0_CAUSE) & 0x8000),
          "CP0 timer is not pending before Count reaches Compare");
  e.tickCount(1);
  require((e.reg(UC_MIPS_REG_CP0_CAUSE) & 0x40008000) == 0x40008000,
          "CP0 equality latches TI and IP7");
  e.reg(UC_MIPS_REG_CP0_COMPARE, 100);
  require(!(e.reg(UC_MIPS_REG_CP0_CAUSE) & 0x40008000),
          "Compare write acknowledges timer pending latch");
  e.reg(UC_MIPS_REG_CP0_COUNT, 0xfffffff0);
  e.reg(UC_MIPS_REG_CP0_COMPARE, 2);
  e.tickCount(17);
  require(e.reg(UC_MIPS_REG_CP0_COUNT) == 1 && !(e.reg(UC_MIPS_REG_CP0_CAUSE) & 0x8000),
          "Count wraps without triggering a later Compare early");
  e.tickCount(1);
  require(e.reg(UC_MIPS_REG_CP0_COUNT) == 2 && (e.reg(UC_MIPS_REG_CP0_CAUSE) & 0x8000),
          "Compare crossing across Count wrap asserts timer IRQ");
  e.reg(UC_MIPS_REG_CP0_COMPARE, 100);
  e.reg(UC_MIPS_REG_CP0_COUNT, 10);
  uint32_t code[] = {0x40024800, 0x2443000c, 0x40835800, 0x34088001, 0x40886000,
                     0,          0x26100001, 0x1000fffe, 0};
  uint32_t handler[] = {0x401a4800, 0x275a0028, 0x409a5800, 0x26310001, 0x42000018, 0};
  e.write(0, code, sizeof(code));
  e.write(0x180, handler, sizeof(handler));
  uc_hook h;
  manta::check(
      uc_hook_add(e.handle, &h, UC_HOOK_CODE, reinterpret_cast<void *>(timerHook), &e, 1, 0));
  require(e.run(0x80000000, 200) == UC_ERR_OK,
          "timer IRQ executes guest exception handler and ERET without host PC changes");
  require(e.reg(UC_MIPS_REG_17) > 0 && e.reg(UC_MIPS_REG_16) > 0,
          "guest handler and interrupted loop both execute");
  require(!(e.reg(UC_MIPS_REG_CP0_STATUS) & 2), "ERET restores execution outside exception level");
}
#endif
void virtualTimeTests(){
  for(auto cpi:{1,2,4,8,16}){
    manta::VirtualTime t(cpi);uint64_t count=0;
    for(uint64_t i=1;i<=1001;++i){
      count+=t.countDelta(i);
      if(count!=t.cycles(i)/2)throw std::runtime_error("Count/peripheral cycle domains diverged");
    }
    require(count==t.cycles(1001)/2,"Count advances at half shared cycles for every dispatch and CPI profile");
    manta::UtmiCalibration u;u.clock(0xa);u.clock(0x28);u.command(true,0);u.command(false,4000000);
    const uint64_t deadline=4000064;
    require(!u.done(t.cycles((deadline-1)/cpi)) && u.done(t.cycles((deadline+cpi-1)/cpi)),
      "peripheral deadline remains cycle-driven across instruction-cost hypotheses");
#ifdef UC_MIPS_MANTA_TIMER_API
    manta::Engine e;e.reg(UC_MIPS_REG_CP0_COUNT,0);e.reg(UC_MIPS_REG_CP0_COMPARE,7);
    for(uint64_t i=1;t.cycles(i)/2<7;++i)e.tickCount(t.countDelta(i));
    require(!(e.reg(UC_MIPS_REG_CP0_CAUSE)&0x8000),"profile does not assert native timer before Compare");
    e.tickCount(t.countDelta((14+cpi-1)/cpi));
    require((e.reg(UC_MIPS_REG_CP0_CAUSE)&0x8000)!=0,"native timer detects Compare crossing with CPI increments");
#endif
  }
  bool rejected=false;try{manta::VirtualTime t(3);}catch(const std::runtime_error&){rejected=true;}
  require(rejected,"unsupported cycle factor is rejected");
  rejected=false;try{manta::VirtualTime(16).cycles(UINT64_MAX);}catch(const std::runtime_error&){rejected=true;}
  require(rejected,"cycle counter overflow cannot silently wrap peripheral time");
}
void utmiTests() {
  manta::UtmiCalibration u;
  u.command(true, 0);
  u.command(false, 3120000);
  require(!u.done(4000000), "UTMI completion requires guest clock sequence");
  u.clock(0x28);
  u.command(true, 0);
  u.command(false, 3120000);
  require(!u.done(4000000), "UTMI clock enable alone cannot replace reset/enable sequence");
  u.clock(0xa);
  u.clock(0x28);
  u.command(true, 0);
  u.command(false, 10);
  require(!u.done(4000000), "UTMI short start pulse does not complete calibration");
  u.command(true, 5000000);
  require(!u.done(9000000), "UTMI requires original guest clear of start bit");
  u.command(false, 9000000);
  require(!u.done(9000001) && u.done(9000064),
          "UTMI completion follows held start/release and virtual deadline");
  u.clock(0xa);
  require(!u.done(10000000), "UTMI clock reset cancels completed calibration");
}
void sharedMiuTests() {
  std::vector<uint8_t> backing(0x2000);
  manta::Engine e;
  e.map(0,0x1000);
  e.mapShared(0x08000000,backing.size(),backing.data());
  e.mapShared(0x40000000,backing.size(),backing.data());
  // Same PageMask/EntryHi/EntryLo pair installed by original 805CB7CC..7E0.
  uint32_t code[] = {0x3c081fff,0x3508e000,0x40882800,0x3c09c000,0x40895000,
    0x3c0a0100,0x354a0007,0x408a1000,0x354a0017,0x408a1800,0x40800000,
    0x000000c0,0x42000002,0x000000c0,0x3c08d000,0x35080100,0x3409abcd,
    0xad090000,0x3c08c000,0x35080100,0x8d100000,0x3c088800,0x35080100,0x8d110000};
  e.write(0,code,sizeof(code));
  e.reg(UC_MIPS_REG_CP0_STATUS,0x10000000);
  require(e.run(0x80000000,std::size(code))==UC_ERR_OK,"original TLB geometry accesses shared MIU1 bus mapping");
  require(e.reg(UC_MIPS_REG_16)==0xabcd && e.reg(UC_MIPS_REG_17)==0xabcd,
          "cached C0 and logical 8800 MIU1 aliases observe the uncached D0 guest store");
  uint32_t a=0,b=0; e.read(0x08000100,&a,4);e.read(0x40000100,&b,4);
  require(a==0xabcd && b==a,"host physical MIU aliases share backing bytes rather than independent allocations");
}
void spiTests() {
  manta::SpiIsp s;
  require(!s.read(0x5c, 1, 100), "SPI requires guest clock and engine enable");
  s.write(0x18, 0x40, 0); s.write(0, 0xaaaa, 100);
  require(s.read(0x5c, 1, 101) == 0 && s.read(0x5c, 1, 164) == 1,
          "SPI FIFO readiness follows enable and deterministic clock deadline");
  s.write(4, 5, 200); s.write(0x30, 1, 210);
  require(s.read(0x54, 1, 270) == 0 && s.read(0x54, 1, 328) == 1,
          "SPI RX waits for queued command clocks and receive clocks");
  require(!s.read(0x14, 255, 328), "persistent NOR protection bits stay unknown");
  require((s.read(0x14, 1, 328).value() & 1) == 0, "known idle BUSY bit consumed by original driver");
  s.write(0x20, 1, 400);
  require(!s.nor.selected() && !s.read(0x14, 255, 500), "CS rising flushes receive FIFO");
  s.write(0xa8, 0x3333, 400); s.write(0x10, 0x9f, 500);
  for (auto b : {0xef, 0x40, 0x17}) {
    s.write(0x30, 1, 600);
    require((s.read(0x14, 255, 664).value() & 255) == unsigned(b), "W25Q64BV documented JEDEC sequence");
  }
  s.write(0x20, 1, 700); s.write(0xa8, 0x3333, 700);
  for (auto [tick, byte] : std::array<std::pair<unsigned,unsigned>,4>{{{800,0xcf},{900,255},{1000,255},{1100,0}}})
    s.write(0x10, uint16_t(byte), tick);
  s.write(0x30, 1, 1200);
  require(s.error.find("undocumented SPI opcode 0xcf") == 0, "CF is not a Winbond ID or Macronix REMS2 response");
  manta::SpiIsp overrun; overrun.write(0x18,0x40,0); overrun.write(0,0xaaaa,0);
  overrun.write(0xa8,0x3333,100); overrun.write(0x10,0x9f,100); overrun.write(0x10,0,101);
  require(overrun.error == "SPI transmit FIFO overrun", "transmit FIFO rejects another byte before shift completion");
  manta::SpiIsp noClock; noClock.write(0,0xaaaa,0);
  require(!noClock.error.empty(), "ISP enable cannot replace divider configuration");
  auto packet = [](manta::SpiNor &n, std::initializer_list<uint8_t> bytes) {
    n.select(); for (auto b : bytes) n.transmit(b);
  };
  manta::SpiNor n;
  n.flashByte = [](uint32_t a) { return uint8_t(a ^ 0x5a); };
  packet(n,{0x90,255,255,0});
  require(n.receive().value == 0xef && n.receive().value == 0x16 && n.receive().value == 0xef,
          "90h two dummy bytes/address-zero returns repeating Winbond manufacturer/device");
  packet(n,{0x90,0,0,1});
  require(n.receive().value == 0x16 && n.receive().value == 0xef, "90h address one reverses ID order");
  packet(n,{0xab,0,0,0});
  require(n.receive().value == 0x16 && n.receive().value == 0x16, "ABh waits for three dummy bytes then repeats electronic ID");
  packet(n,{3,0x7f,0xff,0xff});
  require(n.receive().value == uint8_t(0x7fffff ^ 0x5a) && n.receive().value == 0x5a,
          "array address shifts MSB-first and wraps at configured NOR boundary");
  packet(n,{0x0b,0,0,1,255});
  require(n.receive().value == 0x5b, "fast read consumes its dummy byte before array output");
  packet(n,{6}); n.deselect(); packet(n,{5});
  require((n.receive().value & 3) == 2, "WREN latches only when complete command ends at CS rising");
  packet(n,{4}); n.deselect(); packet(n,{5});
  require((n.receive().value & 3) == 0, "WRDI clears WEL on CS rising");
  manta::SpiNor malformed; packet(malformed,{0x90,0,0}); malformed.receive();
  require(!malformed.error.empty(), "missing address/dummy phase cannot return a chip ID");
  manta::SpiNor mx; mx.model=manta::NorModel::MX25L6405D; packet(mx,{0x9f});
  require(mx.receive().value == 0xc2 && mx.receive().value == 0x20 && mx.receive().value == 0x17,
          "alternative Macronix chip has its own documented JEDEC bytes");
  packet(mx,{0xcf,255,255,0}); mx.receive();
  require(!mx.error.empty(), "MX25L6405D never inherits another Macronix part's CF response");
  manta::SpiNor unknown; unknown.unknownInput=manta::SpiUnknownInput::Deferred;
  packet(unknown,{0xcf,255,255,0});
  require(unknown.receive().known == 0 && unknown.undocumentedSamples == 1,
          "deferred CF sample stays invalid instead of claiming high impedance or an ID");
  unknown.unknownInput=manta::SpiUnknownInput::AssumedPullUp;
  require(unknown.receive().value == 255, "explicit hypothetical floating-input pull-up is separate from chip response");
  unknown.unknownInput=manta::SpiUnknownInput::AssumedPullDown;
  require(unknown.receive().value == 0, "explicit hypothetical pull-down is a separate experiment");
  manta::UnknownObservations guard;
  std::array<uint32_t,32> regs{}; regs[3]=0x807ffff0;
  auto reg=[&](unsigned i){return regs[i];};
  guard.seedSpiCache(regs[3]);
  require(guard.observe(0x8053c724,0xa0640000,reg).empty() && guard.bytes().at(0x7ffff0)==0x1f001014,
          "byte-verified original SPI SB retains unknown controller origin");
  require(!guard.observe(0x8053dd14,0x93a40010,[&](unsigned i){return i==29 ? 0x807fffe0 : regs[i];}).empty(),
          "unknown CF cache stops before original vendor check consumes it");
  require(!guard.observe(0x1000,0x10800001,reg).empty(), "invalid sample register stays invalid after caching");
  manta::SpiNor unselected; unselected.transmit(0x9f);
  require(!unselected.error.empty(), "flash rejects clocks without asserted CS");
}

void gopGuestBankTests() {
  manta::Engine e; e.map(0,0x2000);
  manta::Riu r; r.event=[](auto&,auto,auto,auto,auto&){};
  auto read = +[](uc_engine*,uint64_t off,unsigned n,void *p)->uint64_t {
    return static_cast<manta::Riu*>(p)->read(0x1f200000+uint32_t(off),n,0,0,0);
  };
  auto write = +[](uc_engine*,uint64_t off,unsigned n,uint64_t v,void *p) {
    static_cast<manta::Riu*>(p)->write(0x1f200000+uint32_t(off),n,uint32_t(v),0,0,0);
  };
  manta::check(uc_mmio_map(e.handle,0x1f200000,0x10000,read,&r,write,&r));
  const uint32_t code[] = {0x3c08bf20,0x35083e00,0x24090001,0xa50901fc,0x240a1234,0xa50a0004,
    0x24090004,0xa50901fc,0x240a7777,0xa50a0004,0x24090001,0xa50901fc,0x950b0004};
  e.write(0,code,sizeof(code));
  require(e.run(0x80000000,13)==UC_ERR_OK && e.reg(UC_MIPS_REG_11)==0x1234,
          "real guest MIPS MMIO writes switch GOP banks and read back original framebuffer base");
  require(r.gopByte(1,4).value==0x34 && r.gopByte(4,4).value==0x77,
          "guest MMIO bus retains separate GOP bank contents");
}
void gopMiuAddressTests(){
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};
  require(!manta::gopPhysicalAddress(r,1,0,0xddd800),"unknown MIU-select field cannot resolve a framebuffer address");
  constexpr uint32_t original[]={0x2e020004,0x10400010,0x2602fffc,0x97a20010,0x30420020,0x10400014,0x8fbf0024,
    0x8e220000,0x3c030800,0x24040001,0x00431025,0xae220000,0x8fbf0024,0x00801021,0x8fb10020,
    0x8fb0001c,0x03e00008,0x27bd0028,0x304200ff,0x2c420002,0x1040fff7,0x00002021,0x97a20010,
    0x30420040,0x1440ffee,0x8fbf0024,0x24040001,0x00801021,0x8fb10020,0x8fb0001c,0x03e00008,0x27bd0028};
  manta::Engine cpu;cpu.map(0,0x2000);cpu.map(0x0053a000,0x1000);cpu.write(0x0053ab74,original,sizeof(original));
  for(unsigned select:{0u,0x20u,0x40u,0x60u})for(unsigned id=0;id<6;++id){
    r.write(0x1f2025e4,2,select,0,0,0);
    uint32_t relative=0xddd800,ret=0x80000100;
    cpu.write(0x200,&relative,4);cpu.write(0x1010,&select,4);cpu.write(0x1024,&ret,4);
    cpu.reg(UC_MIPS_REG_SP,0x80001000);cpu.reg(UC_MIPS_REG_16,id);cpu.reg(UC_MIPS_REG_17,0x80000200);
    require(uc_emu_start(cpu.handle,0x8053ab74,ret,0,100)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_PC)==ret,
      "original GOP physical-address fragment returns through its own branches and JR");
    uint32_t physical=0;cpu.read(0x200,&physical,4);
    require(manta::gopPhysicalAddress(r,id<4?1:4,id<4?id:id-4,relative)==physical && cpu.reg(UC_MIPS_REG_2)==1,
      "MIU decoder matches actual original instructions for all six GWIN IDs and both MIU-select fields");
  }
  require(!manta::gopPhysicalAddress(r,1,4,0) && !manta::gopPhysicalAddress(r,4,2,0) &&
    !manta::gopPhysicalAddress(r,0,0,0) && !manta::gopPhysicalAddress(r,1,0,0x08000000),
    "unsupported window IDs and pre-encoded physical addresses cannot alias another MIU");
  r.symbolicInit=true;r.preparePreservedWrite(0x1f2025e4,2,0x40);r.write(0x1f2025e4,2,0x60,0,0,0);
  require(manta::gopPhysicalAddress(r,1,0,0)==0x08000000 && !manta::gopPhysicalAddress(r,4,0,0),
    "unknown auxiliary MIU field remains invalid without hiding independently known main MIU selection");
}
void displayHardwareTests() {
  manta::Riu r; r.event = [](auto&,auto,auto,auto,auto&){};
  require(!r.gopBankKnown(), "GOP reset selector remains unknown");
  rejects([&]{r.write(0x1f203e04,2,123,0,0,0);});
  r.write(0x1f203ffc,2,1,0,0,0);
  require((r.knownBits(0x1f203ffc,2)&0xf0f0)==0, "GOP bank writes cannot fabricate interrupt or hardware write-ack flags");
  auto w = [&](unsigned index, uint16_t value) { r.write(0x1f203e00+index*4,2,value,0,0,0); };
  w(0,0x51); w(1,0x1234); w(2,1); w(9,960); w(4,0); w(5,960); w(6,0); w(7,1080);
  require(!manta::readGopWindow(r,1,0),"neighbor register1Ch cannot substitute for the original vertical-end lane20h");
  w(8,1080);
  auto descriptor = manta::readGopWindow(r,1,0);
  require(descriptor && descriptor->miuRelativeAddress==0x891a0 && descriptor->pitch==7680 &&
          descriptor->width==1920 && descriptor->height==1080 && descriptor->bpp==32,
          "original GOP register geometry decodes eight-byte address/pitch units and 64-bit horizontal units");
  r.write(0x1f203ffc,2,4,0,0,0); w(1,0x7777);
  require(r.gopByte(1,4).value==0x34 && r.gopByte(4,4).value==0x77,
          "GOP register banks retain different framebuffer bases");
  require(!manta::readGopWindow(r,4,0), "incomplete GOP descriptor cannot produce a guest frame");
  r.write(0x1f203ffc,2,1,0,0,0); w(9,1);
  require(!manta::readGopWindow(r,1,0), "GOP pitch cannot address outside a configured scanline");
  r.write(0x1f203ffc,2,4,0,0,0);
  // Actual748M guest writes, not a fabricated display fixture.
  w(0,0x8064);w(1,0xbb00);w(2,0x1b);w(9,2);w(4,0);w(5,2);w(6,0);w(7,0);w(8,8);
  auto disabled=manta::readGopWindow(r,4,0,false,false);
  require(disabled && !disabled->enabled && disabled->miuRelativeAddress==0xddd800 &&
          disabled->pitch==16 && disabled->width==8 && disabled->height==8 && disabled->format==6,
          "original small guest buffer is distinguishable from enabled scan-out");
  require(!manta::readGopWindow(r,4,0),"a configured disabled buffer cannot authorize an authentic frame");
  w(2,0xe000);require(!manta::readGopWindow(r,4,0,false,false),"GOP address units cannot overflow into a different guest memory address");w(2,0x1b);
  r.symbolicInit=true;r.preparePreservedWrite(0x1f203e00,2,1);w(0,0x8064);
  require(!manta::readGopWindow(r,4,0,false,false),"unknown enable state cannot be mislabeled as a disabled configuration");
  manta::Riu sc; sc.event = r.event;
  sc.saveTimingSnapshot(); sc.write(0x1f205e00,1,0x10,0,0,0); sc.write(0x1f205e30,2,0x1111,0,0,0);
  sc.write(0x1f205e00,1,0x24,0,0,0); sc.write(0x1f205e30,2,0x2222,0,0,0);
  sc.write(0x1f205e00,1,0x10,0,0,0);
  require(sc.read(0x1f205e30,2,0,0,0)==0x1111, "SC bank switching preserves independent timing registers");
  require(sc.restoreTimingSnapshot() && sc.knownBits(0x1f205e00,1)==0,
          "opaque SC selector restore retains unknown original state");
  rejects([&]{sc.read(0x1f205e30,2,0,0,0);});
  manta::UnknownObservations guard;
  std::array<uint32_t,32> regs{}; regs[3]=0xbf200000;
  auto reg=[&](unsigned i){return regs[i];};
  guard.seedTimingSnapshot(10);
  require(guard.observe(0x805e5bfc,0x314a00ff,reg).empty() && guard.timingSnapshotUnknown(10),
          "exact original byte mask cannot legitimize opaque selector bits");
  require(guard.observe(0x805e5d44,0xa06a5e00,reg).empty(), "only original selector restore consumes its opaque snapshot");
  require(!guard.observe(0x805e5d44,0xa06a5e04,reg).empty(), "opaque selector cannot become a device command");
  manta::UnknownObservations wrongOrigin; wrongOrigin.seed(10);
  require(!wrongOrigin.observe(0x805e5d44,0xa06a5e00,reg).empty(), "POR or other unknown origins cannot use selector restore permission");
  manta::UnknownObservations clocks;
  clocks.seedClockSnapshot(8,0x1f201751);
  require(clocks.observe(0x805fd444,0x311100ff,reg).empty() && clocks.registerUnknown(17),
          "original clock identity copy preserves invalid origin");
  regs[29]=0x80700000;
  require(clocks.observe(0x803b0200,0xafa80020,reg).empty() && clocks.bytes().at(0x700020)==0x1f201751,
          "interrupt save frame may transparently carry opaque clock snapshot");
  require(clocks.observe(0x803b0204,0x8fa80020,reg).empty() && clocks.registerUnknown(8),
          "interrupt reload keeps opaque clock invalid");
  require(!clocks.observe(0x805fd4bc,0x3225003c,reg).empty(), "unknown saved clock selector cannot drive a different mode's clock change");
  require(!clocks.observe(0x1000,0x11000001,reg).empty(), "transparent clock copies cannot authorize a branch");
  manta::GammaRam gamma;
  gamma.write(0x40,511,0xabc,100);
  require(gamma.status(163)==0x40 && !gamma.written[1][511], "gamma RAM remains busy before transfer completion");
  rejects([&]{gamma.write(0x20,5,1,160);});
  require(gamma.status(164)==0 && gamma.data[1][511]==0xabc && gamma.written[1][511] && gamma.commits==1,
          "gamma channel strobe commits actual addressed twelve-bit sample");
  rejects([&]{gamma.write(0x60,5,1,200);}); rejects([&]{gamma.write(0x20,1024,1,200);});
  manta::Riu g; g.event=r.event; g.symbolicInit=true; g.candidateGamma=true;
  g.write(0x1f205e00,1,0x10,0,0,0);
  g.prepareMaskedRead(0x1f205fb4,0xe0);
  require((g.read(0x1f205fb4,1,0,0,0)&0xe0)==0, "gamma status exposes only causally idle channel bits");
  g.write(0x1f205fb0,2,1,0,0,0); g.write(0x1f205fb8,2,0x321,0,0,0);
  g.prepareRmw(0x1f205fb4,0x20); g.write(0x1f205fb4,1,0x25,0,0,100);
  g.prepareMaskedRead(0x1f205fb4,0xe0);
  require((g.read(0x1f205fb4,1,0,0,120)&0xe0)==0x20, "RIU gamma transaction drives original firmware busy poll");
  g.prepareMaskedRead(0x1f205fb4,0xe0); g.read(0x1f205fb4,1,0,0,164);
  require(g.gammaCommits()==1 && g.knownBits(0x1f205fb4,1)==0xe0,
          "gamma completion leaves unrelated control bits unknown");
}
void gopUpdateTests() {
  manta::GopUpdate u;
  require(u.known()==0 && !u.advance(10),"GOP ACK reset remains unknown without a transaction");
  u.write(0,0xffff,10);
  require(u.known()==0,"software zero alone cannot manufacture GOP ACK readiness");
  u.write(0x100,0xffff,10);u.write(0,0xffff,10);
  require(u.pending() && u.busy()==0xf000 && u.requests==1,"complete original GOP pulse requests a pending latch");
  require(!u.advance(10) && !u.advance({}),"repeated reads and absent clock cannot complete a GOP update");
  require(u.advance(11) && u.busy()==0 && u.latches==1,"next sync edge completes the actual pending latch");
  u.write(0x200,0xffff,{});
  require(u.write(0,0xffff,{}) && u.forced==1 && u.latches==2,"complete original force pulse latches independently of sync");
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};
  r.exploratoryGopVsyncLatch=true;r.configureExploratoryDisplayClock(148500000);
  r.configureExploratoryScBank(0x10);r.write(0x1f20174c,1,0x1c,0,0,0);
  auto panel=[&](unsigned off,unsigned value){r.write(0x1f205e00+off,2,value,0,0,0);};
  panel(0x30,2199);panel(0x34,1124);panel(0x10,112);panel(0x14,2031);
  panel(0x18,0);panel(0x1c,1079);panel(8,1101);panel(12,1109);panel(0x40,0);
  r.write(0x1f203ffc,2,1,0,0,0);
  auto word=[&](unsigned index,uint16_t value,uint64_t tick=0){r.write(0x1f203e00+index*4,2,value,0,0,tick);};
  word(0,0x51);word(1,0x1234);word(2,1);word(9,960);word(4,0);word(5,960);word(6,0);word(8,1080);
  require(!manta::readGopWindow(r,1,0,true),"unlatched guest shadow descriptor cannot authorize scan-out");
  r.write(0x1f203ffc,2,0x101,0,0,0);r.write(0x1f203ffc,2,1,0,0,0);
  require(r.gopUpdate().pending() && r.gopActiveBytes()==0,"RIU strobe remains busy before a programmed sync edge");
  r.prepareMaskedRead(0x1f203ffc,0x2000);r.read(0x1f203ffc,2,0,0,1);
  require(r.gopUpdate().pending(),"same-frame RIU polling cannot toggle ACK");
  auto edge=(uint64_t(1101)*2200*312000000+148499999)/148500000;
  r.prepareMaskedRead(0x1f203ffc,0x2000);r.read(0x1f203ffc,2,0,0,edge);
  auto active=manta::readGopWindow(r,1,0,true);
  require(active && active->miuRelativeAddress==0x891a0 && r.gopUpdate().latches==1,
          "programmed sync latches actual guest framebuffer address and dimensions");
  word(1,0x7777,edge+1);
  require(manta::readGopWindow(r,1,0,true)->miuRelativeAddress==0x891a0,"later shadow writes cannot change active scan-out before another latch");
  r.write(0x1f203ffc,2,0x201,0,0,edge+2);r.write(0x1f203ffc,2,1,0,0,edge+3);
  require(manta::readGopWindow(r,1,0,true)->miuRelativeAddress==0xbbbb8,"force latch transfers updated guest shadow bytes");
  require((r.knownBits(0x1f203ffc,2)&0x00f0)==0,"GOP latch never validates unrelated interrupt fields");
  r.configureExploratoryGopResetWindows();r.write(0x1f203ffc,2,0,0,0,edge+4);
  r.write(0x1f203e00,2,1,0,0,edge+5);
  require(!manta::readGopWindow(r,1,0,true) && (r.gopActiveByte(1,0).known&1),
          "asserted GOP plane reset disables already-latched active windows without erasing framebuffer metadata");
  require(r.gopActiveByte(1,4).value==0x77,"reset cannot fabricate replacement framebuffer addresses");
}
void scalerTimingTests() {
  manta::ScalerTiming s;
  manta::PanelTiming t{2200,1125,112,2031,0,1079,1101,1109,false};
  require(!s.outputVsync(0),"strict timing has no assumed clock/status");
  rejects([&]{s.clock(1);});
  s.clock(148500000);
  require(s.program(t,true,100),"programmed panel timing arms experiment");
  require(s.outputVsync(100)==false,"sync initially outside guest pulse interval");
  auto ticks=[&](uint64_t line){return 100+(line*2200*312000000+148499999)/148500000;};
  require(s.outputVsync(ticks(1101))==true,"sync enters original guest pulse interval");
  require(s.outputVsync(ticks(1109))==true,"sync interval inclusive end");
  require(s.outputVsync(ticks(1110))==false,"sync exits on next scanline");
  require(s.outputVsync(ticks(1101))==true,"same tick returns same level, no read toggling");
  require(!s.program(t,true,ticks(1110)),"unchanged guest state does not reset phase");
  require(s.frames(ticks(2250))==2,"frames count virtual pixels rather than reads");
  s.program(t,false,ticks(2250));
  require(!s.outputVsync(ticks(2251)),"clock gate stops output with unknown monitor level");
  t.vsyncEnd=1125;
  require(!s.program(t,true,0),"out-of-bounds pulse is rejected");
  require(!s.configured() && !s.outputVsync(100),"invalid geometry cannot leave stale output running");
  manta::Riu r; r.event=[](auto&,auto,auto,auto,auto&){};
  r.configureExploratoryDisplayClock(148500000);
  r.configureExploratoryScBank(0x10);
  r.write(0x1f20174c,1,0x1c,0,0,0);
  auto put=[&](unsigned off,unsigned val){r.write(0x1f205e00+off,2,val,0,0,0);};
  put(0x30,2199); put(0x34,1124); put(0x10,112); put(0x14,2031);
  put(0x18,0); put(0x1c,1079); put(8,1101); put(12,1109); put(0x40,0);
  require(r.panelTiming() && *r.panelTiming()==manta::PanelTiming{2200,1125,112,2031,0,1079,1101,1109,false},"RIU geometry decoded from bank 10 only");
  r.configureExploratoryScBank(1);
  r.prepareMaskedRead(0x1f205e78,1);
  auto pulseTick=ticks(1101)-100;
  require((r.read(0x1f205e78,1,0,0,pulseTick)&1)==1 && r.stopReason.empty(),"RIU original status bit follows configured timing");
  r.configureExploratoryScBank(3);r.prepareMaskedRead(0x1f205e78,1);
  require((r.read(0x1f205e78,1,0,0,pulseTick)&1)==1 && r.stopReason.empty(),"bank-3 output monitor uses explicit shared-output-clock experiment");
  r.read(0x1f205e78,1,0,0,pulseTick);
  require(!r.stopReason.empty(),"unknown adjacent status bits cannot be consumed");
  require(r.knownBits(0x1f205e78,1)==0,"experimental monitor does not validate stored register reset bits");
  r.stopReason.clear();r.prepareMaskedRead(0x1f205e78,0x100);
  r.read(0x1f205e78,2,0,0,pulseTick);
  require(!r.stopReason.empty(),"output VSYNC bit0 cannot manufacture the distinct unknown timing-word bit8");
}
void preservedBitsTests() {
  for(auto field:{0x80u,1u}){
    auto address=field==0x80?0x1f225b4cu:0x1f225a5cu;
    manta::PreservedBits saved;saved.seed(7,1,255u&~field,[&](unsigned){return address;});
    std::array<uint32_t,32> regs{};regs[7]=0xa5;regs[4]=0xa0000000u|address;
    auto step=[&](uint32_t w){return saved.observe(w,[&](unsigned r){return regs[r];},[](auto a){return a;},[](auto,auto,auto){});};
    require(step(0x30f60000|field).empty() && saved.mask(22)==field,"original SIF snapshot retains only its actual unknown control field");
    regs[22]=regs[7]&field;
    require(step(0x02c03021).empty() && saved.mask(6)==field,"SIF restore argument keeps the original field invalid");
    regs[6]=regs[22];require(step(0xa0860000).empty() && saved.mask(6)==field,"original SIF field may restore only to the same byte and bit");
    regs[4]^=4;require(!step(0xa0860000).empty(),"saved SIF control cannot become a different register response");
  }
  bool equalityProofsValid=true;
  for(auto unknown:{1u,0x20u,0x80u,0xa1u,255u}){
    manta::PreservedBits proof;proof.seed(4,1,255u&~unknown,[](unsigned){return 0x1f225dd8u;});
    for(unsigned sample=0;sample<256;++sample)for(unsigned target=0;target<256;++target){
      auto regs=[&](unsigned r){return r==4?sample:r==2?target:0u;};
      bool permitted=proof.observe(0x10820001,regs,[](auto a){return a;},[](auto,auto,auto){}).empty();
      bool fixedInequality=((sample^target)&~unknown)!=0;
      if(permitted!=fixedInequality || proof.mask(4)!=unknown)equalityProofsValid=false;
    }
  }
  require(equalityProofsValid,"all byte values and five unknown masks preserve equality uncertainty unless a known unequal bit proves the outcome");
  for(auto value:{1u,0x21u})for(auto op:{4u,5u,20u,21u}){
    manta::PreservedBits partial;partial.seed(4,1,0xdf,[](unsigned){return 0x1f225dd8u;});
    auto regs=[&](unsigned r){return r==4?value:r==2?0x10u:0u;};
    require(partial.observe((op<<26)|(4u<<21)|(2u<<16)|1u,regs,[](auto a){return a;},[](auto,auto,auto){}).empty(),
      "known unequal bit determines original equality branch despite an unrelated unknown mode bit");
    require(partial.mask(4)==0x20,"determinate branch cannot validate the underlying unknown mode bit");
  }
  {
    manta::PreservedBits partial;partial.seed(4,1,0xdf,[](unsigned){return 0x1f225dd8u;});
    auto regs=[](unsigned r){return r==4?0x20u:0u;};
    require(!partial.observe(0x10820001,regs,[](auto a){return a;},[](auto,auto,auto){}).empty(),
      "representative inequality solely in an unknown bit cannot authorize a branch");
    partial.seed(2,1,0xdf,[](unsigned){return 0x1f225dd8u;});
    require(!partial.observe(0x10820001,regs,[](auto a){return a;},[](auto,auto,auto){}).empty(),
      "matching origins alone cannot decide equality without a proven known unequal bit");
  }
  manta::PreservedBits p;
  std::array<uint32_t,32> regs{};
  auto reg=[&](unsigned i){return regs[i];};
  unsigned bank=2, writes=0; uint32_t savedMask=0;
  auto key=[&](uint32_t a){return 0x81000000u+(bank<<10)+a-0x1f205e00;};
  auto store=[&](uint32_t,unsigned,uint32_t m){++writes;savedMask=m;};
  auto step=[&](uint32_t w){return p.observe(w,reg,key,store);};
  p.seed(2,2,0,[&](unsigned b){return key(0x1f205e14+b);});
  require(p.mask(2)==65535,"unknown register bits seeded without choosing reset value");
  regs[2]=0xa5a5;
  require(step(0x00021400).empty(),"unknown halfword can shift into high half");
  regs[2]<<=16; regs[18]=0x40000000;
  require(step(0x00521024).empty() && p.mask(2)==0x40000000,"known AND removes all but preserved bit");
  regs[2]&=regs[18];
  require(step(0x00021402).empty() && p.mask(2)==0x4000,"right shift retains exact originating bit");
  regs[2]>>=16; regs[4]=0xbf205e14;
  require(step(0xa4820000).empty() && writes==1 && savedMask==0x4000,"same banked byte/bit accepts unchanged unknown bit");
  bank=3;
  require(!step(0xa4820000).empty() && writes==1,"different bank cannot receive unknown bit");
  bank=2;
  require(!step(0x10400001).empty(),"branch on unknown control bit rejects");
  regs[4]=0x80701000;
  require(step(0xac820000).empty(),"interrupt frame can preserve bit provenance");
  require(step(0x8c890000).empty() && p.mask(9)==0x4000,"RAM reload retains invalid originating bit");
  regs[10]=0x4000;
  require(!step(0x012a1021).empty(),"unknown bit cannot enter arithmetic with a possible carry");
  require(step(0x30420000).empty() && p.mask(2)==0,"destructive mask can discard unknown bits entirely");
  regs[9]=0x4000;
  require(step(0x35294000).empty() && p.mask(9)==0,"ORI one defines only its forced bit");
  p.seed(3,1,0,[&](unsigned b){return key(0x1f205e1d+b);});regs[3]=0xa5;
  require(step(0x00031a00).empty(),"DLC statistic high byte retains bit origins when shifted");regs[3]<<=8;
  p.seed(4,1,0,[&](unsigned b){return key(0x1f205e1c+b);});regs[4]=0xa5;
  require(step(0x00641821).empty() && p.mask(3)==65535,"disjoint byte concatenation remains wholly invalid");
  p.seed(4,2,0,[&](unsigned b){return key(0x1f205e1c+b);});
  require(!step(0x00641821).empty(),"possible carry cannot be used to concatenate invalid observations");
  p.seed(2,2,0,[&](unsigned b){return key(0x1f205e50+b);});regs[2]=0xa5a5;
  require(step(0x7c423a00).empty() && p.mask(2)==255,"original EXT keeps gain snapshot unknown");
  step(0x3c044000);regs[4]=0x40001000;regs[2]=0xa5;
  require(step(0xac820000).empty(),"opaque sample can be saved through documented MIU1 alias");
  require(p.memoryUnknown(0x40001000) && p.memoryUnknown(0x88001000) && p.memoryUnknown(0x08001000),"unknown-byte queries canonicalize all documented MIU1 aliases");
  require(manta::PreservedBits::backingAddress(0x40001000)==0x08001000,"RMW lookahead must not read MIU0 for raw MIU1 bus alias");
  regs[4]=0x1000;
  require(step(0xac800000).empty(),"MIU0 overwrite does not touch independent MIU1 cache");
  regs[4]=0x88001000;
  require(step(0x8c850000).empty() && p.mask(5)==255,"MIU1 cached alias preserves origin despite MIU0 overwrite");
  p.seed(2,1,15,[&](unsigned b){return key(0x1f203ffc+b);});regs[31]=0x803c1f60;
  require(step(0x03e00008).empty() && p.mask(2)==0xf0,"known leaf return cannot discard unknown GOP IRQ flags");
  require(step(0x304200ff).empty() && p.mask(2)==0xf0,"original byte-reader delay slot retains status provenance");
  require(step(0x3042000f).empty() && p.mask(2)==0,"original caller masks IRQ flags before comparing a known selector");
  regs[2]&=15;regs[17]=1; // metadata observer does not execute ANDI; use actual masked values and a valid bank
  require(step(0x1051ffd8).empty(),"known selector comparison is allowed after actual guest mask");
  p.seed(2,1,0,[&](unsigned b){return key(0x1f203ffc+b);});step(0x3042000f);regs[2]&=15;
  require(!step(0x1051ffd8).empty(),"the same original comparison rejects an unknown power-on GOP bank");
  step(0x24050000); // known cached context field, independent of prior MIU fixture
  p.seed(4,2,0,[&](unsigned b){return key(0x1f205000+b);});regs[6]=1;regs[5]=0;
  require(step(0x00c42024).empty() && p.mask(4)==1,"original GE context mask preserves exactly unknown enable bit zero");
  require(step(0x00a42025).empty() && p.mask(4)==1,"known cached GE fields do not validate the hardware enable bit");
  require(step(0x3082ffff).empty() && p.mask(2)==1,"GE_ReadReg return retains invalid enable provenance");
}
void dlcStatusTests() {
  manta::Riu r; r.event=[](auto&,auto,auto,auto,auto&){};
  r.configureExploratoryScBank(0x1a);
  require(r.knownBits(0x1f205e10,1)==0,"DLC has no invented power-on request or completion");
  r.configureExploratoryDlcPending(true);
  require(r.knownBits(0x1f205e10,1)==4,"DLC pending experiment defines exactly the requested bit");
  r.prepareMaskedRead(0x1f205e10,4);
  require((r.read(0x1f205e10,1,0,0,0)&4)!=0,"pending experiment is guest-consumable as declared");
  r.write(0x1f205e10,1,6,0,0,0);
  require((r.knownBits(0x1f205e10,1)&8)==0,"DLC command write cannot invent asynchronous completion");
  r.prepareMaskedRead(0x1f205e10,8);r.read(0x1f205e10,1,0,0,0);
  require(!r.stopReason.empty(),"unmodeled DLC completion stops even after guest writes control");
  r.stopReason.clear();r.exploratoryDlcTimeout=true;r.prepareMaskedRead(0x1f205e10,8);
  require(!(r.read(0x1f205e10,1,0,0,0)&8) && r.stopReason.empty() && !(r.knownBits(0x1f205e10,1)&8),
    "explicit timeout experiment does not validate completion status");
  r.symbolicInit=true;r.prepareOpaqueRead(0x1f205e1c);r.read(0x1f205e1c,1,0,0,0);
  require(r.stopReason.empty() && r.knownBits(0x1f205e1c,1)==0,"one-shot opaque sample does not define register bits");
  r.read(0x1f205e1c,1,0,0,0);
  require(!r.stopReason.empty(),"opaque sample permission cannot leak to a later read");
}
void combBankTests() {
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};
  require(!r.combBank(),"strict COMB selector starts unknown");
  rejects([&]{r.write(0x1f206c04,2,0x1234,0,0,0);});
  r.configureExploratoryCombBank(0);r.write(0x1f206c04,2,0x1234,0,0,0);
  r.write(0x1f206c00,1,1,0,0,0);r.write(0x1f206c04,2,0xabcd,0,0,0);
  r.write(0x1f206c00,1,0,0,0,0);
  require(r.read(0x1f206c04,2,0,0,0)==0x1234,"original COMB selector changes independently stored register banks");
  r.write(0x1f206c00,1,2,0,0,0);
  require(r.knownBits(0x1f206c04,2)==0,"untouched COMB bank does not inherit earlier table values");
}
void combRestoreTests() {
  manta::Riu r; r.event=[](auto&,auto,auto,auto,auto&){}; r.symbolicInit=true;
  r.configureExploratoryCombBank(0); r.write(0x1f206d1c,1,0xc3,0,0,0);
  r.prepareRmw(0x1f206d1d,0xcf); r.write(0x1f206d1d,1,0xc3,0,0,0);
  require(!r.beginCombRestore(0x1f206d1c,0x3030),"strict mode never permits foreign-byte opaque transfer");
  r.exploratoryCombRestore=true;
  require(!r.beginCombRestore(0x1f206d1c,0x3020),"paired-write rejects mismatched source masks");
  require(r.beginCombRestore(0x1f206d1c,0x3030),"explicit experiment opens only original special-register transaction");
  require(r.preservedStoreKey(0x1f206d1c)==r.controlByteKey(0x1f206d1d),"duplicated low sample retains original high bit identity");
  r.prepareRmw(0x1f206d1c,0xcfcf); r.write(0x1f206d1c,2,0xe3e3,0x805f883c,0,1);
  require(r.knownBits(0x1f206d1c,2)==0xcfcf,"temporary foreign unknowns invalidate previously known low bits");
  r.read(0x1f206d1c,1,0,0,2);
  require(!r.stopReason.empty(),"ordinary read cannot consume quarantined COMB bits");
  r.stopReason.clear(); r.prepareRmw(0x1f206d1c,255);
  r.read(0x1f206d1c,1,0x805f8860,0,3);
  require(r.stopReason.empty(),"original restore RMW discards every quarantined bit");
  r.write(0x1f206d1c,1,0xc3,0x805f887c,0,4);
  require(!r.combRestorePending() && r.combRestoreCount()==1,"only original exact saved-byte restore closes transaction");
  require(r.knownBits(0x1f206d1c,2)==0xcfff,"high unknown status/configuration bits remain invalid after restore");
  require(r.beginCombRestore(0x1f206d1c,0x3030),"new transaction may follow a completed restore");
  r.write(0x1f206d1c,2,0xe3e3,0x805f883c,0,5);
  r.write(0x1f206d1c,1,0xc2,0x805f887c,0,6);
  require(!r.stopReason.empty() && r.combRestorePending(),"incorrect restore cannot release unknown bits");
}
void preservedRegisterRestoreTests() {
  manta::Riu r; r.event=[](auto&,auto,auto,auto,auto&){};r.symbolicInit=true;
  {
    // Original 80504534..80504548: saved high nibble plus a fresh low
    // nibble. Neither sample may validate the other's unknown bits.
    constexpr uint32_t address=0x1f206bb1;
    r.write(address,1,0xa4,0,0,0);r.preparePreservedWrite(address,1,0xfe);
    r.write(address,1,0xa4,0,0,0);
    manta::PreservedBits p;std::array<uint32_t,32> regs{};
    regs[16]=regs[2]=0xa4;regs[3]=0xbf200000;
    p.seed(16,1,r.knownBits(address,1),[&](unsigned){return address;});
    p.seed(2,1,r.knownBits(address,1),[&](unsigned){return address;});
    uint32_t invalid=0;
    auto step=[&](uint32_t w){return p.observe(w,[&](unsigned n){return regs[n];},
      [](auto a){return a;},[&](auto a,auto n,auto mask){invalid=mask;r.preparePreservedWrite(a,n,mask);});};
    require(step(0x321000ff).empty() && step(0x321000f0).empty(),"original saved high nibble retains invalid provenance");
    regs[16]&=0xf0;
    require(step(0x3042000f).empty(),"fresh low-nibble observation retains its independent invalid bits");regs[2]&=15;
    require(step(0x02028025).empty() && p.mask(16)==0xfe,"joining two samples cannot launder unknown low bits");regs[16]|=regs[2];
    require(step(0xa0706bb1).empty() && invalid==0xfe,"original same-byte restore preserves all seven unknown bits");
    r.write(address,1,regs[16],0,0,0);
    require(r.knownBits(address,1)==1,"mixed-sample restore defines no additional hardware state");
  }
  r.write(0x1f205e00,1,0x1a,0,0,0);r.write(0x1f205e51,1,255,0,0,0);
  r.preparePreservedWrite(0x1f205e51,1,255);r.write(0x1f205e51,1,0xa5,0,0,1);
  require(r.knownBits(0x1f205e51,1)==0,"restoring invalid gain must invalidate a previously defined FF register");
  r.write(0x1f203ffc,1,5,0,0,2);require(r.gopBankKnown(),"explicit guest bank write defines selector");
  r.preparePreservedWrite(0x1f203ffc,1,15);r.write(0x1f203ffc,1,0xa5,0,0,3);
  require(!r.gopBankKnown() && r.knownBits(0x1f203ffc,1)==0,"opaque bank restore cannot make poison a usable bank");
  r.write(0x1f205e14,1,0xff,0,0,4);r.preparePreservedWrite(0x1f205e14,1,0x30);
  r.write(0x1f205e14,1,0xc3,0,0,5);
  require(r.knownBits(0x1f205e14,1)==0xcf,"preserved writes invalidate only original unknown bits");
  r.configureExploratoryGopBank(1);
  require(r.gopBankKnown() && r.gopBank()==1 && r.knownBits(0x1f203ffc,2)==15,"GOP experiment defines only low selector nibble, never IRQ or ACK");
}
void semaphoreTests() {
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};r.candidateSemaphore=true;
  r.read(0x1f203008,2,0,0,0);
  require(!r.stopReason.empty(),"resource ID reset is unknown, never universal zero");r.stopReason.clear();
  r.write(0x1f203004,2,2,0,0,0);
  require(!r.stopReason.empty() && r.semaphoreAcquisitions()==0,"unknown owner cannot be claimed by overwriting storage");r.stopReason.clear();
  r.write(0x1f203004,2,0,0,0,0);r.write(0x1f203004,2,2,0,0,0);
  require(r.read(0x1f203004,2,0,0,0)==2 && r.semaphoreAcquisitions()==1,"original reset then acquire defines an owner causally");
  r.write(0x1f203004,2,3,0,0,0);
  require(r.read(0x1f203004,2,0,0,0)==2 && r.semaphoreContentions()==1,"competing ID cannot steal an acquired semaphore");
  r.write(0x1f203004,2,2,0,0,0);
  require(r.semaphoreAcquisitions()==1,"same-owner reacquire is idempotent");
  r.configureExploratorySemaphoreIdle();
  require(r.read(0x1f203008,2,0,0,0)==0,"explicit initially-idle fixture leaves another resource unowned");
  rejects([&]{r.read(0x1f203008,1,0,0,0);});
  rejects([&]{r.write(0x1f203009,2,1,0,0,0);});
}
void geQueueTests() {
  manta::GeQueue unknown;
  rejects([&]{unknown.status();});
  require(!unknown.submit(0,0,0),"GE unknown initial occupancy cannot accept submissions");
  manta::GeQueue q; q.exploratoryReset();
  require(q.status()==0x8080,"GE empty experimental stages expose both original count fields");
  for(unsigned i=0;i<16;++i)require(q.submit(uint16_t(i),uint16_t(i+1),0),"GE real bounded submission");
  require(!q.submit(17,1,0),"GE full FIFO rejects overflow");
  require(((q.status()>>11)&31)==0 && ((q.status()>>3)&31)==16,"GE submission occupancy differs from execution occupancy");
  q.advance(7);require(q.transferred()==0,"GE time before deadline cannot drain queue");
  q.advance(8);require(q.transferred()==1 && q.completed()==0 && (q.status()&1),"GE timed transfer enters busy stage");
  require(((q.status()>>11)&31)==1 && ((q.status()>>3)&31)==15,"GE causal independent stage space");
  q.advance(1000);require(q.status()==0x8080 && q.completed()==16,"GE eventual drain follows submissions, not status reads");
  require(q.submitted()==16 && q.transferred()==16,"GE no invented completion without submission");
  rejects([&]{q.advance(999);});
  rejects([&]{q.submit(0x60,1,1000);});
  require(q.submitted()==16,"GE unsupported drawing command never accepted/retired");
  require(!q.completedTag(),"GE tag reset value remains unknown");
  require(q.submit(0x32,0x2345,1000),"GE original tag write enters queue");
  q.advance(1023);require(!q.completedTag(),"GE tag is not completed on write or FIFO transfer");
  q.advance(1024);require(q.completedTag()==0x2345,"GE tag commits at execution retirement");
  manta::Riu bus;bus.candidateGe=true;bus.symbolicInit=true;bus.configureExploratoryGeReset();
  bus.event=[](const auto&,auto,auto,auto,const auto&){};
  require(bus.knownBits(0x1f205004,2)==2 && bus.knownBits(0x1f205000,2)==1,"GE experiment defines only enable bits");
  bus.prepareMaskedRead(0x1f20501c,0xf800);
  require((bus.read(0x1f20501c,2,0,0,0)&0xf800)==0x8000 && bus.stopReason.empty(),"GE queue-space masked read");
  bus.read(0x1f20501c,2,0,0,0);
  require(!bus.stopReason.empty(),"GE unrelated status bits remain invalid");
}
void geRectFillTests(){
  using Fill=manta::GeRectFill;
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  auto set=[&](unsigned index,uint16_t value,uint16_t known=65535){regs[index]={value,known};};
  set(0,1);set(1,0,0x372);set(0x59,0);set(0x34,0xf00,0x1f00);
  set(0x26,0x1000);set(0x27,0);set(0x33,24);set(0x70,0xcdef);set(0x71,0x89ab);
  set(0x68,1);set(0x69,1);set(0x6a,3);set(0x6b,2);
  set(0x55,0);set(0x56,5);set(0x57,0);set(0x58,3);
  auto read=[&](unsigned index){return regs[index];};
  auto plan=Fill::decode(0x30,read);std::vector<uint8_t> memory(96,0xa5);unsigned writes=0,validations=0;
  auto writable=[&](uint32_t address,size_t size){++validations;return address>=0x1000 && uint64_t(address-0x1000)+size<=memory.size();};
  auto write=[&](uint32_t address,std::span<const uint8_t> bytes){++writes;
    std::copy(bytes.begin(),bytes.end(),memory.begin()+address-0x1000);};
  plan.execute(writable,write);
  bool exact=true;for(unsigned i=0;i<memory.size();++i){auto x=i%24,y=i/24;
    auto expected=y>=1 && y<=2 && x>=4 && x<16?uint8_t(0x89abcdefu>>(8*(x%4))):0xa5;
    exact&=memory[i]==expected;}
  require(exact && writes==2 && validations==1,"GE plain ARGB8888 fills only inclusive clipped pixels, preserves padding and validates complete range first");
  auto saved=regs;
  for(auto [index,pair]:saved){auto mask=pair.second;auto bit=uint16_t(mask&uint16_t(-mask));
    regs[index].second&=uint16_t(~bit);rejects([&]{Fill::decode(0x30,read);});regs=saved;}
  rejects([&]{Fill::decode(0x8040,read);});
  for(auto format:{9u,12u}){set(0x34,uint16_t(format<<8));rejects([&]{Fill::decode(0x30,read);});}regs=saved;
  for(auto control:{0u,3u,5u,0x401u}){set(0,uint16_t(control));rejects([&]{Fill::decode(0x30,read);});}regs=saved;
  set(1,0x10);rejects([&]{Fill::decode(0x30,read);});regs=saved;
  set(0x27,0x8000);rejects([&]{Fill::decode(0x30,read);});regs=saved;
  set(0x33,12);rejects([&]{Fill::decode(0x30,read);});regs=saved;
  writes=validations=0;set(0x55,4);auto empty=Fill::decode(0x30,read);empty.execute(writable,write);
  require(empty.empty && writes==0 && validations==0,"GE fully clipped rectangle retires without inventing a destination read or pixel");regs=saved;
  auto previous=memory;writes=0;
  rejects([&]{plan.execute([](auto,auto){return false;},write);});
  require(memory==previous && writes==0,"GE unavailable destination rejects before the first raster write");
  set(0x34,0x800);set(0x70,0x8080);set(0x71,0xff80);set(0x68,0);set(0x69,0);set(0x6a,0);set(0x6b,0);
  auto rgb565=Fill::decode(0x30,read);rgb565.execute(writable,write);
  require(memory[0]==0x10 && memory[1]==0x84,"GE RGB565 packs the known ARGB start color using 5/6/5 channels");
  manta::GeQueue q;q.exploratoryReset();unsigned effects=0;
  require(q.submit(0x60,0x30,0,[&]{++effects;}),"validated raster work enters the existing GE queue");
  q.advance(23);require(effects==0 && q.completed()==0,"GE raster effects never occur at submission or stage transfer");
  q.advance(24);require(effects==1 && q.completed()==1,"GE raster completion follows its actual memory effect");
  require(q.submit(0x60,0x30,24,[]{throw std::runtime_error("unavailable target");}),"pending raster retains its uncompleted work");
  rejects([&]{q.advance(48);});
  require(q.completed()==1,"failed raster effect never retires as success");
}
void htotalBranchJoinTests(){
  using J=manta::HtotalBranchJoin;
  std::map<uint32_t,uint32_t> words;for(auto [pc,w]:J::expected)words[pc]=w;
  auto fetch=[&](uint32_t pc){auto it=words.find(pc);if(it==words.end())throw std::runtime_error("unexpected fetch");return it->second;};
  for(unsigned counter:{0u,1u,3u,4u,5u,255u}){
    for(uint16_t history:{uint16_t(0),uint16_t(0x046f),uint16_t(0xffff)}){
      std::map<uint32_t,uint8_t> ram;ram[0x006d83d4]=uint8_t(counter);ram[0x006ad960]=0x11;ram[0x006ad961]=0x22;
      auto load8=[&](uint32_t a){return ram.at(a);};
      J::Regs entry{};entry[2]=0x1111;entry[3]=0x2222;entry[4]=0x3333;entry[5]=history;entry[7]=0x9999;entry[19]=0x7777;
      auto m=J::run(entry,fetch,load8);
      require(m.outcomes.size()==2 && m.common[19]==0x806b0000u,"both original outcomes load the same s3 base before the join");
      for(auto r:m.differingRegisters)require(r==2||r==3||r==4,"only dead v0/v1/a0 may differ between outcomes");
      require(m.common[5]==history && m.common[6]==0,"live history/mode registers are untouched by either outcome");
      bool counterUnknown=m.unknownBytes.at(0x006d83d4);
      require(counterUnknown,"stability counter differs between mismatch and match outcomes for every entry counter");
      if(counter<4)require(!m.unknownBytes.contains(0x006ad960),"accepted total is not written when the counter is below four");
      else{
        bool unknownTotal=m.unknownBytes.at(0x006ad960)||m.unknownBytes.at(0x006ad961);
        require(unknownTotal==(history!=0x2211),"accepted total merge compares the exact matched value against its entry value");
      }
    }
  }
  // Execution must use the guest's words: any change rejects before anything is merged.
  for(auto [pc,w]:J::expected){
    auto saved=words[pc];words[pc]=w^1;
    std::map<uint32_t,uint8_t> ram{{0x006d83d4,2},{0x006ad960,0},{0x006ad961,0}};
    rejects([&]{J::run(J::Regs{},fetch,[&](uint32_t a){return ram.at(a);});});
    words[pc]=saved;
  }
  // A live register differing between outcomes is refused (simulate by changing which register is cleared).
  {
    auto saved=words[0x80501750];words[0x80501750]=0xa05383d4; // sb $s3 instead of $zero: still verified-word mismatch
    std::map<uint32_t,uint8_t> ram{{0x006d83d4,2},{0x006ad960,0},{0x006ad961,0}};
    rejects([&]{J::run(J::Regs{},fetch,[&](uint32_t a){return ram.at(a);});});
    words[0x80501750]=saved;
  }
}
void geStretchRequestTests(){
  using Req=manta::GeStretchRequest;
  // Final 0x60=8040 request recorded from original OSD 3 instructions
  // (evidence/ge-osd-copy-guard.json.gz, allocated_paint_GE_registers).
  static const std::pair<unsigned,uint16_t> recorded[]={{0x00,0x0001},{0x55,0x0213},{0x57,0x002a},{0x56,0x0219},{0x58,0x0055},{0x34,0x0c09},{0x26,0xd800},{0x27,0x00dd},{0x33,0x0aa0},{0x01,0xa485},{0x00,0x0003},{0x13,0x00ff},{0x12,0x0100},{0x11,0x0001},{0x00,0x0007},{0x13,0x00ff},{0x12,0x0100},{0x11,0x0001},{0x00,0x0007},{0x11,0xff01},{0x16,0x0078},{0x17,0x0078},{0x14,0xf0ff},{0x15,0x00ff},{0x1c,0x0000},{0x00,0x0047},{0x34,0x0c09},{0x20,0xe128},{0x21,0x03d6},{0x30,0x000e},{0x01,0xa485},{0x59,0x0000},{0x64,0x1000},{0x65,0x0f45},{0x5e,0x0000},{0x5f,0x1fa3},{0x5e,0x0000},{0x5f,0x1fa3},{0x68,0x0213},{0x69,0x002a},{0x6a,0x0219},{0x6b,0x0055},{0x6c,0x0000},{0x6d,0x0000},{0x6e,0x0007},{0x6f,0x002a},{0x68,0x0213},{0x69,0x002a},{0x6a,0x0219},{0x6b,0x0055},{0x01,0xa495}};
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  for(auto [i,v]:recorded)regs[i]={v,i==1?uint16_t(0x372):uint16_t(0xffff)};
  auto saved=regs;auto get=[&](unsigned i){return regs[i];};
  auto r=Req::decode(0x8040,get);
  require(!r.nearest && r.dither() && r.blend() && r.sourceKey(),"recorded OSD 3 request is stretched, dithered, blended and source-keyed");
  require(r.sourceFormat==9 && r.destinationFormat==0xc && r.source==0x03d6e128 && r.destination==0x00ddd800,"recorded request source/destination formats and addresses");
  require(r.sourcePitch==14 && r.destinationPitch==2720 && r.srcWidth==7 && r.srcHeight==42,"recorded request pitches and source extent");
  require(r.destLeft==531 && r.destTop==42 && r.destRight==537 && r.destBottom==85,"recorded request destination rectangle is 7x44");
  require(r.ratioX==0x1000 && r.ratioY==0xf45 && r.phaseX==0 && r.phaseY==-93 && r.phaseUpY && !r.phaseUpX,"recorded request fixed-point ratio and phase match original scale arithmetic");
  require(r.keyLow==0x00780078 && r.keyHigh==0x00fff0ff,"recorded request key range halves");
  require(r.alpha11==0xff01 && r.alpha12==0x100 && r.alpha13==0xff,"recorded request alpha registers");
  require(r.unresolved().size()==7,"every unestablished hardware semantic of the recorded request is enumerated");
  require(Req::decode(0xc040,get).nearest,"C040 is the nearest-mode variant of the same stretch request");
  for(auto cmd:{0x40,0x30,0x8041,0x4040})rejects([&]{Req::decode(uint16_t(cmd),get);});
  for(auto [index,pair]:saved){
    auto bit=uint16_t(pair.second&uint16_t(-pair.second));regs[index].second&=uint16_t(~bit);
    rejects([&]{Req::decode(0x8040,get);});regs=saved;}
  regs[1].first=0xa485;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0x65].first=0xf46;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0x5f].first=0x1fa4;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0x5e].first=0x1000;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0x6f].first=43;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0x6a].first=530;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  regs[0].first=0;rejects([&]{Req::decode(0x8040,get);});regs=saved;
  require(Req::expectedRatio(7,7)==0x1000,"unit scale is exactly 1.0 in 4.12 fixed point");
  require(Req::expectedRatio(1,32)==128 && Req::expectedRatio(32,1)==0xffffffffu && Req::expectedRatio(31,1)==31u*4096,"scale ratio boundaries match the original 32x limit");
}
void geBitBltTests(){
  using Blit=manta::GeBitBlt;
  std::map<unsigned,std::pair<uint16_t,uint16_t>> regs;
  auto set=[&](unsigned i,uint16_t v,uint16_t k=65535){regs[i]={v,k};};
  set(0,1);set(1,0,0x372);set(0x59,0);set(0x34,0x0f0f,0x1f1f);
  set(0x20,0x1000);set(0x21,0);set(0x26,0x2000);set(0x27,0);
  set(0x30,24);set(0x33,28);set(0x68,1);set(0x69,1);set(0x6a,3);set(0x6b,2);
  set(0x6c,2);set(0x6d,1);set(0x6e,3);set(0x6f,2);
  set(0x55,2);set(0x56,5);set(0x57,0);set(0x58,3);
  auto get=[&](unsigned i){return regs[i];};auto saved=regs;
  std::vector<uint8_t> src(96),dst(112,0xa5);
  for(unsigned i=0;i<src.size();++i)src[i]=uint8_t(i*37+11);
  src[24+3*4+3]=0;src[24+4*4+3]=255;src[48+3*4+3]=127;
  unsigned reads=0,writes=0;
  auto read=[&](uint32_t a,size_t n){++reads;
    if(a<0x1000 || uint64_t(a-0x1000)+n>src.size())throw std::runtime_error("unmapped source");
    return std::vector<uint8_t>(src.begin()+a-0x1000,src.begin()+a-0x1000+n);};
  auto writable=[&](uint32_t a,size_t n){return a>=0x2000 && uint64_t(a-0x2000)+n<=dst.size();};
  auto write=[&](uint32_t a,std::span<const uint8_t> b){++writes;std::copy(b.begin(),b.end(),dst.begin()+a-0x2000);};
  for(auto format:{0x0f0fu,0x0808u}){
    regs=saved;set(0x34,uint16_t(format));auto plan=Blit::decode(0x40,get);
    std::fill(dst.begin(),dst.end(),0xa5);reads=writes=0;plan.execute(read,writable,write);
    unsigned bpp=format==0x0808?2:4;bool exact=true;
    for(unsigned i=0;i<dst.size();++i){auto y=i/28,x=i%28;
      uint8_t expected=0xa5;
      if(y>=1 && y<=2 && x>=2*bpp && x<4*bpp)expected=src[y*24+(x-2*bpp)+3*bpp];
      exact&=dst[i]==expected;
    }
    require(exact && reads==2 && writes==2,"GE plain copy preserves exact alpha/color bytes, source offset, clipping and both pitch paddings");
    if(bpp==4)require(dst[28+2*4+3]==0 && dst[28+3*4+3]==255 && dst[56+2*4+3]==127,
      "disabled GE blending preserves fully transparent, opaque and intermediate source alpha exactly");
  }
  regs=saved;
  for(auto [index,pair]:saved){
    auto bit=uint16_t(pair.second&uint16_t(-pair.second));regs[index].second&=uint16_t(~bit);
    rejects([&]{Blit::decode(0x40,get);});regs=saved;
  }
  for(auto cmd:{0x30,0x8040,0xc040,0x41,0x140})rejects([&]{Blit::decode(uint16_t(cmd),get);});
  for(auto format:{0x0c09,0x0909,0x0f08,0x080f}){set(0x34,uint16_t(format));rejects([&]{Blit::decode(0x40,get);});}regs=saved;
  for(auto control:{0,3,5,0x47,0x401}){set(0,uint16_t(control));rejects([&]{Blit::decode(0x40,get);});}regs=saved;
  for(auto control:{0x10,0x20,0x40,0x100,0x200}){set(1,uint16_t(control));rejects([&]{Blit::decode(0x40,get);});}regs=saved;
  set(0x6e,4);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  set(0x59,1);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  for(auto index:{0x21,0x27}){set(index,0x8000);rejects([&]{Blit::decode(0x40,get);});regs=saved;}
  for(auto index:{0x30,0x33}){set(index,7);rejects([&]{Blit::decode(0x40,get);});regs=saved;}
  set(0x20,0x2000);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  set(0x55,4096);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  set(0x6c,4095);set(0x30,65532);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  set(0x6d,4095);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  set(0x27,0x7ff);set(0x26,0xfff0);rejects([&]{Blit::decode(0x40,get);});regs=saved;
  auto plan=Blit::decode(0x40,get);auto before=dst;reads=writes=0;
  rejects([&]{plan.execute(read,[](auto,auto){return false;},write);});
  require(reads==0 && writes==0 && dst==before,"unmapped BitBlt target fails before source read and writes");
  rejects([&]{plan.execute({},writable,write);});
  rejects([&]{plan.execute(read,writable,{});});
  auto shortRead=[&](uint32_t,size_t n){return std::vector<uint8_t>(n-1);};
  rejects([&]{plan.execute(shortRead,writable,write);});
  require(writes==0 && dst==before,"short BitBlt source never paints or retires");
  unsigned sourceRow=0;
  auto invalidRead=[&](uint32_t a,size_t n){if(++sourceRow==2)throw std::runtime_error("invalid hardware-origin bits");return read(a,n);};
  manta::GeQueue q;q.exploratoryReset();q.submit(0x60,0x40,0,[&]{plan.execute(invalidRead,writable,write);});
  rejects([&]{q.advance(24);});
  require(writes==0 && dst==before && q.completed()==0,"unknown late source row preserves all destination bytes and prevents queue retirement");
  set(0x55,4);auto empty=Blit::decode(0x40,get);empty.execute({}, {}, {});
  require(empty.empty,"fully clipped copy consumes no guest bytes");
}
void opaquePolicyTests(){
  {
    manta::Riu bus;bus.event=[](auto&,auto,auto,auto,auto&){};
    bus.write(0x1f206b98,2,0xffff,0,0,0);
    require(!(bus.knownBits(0x1f206b98,2)&0x4000),"software writes cannot define live AFEC HSYNC lock");
    bus.configureExploratoryAnalogDisconnected();
    require(!(bus.knownBits(0x1f206b98,2)&0x4000),"disconnected HDTV fixture alone cannot silently authorize AVD status");
    bus.configureExploratoryAvdNoHsync();
    bus.prepareMaskedRead(0x1f206b98,0x4000);
    require(!(bus.read(0x1f206b98,2,0,0,1)&0x4000) && bus.stopReason.empty(),"separate absent-HSYNC experiment derives lock low from explicit disconnected input");
    bus.prepareMaskedRead(0x1f206b99,0x40);
    require(bus.knownBits(0x1f206b99,1)==255 && !(bus.read(0x1f206b99,1,0,0,1)&0x40),"byte and halfword AFEC status agree on bit14 address encoding");
    bus.configureExploratoryAnalogDisconnected(false);
    require(!(bus.knownBits(0x1f206b98,2)&0x4000),"removing disconnected input withdraws HSYNC validity");
    manta::Riu fresh;fresh.event=bus.event;fresh.configureExploratoryAnalogDisconnected();fresh.configureExploratoryAvdNoHsync();
    require(fresh.knownBits(0x1f206b98,2)==0x4000 && fresh.knownBits(0x1f206b98,1)==0,
      "absent sync establishes neither status-ready nor any unrelated low-byte field");
    fresh.read(0x1f206b98,2,0,0,1);
    require(!fresh.stopReason.empty(),"full AFEC status cannot be consumed as a fabricated all-zero no-signal response");
    // Original MDrv_AVD_IsSyncLocked extracts bit14; all other bits discarded.
    constexpr uint32_t extract=0x7c420380; // EXT v0,v0,14,1
    for(auto other:{0u,0xbfffu}){
      manta::Engine cpu;cpu.map(0,0x1000);cpu.write(0,&extract,4);cpu.reg(UC_MIPS_REG_2,other);
      require(cpu.run(0x80000000,1)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_2)==0,
        "original driver returns no HSYNC regardless of every unimplemented status field");
    }
    constexpr uint32_t hal[]={0x3c04806b,0x2482d974,0x90430004,0x10600004,0x00001021,
      0x8c82d974,0x94436b98,0x3062ffff,0x03e00008,0};
    constexpr uint32_t driver[]={0x27bdffe8,0xafbf0014,0x0c140c00,0,0x8fbf0014,0x7c420380,0x03e00008,0x27bd0018};
    manta::Engine original;original.map(0,0x20000);original.map(0x00501000,0x1000);
    original.map(0x00503000,0x1000);original.map(0x006ad000,0x1000);
    original.write(0x00503000,hal,sizeof(hal));original.write(0x00501698,driver,sizeof(driver));
    uint32_t base=0xbf200000;uint8_t initialized=1;
    original.write(0x006ad974,&base,4);original.write(0x006ad978,&initialized,1);
    fresh.symbolicInit=true;fresh.stopReason.clear();
    struct Context{manta::Riu* bus;unsigned reads=0;} context{&fresh};
    auto read=+[](uc_engine*,uint64_t offset,unsigned n,void* user)->uint64_t{
      auto& c=*static_cast<Context*>(user);++c.reads;auto a=0x1f206000u+uint32_t(offset);
      c.bus->prepareOpaqueRead(a);return c.bus->read(a,n,0x80503018,0,1);
    };
    manta::check(uc_mmio_map(original.handle,0x1f206000,0x1000,read,&context,nullptr,nullptr));
    original.reg(UC_MIPS_REG_SP,0x80010000);original.reg(UC_MIPS_REG_RA,0x80000100);
    require(original.run(0x80501698,18)==UC_ERR_OK && original.reg(UC_MIPS_REG_PC)==0x80000100 &&
      original.reg(UC_MIPS_REG_2)==0 && context.reads==1 && fresh.knownBits(0x1f206b98,2)==0x4000,
      "original AVD caller and HAL execute real LHU/EXT, preserve unknown unrelated status and return absent HSYNC");
  }
  std::vector<uint8_t> code{0,0,0x62,0x90}; // original804FB278 LBU v0,(v1)
  auto line=std::string("0x804fb278 0x1f225900 1 0x804fb278 4 ")+manta::sha256(code)+"\n";
  auto rules=manta::parseOpaqueReadPolicy("# INVALID observations only\n"+line);
  require(rules.size()==1 && manta::validateOpaqueReadRule(rules[0],0x804fb278,0x1f225900,1,code),"opaque policy requires the exact original load bytes and PC/address/width");
  auto bad=code;bad[0]^=1;
  require(!manta::validateOpaqueReadRule(rules[0],0x804fb278,0x1f225900,1,bad),"changed instruction bytes cannot acquire an opaque observation permission");
  require(!manta::validateOpaqueReadRule(rules[0],0x804fb27c,0x1f225900,1,code),"opaque policy cannot move the guest PC or authorize another instruction");
  require(!manta::validateOpaqueReadRule(rules[0],0x804fb278,0x1f225904,1,code),"opaque policy cannot change the observed peripheral address");
  auto wrong=rules[0];wrong.width=2;
  require(!manta::validateOpaqueReadRule(wrong,wrong.pc,wrong.address,2,code),"load width must be proven by the real instruction encoding");
  auto arithmetic=std::vector<uint8_t>{0x21,0x10,0,0};wrong=rules[0];wrong.hash=manta::sha256(arithmetic);
  require(!manta::validateOpaqueReadRule(wrong,wrong.pc,wrong.address,1,arithmetic),"an arithmetic instruction cannot be used as an opaque read");
  rejects([&]{manta::parseOpaqueReadPolicy(line+line);});
  rejects([&]{manta::parseOpaqueReadPolicy(line.substr(0,line.size()-1)+" 0xe3\n");});
  rejects([&]{manta::parseOpaqueReadPolicy("0x804fb278 0 1 0x804fb278 4 "+rules[0].hash);});
  rejects([&]{manta::parseOpaqueReadPolicy("0x804fb278 0x1f225900 1 0x804fb278 0 "+rules[0].hash);});
  rejects([&]{manta::parseOpaqueReadPolicy(std::string(65537,'#'));});
  manta::PreservedBits invalid;invalid.seed(2,1,0,[](unsigned){return 0x1f225900u;});
  auto regs=[](unsigned r){return r==2?0xa5u:0u;};
  require(!invalid.observe(0x10400001,regs,[](auto a){return a;},[](auto,auto,auto){}).empty(),"an opaque policy observation still cannot decide a genuinely unknown status predicate");
}
void readMaskTests() {
  require(!manta::rmwLogicalInputTracked(0,0xf0,false),"an earlier opaque high field cannot masquerade as a known RMW operand");
  require(!manta::rmwLogicalInputTracked(0,0,true),"coarse invalid observations cannot become concrete RMW masks");
  require(manta::rmwLogicalInputTracked(255,0,false) && manta::rmwLogicalInputTracked(0,0,false),"current sampled dependencies and genuinely known operands remain distinguishable");
  require(manta::independentStackSpill(0xafbf0044,true,true),"independent original callee-save SW may interleave with device RMW proof");
  require(!manta::independentStackSpill(0xafa20044,true,false),"a sampled MMIO value cannot escape through an accepted stack spill");
  require(!manta::independentStackSpill(0xafbf0044,false,true),"unknown stack pointer cannot authorize a lookahead memory effect");
  require(!manta::independentStackSpill(0xacbf0044,true,true),"a store to an unrelated base is not a callee-save stack spill");
  std::array<uint32_t,15> adc={0x90620000,0x30a500ff,0x30c600ff,0x10800009,0x00001021,
    0x90620000,0x304400ff,0x14a00003,0x00861025,0x00061027,0x00821024,0xa0620000,0x24020001,0x03e00008,0};
  require(manta::discardedAdcControlSample(adc),"exact original ADC helper proves first sample is unused");
  auto altered=adc;altered[4]=0x00401021;
  require(!manta::discardedAdcControlSample(altered),"retaining the first ADC sample must invalidate the proof");
  altered=adc;altered[3]=0x10400009;
  require(!manta::discardedAdcControlSample(altered),"branching on the first ADC sample must invalidate the proof");
  for(auto poison:{0u,255u}){
    struct Bus {unsigned first,reads=0,writes=0,value=0;} bus{poison};
    manta::Engine cpu;cpu.map(0,0x1000);cpu.write(0,adc.data(),sizeof(adc));
    auto read=+[](uc_engine*,uint64_t,unsigned,void* p)->uint64_t{auto& b=*static_cast<Bus*>(p);return ++b.reads==1?b.first:0x54;};
    auto write=+[](uc_engine*,uint64_t,unsigned,uint64_t v,void* p){auto& b=*static_cast<Bus*>(p);++b.writes;b.value=unsigned(v);};
    manta::check(uc_mmio_map(cpu.handle,0x1f204000,0x1000,read,&bus,write,&bus));
    cpu.reg(UC_MIPS_REG_3,0xbf204afc);cpu.reg(UC_MIPS_REG_4,0x10257e);
    cpu.reg(UC_MIPS_REG_5,1);cpu.reg(UC_MIPS_REG_6,1);cpu.reg(UC_MIPS_REG_RA,0x80000100);
    require(cpu.run(0x80000000,13)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_PC)==0x80000100 &&
      bus.reads==2 && bus.writes==1 && bus.value==0x55,
      "original ADC instructions discard either first value and update only the independently reread byte");
  }
  constexpr uint32_t originalExt=0x7cc53c00; // EXT a1,a2,16,8 at805EE834
  manta::Engine ext;ext.map(0,0x1000);ext.write(0,&originalExt,4);ext.reg(UC_MIPS_REG_6,0xab35cdef);
  require(ext.run(0x80000000,1)==UC_ERR_OK && ext.reg(UC_MIPS_REG_5)==0x35,
          "original interleaved EXT executes its architectural extraction");
  require(manta::independentExtract(originalExt,0xab35cdef,true)==0x35,
          "RMW proof agrees with original instruction execution without touching sampled a0");
  require(!manta::independentExtract(originalExt,0xab35cdef,false),"RMW proof rejects an unknown independent EXT source");
  require(!manta::independentExtract(originalExt|4,0xab35cdef,true),"INS cannot masquerade as an independent EXT");
  require(!manta::independentExtract(0x7cc5ffc0,0xab35cdef,true),"invalid overflowing EXT range is rejected");
  std::array<uint32_t,8> ctrl={0x95025004,0xafbf002c,0xafb50028,0x30420002,
      0xafb30020,0xafb2001c,0xafb10018,0xafb00014};
  require(manta::immediateReadMask(ctrl)==2,"stock GE control read masks bit1 after independent SWs");
  auto leaked=ctrl;leaked[1]=0xafa2002c;
  require(!manta::immediateReadMask(leaked),"read mask rejects earlier sampled-value store");
  leaked=ctrl;leaked[1]=0x10400001;
  require(!manta::immediateReadMask(leaked),"read mask rejects earlier decision");
  leaked=ctrl;leaked[1]=0x24420001;
  require(!manta::immediateReadMask(leaked),"read mask rejects earlier modification");
  std::array<uint32_t,8> fifo={0x9503501c,0x24110010,0x2ca20010,0x00a2880a,
      0x7c6322c0,0x0071182b,0x1060fff0,0x2506501c};
  require(manta::immediateReadMask(fifo)==0xf800,"stock GE EXT reads actual high queue field");
  auto copied=fifo;copied[1]=0x00608821;
  require(!manta::immediateReadMask(copied),"read mask rejects earlier register copy");
  std::array<uint32_t,3> leaf={0x90620000,0x03e00008,0x30420001};
  require(manta::immediateReadMask(leaf)==1,"original JR delay-slot mask remains supported");
}
void gopSelectorProvenanceTests() {
  manta::PreservedBits p;
  std::array<uint32_t,32> regs{};regs[2]=0xa5a0;regs[3]=0x1ffc;regs[4]=0;
  p.seed(2,2,15,[](unsigned b){return 0x1f203ffc+b;});
  auto step=[&](uint32_t word) { return p.observe(word,[&](unsigned r){return regs[r];},
      [](uint32_t a){return a;},[](uint32_t,unsigned,uint32_t){}); };
  require(step(0x3047000f).empty() && p.mask(7)==0,"GOP original bank ANDI discards unknown IRQ/ACK bits into different destination");
  regs[7]=0;
  require(step(0x10870011).empty(),"GOP bank comparison consumes only known selector nibble");
  require(step(0x3062ffff).empty() && p.mask(2)==0,"GOP branch delay slot replaces unused unknown selector value");
  p.seed(2,2,15,[](unsigned b){return 0x1f203ffc+b;});
  require(step(0x3042fff0).empty() && p.mask(2)==0xfff0,"GOP original selector restore retains unknown high fields");
  require(!step(0x10400001).empty(),"GOP unknown IRQ/ACK decision remains blocked");
  p.seed(2,2,255,[](unsigned b){return 0x800001f8+b;});regs[16]=0;
  require(step(0x02021007).empty() && p.mask(2)==0xff00,"GOP original SRAV retains exact unknown bit identities with a known shift");
  require(step(0x30420003).empty() && p.mask(2)==0,"GOP original MIU selector mask discards unused fields");
  p.seed(2,2,255,[](unsigned b){return 0x800001f8+b;});regs[16]=8;
  require(step(0x02021007).empty() && p.mask(2)==255,"GOP different plane cannot consume unknown selector bits as known");
  p.seed(16,1,0,[](unsigned b){return 0x9000+b;});
  require(!step(0x02021007).empty(),"GOP unknown shift count remains a decision blocker");
}
void gopPaletteTests() {
  manta::GopPalette p;
  require(!p.color(0,7) && p.defined(0)==0,"GOP palette reset colors stay unknown");
  require(!p.pulse(0,0x3007,0xffff,0xff112233,0xffffffffu),"GOP color data without command cannot populate palette");
  require(!p.pulse(0,0x3107,0xffff,0xff112233,0xffffffffu),"GOP starts original palette pulse");
  require(!p.color(0,7),"GOP palette remains undefined before complete pulse");
  require(p.pulse(0,0x3007,0xffff,0xff112233,0xffffffffu),"GOP completes original write pulse");
  require(p.color(0,7)==0xff112233 && p.commits()==1 && p.defined(0)==1,"GOP retains exact guest packed color");
  require(!p.color(1,7),"GOP palettes retain independent bank state");
  p.pulse(3,0x3107,0xffff,0x80445566,0xffffffffu);
  require(p.pulse(3,0x3007,0xffff,0x80445566,0xffffffffu) && p.color(1,7)==0x80445566,"GOP second plane original pulse");
  rejects([&]{p.pulse(0,0x3101,0xffff,0,0xffff);});
  rejects([&]{p.pulse(0,0x3101,0xff00,0,0xffffffffu);});
  p.pulse(0,0x3101,0xffff,0x12345678,0xffffffffu);
  rejects([&]{p.pulse(0,0x3002,0xffff,0x12345678,0xffffffffu);});
  require(p.commits()==2,"GOP rejected malformed pulses cannot manufacture entries");
  p.pulse(0,0x2109,0xffff,0x76543210,0xffffffffu);
  require(p.pulse(0,0x2009,0xffff,0x76543210,0xffffffffu) && p.mode(0,9)==0x2000,
      "GOP observed mode2 raw color remains separate from mode3; no pixel format conversion invented");
}
void vdSharedExecutionTests(){
  auto program=[](const std::vector<uint8_t>& code){
    manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
    for(auto b:code)p.write(0x18,b);return p;
  };
  {
    // Architectural observation used by the PM history: execute both bytes
    // of MOV DPTR and the 16-bit rollover, without modifying processor state.
    auto p=program({0x90,0xff,0xff,0xa3});manta::VdMcuCpu cpu;
    cpu.resetProgram(p);cpu.step();
    require(cpu.dataPointer()==0xffff,"8051 history observes the actual little register pair after MOV DPTR");
    auto state=cpu.stateBytes();(void)cpu.dataPointer();
    require(cpu.stateBytes()==state,"read-only PM DPTR observation cannot alter execution/provenance state");
    cpu.step();require(cpu.dataPointer()==0,"8051 INC DPTR rolls over the full 16-bit register pair");
  }
  for(bool timer:{false,true}){
    std::vector<uint8_t> code(128);code[0]=2;code[2]=0x20;
    code[timer?0xb:3]=2;code[(timer?0xb:3)+2]=0x40;
    const std::vector<uint8_t> init=timer?std::vector<uint8_t>{0x75,0x81,0xa5,0x75,0xa8,0x82,0x75,0x89,1,
      0x75,0x8c,0xff,0x75,0x8a,0xfc,0xd2,0x8c,0x80,0xfe}:
      std::vector<uint8_t>{0x75,0x81,0xa5,0x75,0xa8,0x81,0xd2,0x88,0x80,0xfe};
    std::copy(init.begin(),init.end(),code.begin()+0x20);
    const std::vector<uint8_t> handler=timer?std::vector<uint8_t>{0xc2,0x8c,0x74,0x5a,0x32}:
      std::vector<uint8_t>{0x74,0x5a,0x32};
    std::copy(handler.begin(),handler.end(),code.begin()+0x40);
    auto p=program(code);manta::VdMcuCpu cpu;cpu.standardInterrupts=true;
    std::vector<uint16_t> fetches;cpu.instruction=[&](uint16_t pc,uint8_t b,const auto&){
      require(pc<code.size() && b==code[pc],"8051 ISR fetch is validated after architectural interrupt entry");fetches.push_back(pc);
    };
    cpu.resetProgram(p);for(unsigned i=0;i<20;++i)cpu.step();
    if(!timer)cpu.requestInt0();
    for(unsigned i=0;i<70;++i)cpu.step();
    require(cpu.interruptEntries()==1 && cpu.accumulator()==0x5a && cpu.stack()==0xa5,
      "8051 executes one real ISR and RETI; edge flag clears at entry and timer handler stops its source");
    require(std::find(fetches.begin(),fetches.end(),timer?0xb:3)!=fetches.end(),
      "trace contains the actual auxiliary interrupt vector, not an interrupted-PC opcode");
  }
  {
    auto p=program({0x75,0x87,1});manta::VdMcuCpu c;c.resetProgram(p);
    for(unsigned i=0;i<20;++i)c.step();
    require(c.instructions()==1,"8051 idle ticks are not counted as original instruction execution");
  }
  for(bool badBranch:{false,true}){
    auto p=program({0x90,0x1b,0,0xe0,0x54,0xfb,0xf0,uint8_t(badBranch?0x60:0x90),0x40,0,0xe0});
    manta::VdMcuCpu c;unsigned restores=0;c.busRead=[](uint16_t a)->std::optional<manta::VdByte>{
      if(a==0x1b00)return manta::VdByte{0xa5,0,a};return {};
    };
    c.busWrite=[&](uint16_t a,manta::VdByte v){
      require(a==0x1b00 && v.known==4 && v.origin==a && !(v.value&4),
        "original MCU mask RMW defines only the cleared bit, retaining unknown-byte origin");++restores;return true;
    };
    std::string stop;try{c.startup(p,100);}catch(const std::exception& e){stop=e.what();}
    require(restores==1 && stop.find(badBranch?"unknown XDATA bits":"XDATA read unsupported")!=std::string::npos,
      "guarded MCU RMW does not turn unknown mask bits into a usable branch or status response");
  }
  for(unsigned scenario=0;scenario<5;++scenario){
    // MOVX -> R6 -> independent CLR -> reload; neither Rn nor its direct
    // IRAM alias can convert an invalid hardware observation to valid data.
    std::vector<uint8_t> code={0x90,0x1b,0,0xe0,0xfe,0xe4};
    if(scenario==1)code.insert(code.end(),{0x7e,0x42});
    if(scenario==3)code.insert(code.end(),{0x75,0xd0,8});
    if(scenario==4)code.insert(code.end(),{0x78,6,0xe6});
    else code.insert(code.end(),scenario==2?std::initializer_list<uint8_t>{0xe5,6}:std::initializer_list<uint8_t>{0xee});
    code.insert(code.end(),{0x90,0x40,0,0xf0,0x60,0});
    auto p=program(code);manta::VdMcuCpu c;manta::VdByte saved;unsigned writes=0;
    c.busRead=[](uint16_t a)->std::optional<manta::VdByte>{
      if(a==0x1b00)return manta::VdByte{0xa5,0,a};return {};};
    c.busWrite=[&](uint16_t a,manta::VdByte value){require(a==0x4000,"MCU test destination is private RAM");saved=value;++writes;return true;};
    std::string stop;try{c.startup(p,100);}catch(const std::exception& e){stop=e.what();}
    require(writes==1,"MCU register-provenance fixture executes actual MOVX store");
    require(saved.known==(scenario==1 || scenario==3?255:0),"8051 banked Rn and direct alias preserve invalid source; actual overwrite removes it");
    if(scenario==0 || scenario==2 || scenario==4)require(saved.origin==0x1b00 && stop.find("unknown XDATA bits")!=std::string::npos,
      "unknown peripheral value survives Rn reload and stops before original conditional branch");
  }
  for(uint8_t stackBase:{7,0x40,0xa5})for(uint8_t known:{0,0x7f,255}){
    auto p=program({0x75,0x81,stackBase,0x90,0x1b,0,0xe0,0xc0,0xe0,0xe4,0xd0,0xe0,
      0x60,0,0x90,0x40,0,0xe0});
    manta::VdMcuCpu cpu;cpu.busRead=[&](uint16_t address)->std::optional<manta::VdByte>{
      if(address==0x1b00)return manta::VdByte{0xa5,known,address};return {};};
    std::string stop;try{cpu.startup(p,100);}catch(const std::exception& error){stop=error.what();}
    require(cpu.instructions()==(known==255?8:6) && cpu.pc()==(known==255?17:12),
      "PUSH/POP saves unknown bytes across lower and upper IRAM, stopping only at their consumer");
    require(cpu.accumulatorKnown()==known && cpu.accumulatorOrigin()==0x1b00 && cpu.stack()==stackBase,
      "PUSH/POP restores the exact byte mask, origin and original stack pointer");
    require(stop.find(known==255?"XDATA read unsupported":"unknown XDATA bits")!=std::string::npos,
      "known stack data continues, while invalid data never supplies a branch predicate");
  }
  for(uint8_t destination:{0x82,0xd0}){
    auto p=program({0x75,0x81,0xa5,0x90,0x1b,0,0xe0,0xc0,0xe0,0xe4,0xd0,destination});
    manta::VdMcuCpu cpu;cpu.busRead=[](uint16_t address)->std::optional<manta::VdByte>{
      return manta::VdByte{0xa5,0,address};};
    std::string stop;try{cpu.startup(p,100);}catch(const std::exception& error){stop=error.what();}
    require(cpu.pc()==10 && cpu.stack()==0xa6 && stop.find("SFR control/address")!=std::string::npos,
      "POP cannot convert unknown stack bits into DPTR or PSW control state");
  }
  {
    auto p=program({0x75,0x81,0xa5,0x75,0x82,0x42,0xc0,0x82,0x75,0x82,0,
      0xd0,0x82,0xe5,0x82,0x90,0x40,0,0xf0,0x80,0xfe});
    manta::VdMcuCpu cpu;std::string stop;unsigned writes=0;
    cpu.busWrite=[&](uint16_t address,manta::VdByte value){
      require(address==0x4000 && value.value==0x42 && value.known==255,
        "known DPL survives its independent SFR stack round trip");++writes;return true;};
    try{cpu.startup(p,100);}catch(const std::exception& error){stop=error.what();}
    require(writes==1 && cpu.stack()==0xa5 && stop.find("cycle budget")!=std::string::npos,
      "direct DPL stack save uses the SFR rather than unrelated upper IRAM provenance");
  }
  for(uint8_t destination:{0x20,0x42,0xe0}){
    auto save=program({0x75,0x81,0xa5,0x90,0x1b,0,0xe0,0xc0,0xe0,0x80,0xfe});
    auto failedPush=program({0x75,0x81,0xa5,0xc0,0x8e});
    std::vector<uint8_t> restoreCode={0x75,0x81,0xa6,0xd0,destination};
    if(destination!=0xe0)restoreCode.insert(restoreCode.end(),{0xe5,destination});
    restoreCode.insert(restoreCode.end(),{0x60,0});auto restore=program(restoreCode);
    manta::VdMcuCpu cpu;cpu.busRead=[](uint16_t address)->std::optional<manta::VdByte>{
      return manta::VdByte{0xa5,0,address};};
    cpu.resetProgram(save);
    for(unsigned ticks=0;ticks<100 && cpu.instructions()<4;++ticks)cpu.step();
    require(cpu.stack()==0xa6,"reset fixture saves an invalid byte in actual upper IRAM");
    cpu.resetProgram(failedPush);std::string failed;
    try{for(unsigned ticks=0;ticks<100;++ticks)cpu.step();}catch(const std::exception& error){failed=error.what();}
    require(cpu.pc()==3 && cpu.stack()==0xa5 && failed.find("nonstandard SFR read unsupported")!=std::string::npos,
      "failed PUSH source read cannot write stack RAM or commit its speculative provenance");
    cpu.resetProgram(restore);std::string stop;
    try{for(unsigned ticks=0;ticks<100;++ticks)cpu.step();}catch(const std::exception& error){stop=error.what();}
    require(cpu.pc()==(destination==0xe0?5:7) && cpu.accumulatorKnown()==0 && cpu.accumulatorOrigin()==0x1b00 &&
      stop.find("unknown XDATA bits")!=std::string::npos,
      "warm reset preserves retained upper-IRAM provenance through direct RAM and ACC restores");
  }
  {
    std::vector<uint8_t> code(128);code[0]=2;code[2]=0x20;code[3]=2;code[5]=0x40;
    const std::vector<uint8_t> init={0x75,0x81,0xa5,0x75,0xa8,0x81,0xd2,0x88,0x90,0x40,0,0xe0,0x80,0xfe};
    const std::vector<uint8_t> handler={0xc0,0xe0,0xc0,0x83,0xc0,0x82,0xc0,0xd0,
      0x75,0xd0,0x18,0xc2,0xa8,0x90,0x1b,0x10,0xe0,0xd0,0xd0,0xd0,0x82,0xd0,0x83,0xd0,0xe0,0x32};
    std::copy(init.begin(),init.end(),code.begin()+0x20);
    std::copy(handler.begin(),handler.end(),code.begin()+0x40);
    auto p=program(code);manta::VdMcuCpu cpu;cpu.standardInterrupts=true;
    cpu.busRead=[](uint16_t address)->std::optional<manta::VdByte>{
      return manta::VdByte{0xa5,uint8_t(address==0x4000?0:255),address};};
    cpu.resetProgram(p);
    for(unsigned count=0;count<100 && cpu.accumulatorKnown()==255;++count)cpu.step();
    require(cpu.accumulatorKnown()==0,"interrupt fixture reaches the actual invalid MOVX result");
    cpu.requestInt0();
    for(unsigned count=0;count<100 && (cpu.pc()!=0x2c || cpu.interruptEntries()==0);++count)cpu.step();
    require(cpu.pc()==0x2c && cpu.interruptEntries()==1 && cpu.stack()==0xa5,
      "actual interrupt entry, original ISR save prefix and RETI restore the interrupted instruction stream");
    require(cpu.accumulatorKnown()==0 && cpu.accumulatorOrigin()==0x4000 && cpu.dataPointer()==0x4000,
      "ISR round trip preserves the invalid accumulator and independently known DPTR");
  }
  {
    auto p=program({0x90,0x1b,0,0xe0,0xfe,0xe4,0x75,0x81,6,0x22});
    manta::VdMcuCpu c;c.busRead=[](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{0xa5,0,a};};
    std::string stop;try{c.startup(p,100);}catch(const std::exception& e){stop=e.what();}
    require(stop.find("stack/return address")!=std::string::npos && c.pc()==9,
      "invalid R6 cannot become a return PC through overlapping low-IRAM stack storage");
  }
  {
    manta::Riu r;r.event=[](auto,auto,auto,auto,auto){};
    r.candidateVdMcuProgram=true;r.exploratoryVdMcuStartup=true;r.exploratoryVdSharedRun=true;
    r.exploratoryVdSuspendedBoundary=true;
    uint64_t tick=0;auto w=[&](unsigned p,unsigned v){r.write(0x1f206900+p,1,v,0,255,++tick);};
    w(0,1);w(1,0);w(5,0x51);w(8,0);w(9,0);
    const std::vector<uint8_t> code={0x90,0x2f,0,0xe4,0xf0,0xa3,0x74,0x23,0xf0,0xa3,0x74,0x81,0xf0,
      0xa3,0x74,0x66,0xf0,0x90,0x40,0,0xe0};
    for(auto b:code)w(0x18,b);w(5,0x50);w(5,0);w(1,1);w(0,0);
    require(r.knownBits(0x1f224c00,1)==0,"mailbox reset value remains unknown before actual MCU instructions write it");
    r.write(0x1f206700,1,0,0,255,tick+=1000);
    require(r.vdMailboxWrites()==4 && r.vdMcuCpu().instructions()==13 && r.vdMcuSuspended(),
      "scheduled MCU executes its real MOVX stores and stops on unmapped external data");
    require(r.read(0x1f224c00,1,0,255,++tick)==0 && r.read(0x1f224c01,1,0,255,++tick)==0x23 &&
      r.read(0x1f224c04,1,0,255,++tick)==0x81 && r.read(0x1f224c05,1,0,255,++tick)==0x66,
      "MIPS sees original MCU writes through the verified byte-strided shared mailbox aliases");
    require(r.knownBits(0x1f224c02,1)==0,"shared aliases do not define unconnected RIU byte gaps");
  }
  {
    manta::Riu r;r.event=[](auto,auto,auto,auto,auto){};
    r.candidateVdMcuProgram=true;r.exploratoryVdMcuStartup=true;r.exploratoryVdSharedRun=true;
    uint64_t tick=0;auto w=[&](unsigned p,unsigned v){r.write(0x1f206900+p,1,v,0,255,++tick);};
    w(0,1);w(1,0);w(5,0x51);w(8,0);w(9,0);w(0x18,0x80);w(0x18,0xfe);
    w(5,0x50);w(5,0);w(1,1);w(0,0);
    r.write(0x1f206700,1,0,0,255,tick+=100);
    require(r.vdMcuCpu().totalInstructions()>0,"released auxiliary CPU really executes its uploaded branch loop");
    w(0,1);auto held=r.vdMcuCpu().totalInstructions();
    r.write(0x1f206700,1,0,0,255,tick+=10000);
    require(r.vdMcuCpu().totalInstructions()==held,"host reset hold prevents all auxiliary instruction dispatch, without catch-up execution");
    w(0,0);require(r.vdMcuCpu().instructions()==0 && r.vdMcuCpu().pc()==0,"reset release restarts original uploaded code at its reset vector");
    r.write(0x1f206700,1,0,0,255,tick+=100);
    require(r.vdMcuCpu().totalInstructions()>held,"original auxiliary execution resumes after release");
  }
}
void tspProvenanceTests() {
  {
    manta::AudioNoAck c;
    require(!c.arm(0x804f9098,0x1f225ab4,1,0x804f7a28,255),"no-ACK hypothesis cannot override known response data");
    require(!c.arm(0x804f9098,0x1f225ab5,1,0x804f7a28,0),"predicate cannot admit another hardware byte");
    require(c.arm(0x804f9098,0x1f225ab4,1,0x804f7a28,0),"only verified unknown acknowledgment can arm the scenario");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xe3,0xe3,0,true,1),"a placeholder matching E3 cannot force a native branch to take failure");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,1,true,1),"unknown expected constant still rejects decision");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,0,false,1),"foreign unknown origin cannot use acknowledgment predicate");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,0,true,101),"original finite retry bound cannot be exceeded");
    require(c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,0,true,1) && !c.pending(),"matching explicit predicate authorizes one comparison without a byte value or PC write");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,0,true,1),"predicate authorization cannot be reused");
    require(c.arm(0x804f9050,0x1f225cb8,1,0x804f7b10,0),"decoder load uses its separately verified acknowledgment source");
    require(!c.constrain(0x804f7a28,0x1452fff7,0xa5,0xe3,0,true,1),"decoder observation cannot authorize the SE comparison");
    require(c.constrain(0x804f7b10,0x1452fff7,0xa5,0xe3,0,true,100),"decoder no-ACK predicate preserves its own original finite retry bound");
    manta::PreservedBits bits;bits.seed(2,1,0,[](unsigned){return 0x1f225ab4;});
    require(bits.exactByteOrigin(2,0x1f225ab4) && !bits.exactByteOrigin(2,0x1f225ab5),"predicate source is an exact retained unknown byte origin");
    require(!bits.observe(0x1452fff7,[](unsigned){return 0;},[](uint32_t a){return a;},[](auto,auto,auto){}).empty() && bits.mask(2)==255,
      "ordinary provenance still rejects unknown comparisons and retains invalidity");
  }
  {
    manta::PreservedBits bits;uint32_t restored=0;
    std::array<uint32_t,32> regs{};regs[2]=0xa5;regs[4]=0xbf225dd8;
    auto observe=[&](uint32_t word){return bits.observe(word,[&](unsigned r){return regs[r];},
      [](uint32_t address){return address;},[&](auto,auto,uint32_t mask){restored=mask;});};
    bits.seed(2,1,0,[](unsigned){return 0x1f225dd8;});
    require(observe(0x30420020).empty() && bits.mask(2)==0x20,"original mode ANDI retains only unknown bit 5");
    regs[2]&=0x20;
    require(observe(0x02021025).empty() && bits.mask(2)==0x20,"original mode OR with known zero preserves bit identity");
    require(observe(0x304600ff).empty() && bits.mask(6)==0x20,"original byte mask does not make mode bit 5 known");
    require(observe(0xa0860000).empty() && restored==0x20,"unknown mode bit may return only to its same register bit");
    regs[4]=0xbf225dd9;
    require(!observe(0xa0860000).empty(),"copying retained mode bit into another byte must stop");
  }
  {
    manta::PreservedBits bits;uint32_t restored=0;std::array<uint32_t,32> regs{};
    regs[6]=0xa5a4;regs[7]=0xbf225a30;
    auto observe=[&](uint32_t word){return bits.observe(word,[&](unsigned r){return regs[r];},
      [](uint32_t address){return address;},[&](auto,auto,uint32_t mask){restored=mask;});};
    bits.seed(6,2,1,[](unsigned b){return 0x1f225a30+b;});
    require(observe(0x30c6fffe).empty() && bits.mask(6)==0xfffe,"original word selector clears bit0 without inventing other fields");
    require(observe(0xa4e60000).empty() && restored==0xfffe,"word selector restore retains both original byte origins");
    require(!observe(0x14c00001).empty(),"a selector observation still cannot drive an unknown branch");
  }
  {
    manta::PreservedBits bits;uint32_t restored=0;std::array<uint32_t,32> regs{};
    regs[2]=0xa5a5;regs[3]=0xbf225c40;
    auto observe=[&](uint32_t word){return bits.observe(word,[&](unsigned r){return regs[r];},
      [](uint32_t address){return address;},[&](auto,auto,uint32_t mask){restored=mask;});};
    bits.seed(2,2,0,[](unsigned b){return 0x1f225c40+b;});
    require(observe(0x305000ff).empty() && bits.mask(16)==255,"original standard-command caller discards unknown upper word byte");
    regs[16]=0xa5;
    require(observe(0x02001021).empty() && observe(0x3051000f).empty() && bits.mask(17)==15,"original return and low-nibble mask retain exact command origins");
    regs[17]=5;
    require(observe(0x36250010).empty() && bits.mask(5)==15,"standard-command high-nibble change does not invent retained low bits");
    require(observe(0xa0650000).empty() && restored==15,"standard-command unknown nibble returns only to the same command byte");
    require(!observe(0x16200001).empty(),"unknown standard-command bits cannot select a branch");
  }
  {
    manta::AudioIdma p;
    require(!p.read(0x18) && !p.statusKnown(),"IDMA never invents reset data or completion");
    rejects([&]{p.data(0,1);});
    p.address(false,0x1234);p.data(0,0x34);p.data(1,0x56);p.data(0,0x12);
    require(p.wordWrites==0 && !p.statusKnown(),"partial IDMA port word cannot signal completed transfer");
    p.data(1,0);
    require(p.wordWrites==1 && p.statusKnown()==0x10,"four original upload phases initialize a real 24-bit word");
    p.address(true,0x1234);p.command(8,255);
    require(p.read(0x1c)==0x12 && p.read(0x18)==0x34 && p.read(0x19)==0x56,
      "IDMA readback lane order matches original low/middle/high verification routine");
    p.command(8,255);
    require(!p.read(0x18) && p.wordReads==2,"auto-incremented IDMA read of unwritten word remains unknown");
    p.address(false,0x1234);rejects([&]{p.data(1,0);});
    p.data(0,1);rejects([&]{p.address(false,0x4321);});rejects([&]{p.invalidateAddress(false);});
    manta::Riu r;r.candidateAudioIdma=true;r.event=[](auto,auto,auto,auto,auto){};
    uint64_t tick=0;auto w=[&](unsigned off,unsigned n,unsigned v){r.write(0x1f225a00+off,n,v,0,n==1?255:65535,++tick);};
    w(8,2,0x21);w(4,1,0x34);w(5,1,0x56);w(4,1,0x12);w(5,1,0);w(0x10,2,0x21);w(0,1,8);
    require(r.read(0x1f225a00,1,0,8,++tick)==0,"IDMA request completion replaces a software request latch with transfer state");
    require(r.read(0x1f225a1c,1,0,255,++tick)==0x12 && r.read(0x1f225a18,1,0,255,++tick)==0x34 && r.read(0x1f225a19,1,0,255,++tick)==0x56,
      "RIU IDMA readback returns bytes actually transferred through original MMIO ports");
    require(r.knownBits(0x1f225ab4,1)==0 && r.knownBits(0x1f225c18,1)==0,
      "IDMA transfers cannot produce DSP2 alive or another DSP's memory contents");
    manta::Riu bytes;bytes.candidateAudioIdma=true;bytes.event=[](auto,auto,auto,auto,auto){};
    auto bw=[&](unsigned off,unsigned n,unsigned v){bytes.write(0x1f225c00+off,n,v,0x804f9230,n==1?255:65535,++tick);};
    bw(8,1,0x1a);rejects([&]{bw(4,1,0xcd);});
    require(bytes.audioIdma(1).wordWrites==0,"partial byte address cannot borrow an unknown high address byte");
    bw(9,1,0x40);bw(4,1,0xcd);bw(5,1,0xab);bw(4,1,0xef);bw(5,1,0);
    bw(0x10,1,0x1a);bw(0x11,1,0x40);bw(0,1,8);
    require(bytes.read(0x1f225c1c,1,0,255,++tick)==0xef && bytes.read(0x1f225c18,1,0,255,++tick)==0xcd && bytes.read(0x1f225c19,1,0,255,++tick)==0xab,
      "original byte-address sequence retains the 4000 memory-selection bit and actual word payload");
    bw(0x10,2,0x1a);bw(0,1,8);
    require(bytes.knownBits(0x1f225c18,1)==0,"encoded data-bank write cannot overwrite an uninitialized program-bank address");
    manta::Riu guarded;guarded.candidateAudioIdma=true;guarded.symbolicInit=true;guarded.event=[](auto,auto,auto,auto,auto){};
    auto gw=[&](unsigned off,unsigned n,unsigned v){guarded.write(0x1f225a00+off,n,v,0,n==1?255:65535,++tick);};
    guarded.prepareRmw(0x1f225a00,3);gw(0,1,3);gw(8,2,0x11ff);
    gw(4,1,0x88);gw(5,1,0x77);gw(4,1,0x99);gw(5,1,0);gw(0x10,2,0x11ff);
    guarded.prepareRmw(0x1f225a00,8);
    require(guarded.read(0x1f225a00,1,0x804f92a8,255,++tick)==3 && guarded.stopReason.empty(),
      "live IDMA status preserves the original same-origin RMW of unrelated unknown fields");
    gw(0,1,11);guarded.prepareMaskedRead(0x1f225a00,8);
    require((guarded.read(0x1f225a00,1,0x804f9098,255,++tick)&8)==0 && guarded.stopReason.empty(),
      "original byte-verified destructive mask consumes only the actual IDMA completion bit");
    require(guarded.knownBits(0x1f225a00,1)!=255 && guarded.knownBits(0x1f225ab4,1)==0,
      "IDMA command completion leaves unrelated unknown status fields and DSP alive invalid");
  }
  for(bool suspend:{false,true}){
    manta::Riu r;r.event=[](auto,auto,auto,auto,auto){};r.candidateAvdMcuProgram=true;
    r.exploratoryAvdMcuStartup=true;r.exploratoryAvdSuspendedBoundary=suspend;
    uint64_t tick=0;auto w=[&](unsigned off,unsigned v){r.write(0x1f2068c0+off,1,v,0,255,++tick);};
    w(1,0);w(5,0x51);w(8,0);w(9,0);
    // Actual AVD reset, stack and first transaction instructions, arranged
    // contiguously here; full original image is exercised separately.
    for(auto v:{0x02,0x00,0x03,0x75,0x81,0xac,0x90,0xc0,0x08,0x74,0x55,0xf0})w(0x18,v);
    w(5,0);bool failed=false;try{w(1,1);}catch(const manta::VdHardwareBoundary&){failed=true;}
    require(failed!=suspend && r.avdMcuSuspended()==suspend,"only explicit AVD suspension continues an unresolved auxiliary transaction");
    require(r.avdMcuCpu().totalInstructions()==4 && r.avdMcuCpu().pc()==11 && !r.avdMcuCpu().running(),
      "original instruction core retains AVD at actual MOVX boundary without producing a ready reply");
    require(r.knownBits(0x1f225ab4,1)==0 && r.knownBits(0x1f206ba8,1)==0,
      "AVD startup cannot manufacture audio DSP2 readiness or video status");
  }
  {
    manta::AvdMcuProgram avd;
    require(!avd.readable(),"AVD readback is unknown until original programming");
    rejects([&]{avd.read();});
    rejects([&]{avd.write(1,0,0);});
    avd.write(1,0,1);avd.write(5,0x50,0xf0);avd.write(8,0,255);avd.write(9,0,255);
    rejects([&]{avd.write(0x18,0x12,255);});
    avd.write(5,0x5d,0xf1); // unrelated preserved fields must not choose mode
    for(auto b:{0x02,0x00,0x03,0x75,0x81,0xac})avd.write(0x18,uint8_t(b),255);
    avd.write(8,0,255);avd.write(9,0,255);
    for(auto b:{0x02,0x00,0x03,0x75,0x81,0xac})require(avd.read()==b,"AVD readback returns real uploaded byte and advances cursor");
    require(avd.writes==6 && avd.reads==6 && !avd.readable(),"AVD never reads fabricated zero code past the guest upload");
    avd.write(5,0,0xf1);require(avd.write(1,1,1) && avd.startRequests==1,"AVD release records request without any fabricated ready state");
    require(!avd.readable(),"execution mode cannot use the programming readback port");
    manta::Riu bus;bus.candidateAvdMcuProgram=true;bus.event=[](auto,auto,auto,auto,auto){};
    require(bus.knownBits(0x1f2068e0,1)==0 && bus.knownBits(0x1f2068c1,1)==0,"AVD candidate does not invent hardware reset bytes");
    rejects([&]{bus.write(0x1f2068e0,1,0,0,255,1);});
  }
  {
    manta::Riu ci;ci.event=[](auto,auto,auto,auto,auto){};
    require(ci.knownBits(0x1f206891,1)==0,"strict CI detection has no invented absent-card default");
    ci.configureExploratoryCiDetectHigh();
    require(ci.knownBits(0x1f206891,1)==4 && ci.read(0x1f206891,1,0,4,1)==4,
      "explicit external CI line defines only the original driver's bit2");
    require(ci.ciDetectReads==1 && ci.knownBits(0x1f20689d,1)==0,
      "CI input sample cannot validate interrupt status or complete a card transaction");
    rejects([&]{ci.write(0x1f206891,1,0,0,255,2);});
    manta::PreservedBits input;std::array<uint32_t,32> registers{};registers[4]=0xa5;
    input.seed(4,1,4,[](unsigned){return 0x1f206891;});
    auto step=[&](uint32_t w){return input.observe(w,[&](unsigned r){return registers[r];},
      [](uint32_t a){return a;},[](uint32_t,unsigned,uint32_t){});};
    require(step(0x10800001).empty() && input.mask(4)==0xfb,
      "known asserted CI bit proves the byte nonzero without validating adjacent input bits");
    registers[5]=4;
    require(!step(0x10850001).empty(),"CI comparison whose outcome depends on adjacent unknown bits still rejects");
    require(step(0x30840004).empty() && input.mask(4)==0,
      "original CI ANDI bit2 discards all unrelated invalid input fields");
  }
  {
    manta::PreservedBits discarded;std::array<uint32_t,32> registers{};
    registers[16]=0x8072d918;registers[29]=0x8072d908;registers[2]=0xa5;
    auto observe=[&](uint32_t w){return discarded.observe(w,[&](unsigned r){return registers[r];},
      [](uint32_t a){return a;},[](uint32_t,unsigned,uint32_t){});};
    discarded.seed(2,1,0,[](unsigned){return 0x1f2068a0;});
    require(observe(0xa2020000).empty() && discarded.memoryUnknown(0x8072d918),
      "original PCMCIA read wrapper may copy an invalid byte to RAM without validating hardware state");
    require(!observe(0x10400001).empty(),"a changed PCMCIA caller cannot branch on the deferred unknown byte");
    require(observe(0x24020051).empty() && discarded.mask(2)==0,
      "original caller replaces its temporary with immediate 0x51, independent of the pre-read");
    registers[2]=0x51;
    require(observe(0xa3a20010).empty() && !discarded.memoryUnknown(0x8072d918),
      "original overwrite removes provenance only from the overwritten stack byte");
  }
  manta::PreservedBits p;std::array<uint32_t,32> regs{};
  regs[8]=0x1f202a00;regs[2]=0xa5a5;unsigned restores=0;uint32_t retained=0;
  auto step=[&](uint32_t word) {return p.observe(word,[&](unsigned r){return regs[r];},
      [](uint32_t a){return a;},[&](uint32_t,unsigned,uint32_t mask){++restores;retained=mask;});};
  p.seed(2,2,0,[](unsigned b){return 0x1f202bfc+b;});
  require(step(0x3042fff7).empty() && p.mask(2)==0xfff7,
      "stock TSP route clears only its explicit configuration bit; untouched mask fields remain unknown");
  regs[2]&=0xfff7;
  require(step(0xa50201fc).empty() && restores==1 && retained==0xfff7,
      "stock TSP high-halfword restore preserves the exact original byte/bit identities");
  require(!step(0xa50200fc).empty(),"TSP unknown IRQ-mask bits cannot be copied into command-queue status");
  p.seed(2,2,0,[](unsigned b){return 0x1f202afc+b;});regs[2]=0xa5a5;
  require(step(0x3042001f).empty() && p.mask(2)==31,
      "original TSP CmdQ_CmdCount mask does not manufacture known occupancy");
  regs[2]&=31; // match the ANDI which the production CPU actually executes
  require(!step(0x14400001).empty(),"original TSP nonempty-queue poll still rejects unknown occupancy");
  p.seed(2,2,31,[](unsigned b){return 0x1f202afc+b;});regs[2]=0xa5a0;
  require(step(0x3042001f).empty() && p.mask(2)==0,
      "known queue-count bits may discard unrelated unknown status fields");
  regs[2]=0;
  require(step(0x14400001).empty(),"TSP branch can consume only independently known queue-count bits");
}
void tspQueueTests() {
  manta::TspQueue q;
  rejects([&]{q.count();});
  require(!q.control(0,0xffff) && !q.known(),"TSP reset polarity is never silently assumed");
  q.enableExploratoryResetLow();
  require(!q.control(0,0xfffd) && !q.known(),"unknown TSP reset bit cannot initialize occupancy");
  require(q.control(0,0xffff) && q.held() && q.count()==0 && q.resets()==1,
      "original engine reset assertion empties queue only in explicit reset experiment");
  require(q.status()==0x80,"TSP empty status derives from queue occupancy");
  rejects([&]{q.submit({0x1000,188,1,0});});
  require(q.control(2,0xffff) && !q.held() && q.releases()==1,"original guest releases TSP reset");
  q.submit({0x1000,188,1,10});
  require(q.count()==1 && q.status()==1 && q.submissions()==1,"actual file-in descriptor changes pending occupancy");
  rejects([&]{q.execute(1000000);});
  require(q.count()==1,"unimplemented TS DMA cannot disappear with virtual time");
  for(unsigned i=1;i<16;++i)q.submit({0x1000+i*188,188,1,10+i});
  require(q.count()==16 && q.status()==0x50,"TSP full flag and count derive from bounded FIFO");
  rejects([&]{q.submit({0x4000,188,1,100});});
  require(!q.commandReset(0x800,0xf7ff) && q.count()==16,"unknown command reset cannot discard work");
  require(q.commandReset(0x800,0xffff) && q.count()==0 && q.submissions()==16,
      "command reset discards pending work without claiming completion");
  constexpr uint32_t reader[]={0x946200fc,0x946300f8,0x03e00008,0x3042001f};
  require(manta::immediateReadMask(reader)==31,"stock strided TSP count reader consumes only low five bits through JR delay slot");
  constexpr uint32_t unsafe[]={0x946200fc,0x00401821,0x03e00008,0x3042001f};
  require(!manta::immediateReadMask(unsafe),"TSP count read proof rejects an intervening copy");
  constexpr uint32_t unsafeJump[]={0x949f00fc,0x03e00008,0x33ff001f};
  require(!manta::immediateReadMask(unsafeJump),"JR consumes its target before a delay-slot mask; unknown loaded RA cannot pass read proof");
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};r.symbolicInit=true;r.candidateTsp=true;
  require(r.knownBits(0x1f202afc,2)==0,"TSP candidate status initially remains unknown");
  r.configureExploratoryTspResetLow();r.write(0x1f202be8,2,0,0,0xffff,1);
  r.write(0x1f202be8,2,2,0,0xffff,2);
  require(r.tspQueueKnown() && r.tspResetReleases()==1 && r.knownBits(0x1f202afc,2)==0xdf,
      "guest RIU writes cause reset and count state, leaving unrelated bits invalid");
  r.prepareMaskedRead(0x1f202afc,31);
  require((r.read(0x1f202afc,2,0,0xffff,3)&31)==0,"guest count read observes reset FIFO rather than universal zero register");
}
void tspIndirectTests() {
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};r.symbolicInit=true;r.candidateTsp=true;
  r.configureExploratoryTspResetLow();r.write(0x1f202be8,2,0,0,0xffff,1);r.write(0x1f202be8,2,2,0,0xffff,2);
  auto select=[&](uint32_t target){r.write(0x1f202b24,2,(target>>1)&65535,0,0xffff,3);
    r.write(0x1f202b28,2,target>>17,0,0xffff,4);};
  auto command=[&](uint16_t v){r.write(0x1f202b20,2,v,0,0xffff,5);};
  select(0x420000);command(0x21);
  require(!r.tspStoredWordKnown(0x210000) && !r.tspStoredWordKnown(0x210001) &&
      !r.tspStoredWordKnown(0x218000),"read-log observer cannot classify absent/unaligned/outside internal storage as known");
  require(r.tspIndirectReads()==1 && r.knownBits(0x1f202b34,2)==0 && r.knownBits(0x1f202b38,2)==0,
      "original indirect read command retains unknown filter SRAM reset contents");
  require(r.controlByteKey(0x1f202b34)==r.preservedStoreKey(0x1f202b2c) &&
      r.controlByteKey(0x1f202b38)==r.preservedStoreKey(0x1f202b30),
      "separate TSP read/write ports share exact internal register bit provenance");
  r.write(0x1f202b2c,2,0x5678,0,0xffff,6);r.write(0x1f202b30,2,0x1234,0,0xffff,7);
  command(0x21);require(r.knownBits(0x1f202b34,2)==0,"data staging without a write command cannot change filter SRAM");
  command(0x23);command(0x21);
  auto originalReads=r.tspIndirectReads(),cpuReads=r.tspCpuReads();
  require(r.tspStoredWordKnown(0x210000) && r.tspIndirectReads()==originalReads && r.tspCpuReads()==cpuReads,
      "known-state trace inspection uses actual uploaded metadata without executing a device read or changing counters");
  require(r.knownBits(0x1f202b34,2)==65535 && r.knownBits(0x1f202b38,2)==65535 &&
      r.read(0x1f202b34,2,0,0xffff,8)==0x5678 && r.read(0x1f202b38,2,0,0xffff,9)==0x1234,
      "original address/data/write/read sequence has actual guest-visible storage effects");
  select(0x420008);command(0x21);
  require(r.knownBits(0x1f202b34,2)==0,"indirect filter words retain independent undefined state");
  auto oldKey=r.controlByteKey(0x1f202b34);select(0x420010);
  require(r.controlByteKey(0x1f202b34)==oldKey && r.preservedStoreKey(0x1f202b2c)!=oldKey,
      "read latch preserves its producer address; a new destination cannot inherit invalid source bits");
  select(0x420008);
  r.preparePreservedWrite(0x1f202b2c,2,0xfffe);r.write(0x1f202b2c,2,0xa5a4,0,0xffff,10);
  r.preparePreservedWrite(0x1f202b30,2,0xffff);r.write(0x1f202b30,2,0xa5a5,0,0xffff,11);
  command(0x23);command(0x21);
  require(r.knownBits(0x1f202b34,2)==1 && r.knownBits(0x1f202b38,2)==0,
      "masked guest filter update defines only its explicit bit, not poison or prior write-buffer bits");
  require(!r.tspStoredWordKnown(0x210004),"partial valid internal bits cannot be classified as a fully known internal word");
  require(r.tspStoredWordKnownMask(0x210004)==1,"changes-only logging retains the exact partial validity mask without filling unknown bits");
  rejects([&]{command(0x25);});
  select(0x430000);rejects([&]{command(0x21);});
  select(0x420002);rejects([&]{command(0x21);});
  r.write(0x1f202b24,2,0,0,0xffff,12);r.write(0x1f202b28,2,0x8000,0,0xffff,13);
  rejects([&]{command(0x21);});
  select(0x420660);r.write(0x1f202b2c,2,0,0,0xffff,14);r.write(0x1f202b30,2,0x100,0,0xffff,15);
  command(0x23);
  require(!r.tspStoredWordKnown(0x210330) && !r.tspStoredWordKnown(0x21033c),
      "generic stored-register log filter cannot hide command/reply mailbox observations");
  require(r.read(0x1f202b00,2,0,0xffff,16)==0 && r.read(0x1f202b04,2,0,0xffff,17)==0x100,
      "OR command mailbox, MIPS direct access and indirect address share one backing");
  r.write(0x1f202c00,2,0x5678,0,0xffff,18);r.write(0x1f202c04,2,0x1234,0,0xffff,19);
  select(0x420678);command(0x21);
  require(r.read(0x1f202b34,2,0,0xffff,20)==0x5678 && r.read(0x1f202b38,2,0,0xffff,21)==0x1234,
      "response mailbox read through indirect port observes real direct writes, not an independent copy");
}
void tspDownloadTests() {
  manta::TspDownload d;std::vector<uint8_t> source(64);
  for(unsigned i=0;i<source.size();++i)source[i]=uint8_t(i^0x5a);
  require(!d.known() && !d.done(),"TSP download completion starts unknown");
  rejects([&]{d.byte(0);});
  d.start(source,10);
  require(d.known() && d.pending() && !d.done(),"actual TSP transfer starts busy, without pretending SRAM is initialized");
  require(!d.advance(13),"TSP programmed transfer cannot finish before its deterministic deadline");
  rejects([&]{d.byte(0);});
  require(d.advance(14) && d.done() && d.completions()==1 && d.transferred()==source,
      "TSP completion follows an actual byte-for-byte transfer into QMEM");
  require(d.byte(63)==source[63],"download modifies real internal SRAM bytes");
  rejects([&]{d.byte(64);});
  rejects([&]{d.start(std::vector<uint8_t>(0x4004),15);});
  rejects([&]{d.start(std::vector<uint8_t>(3),15);});
  manta::Riu r;r.event=[](auto&,auto,auto,auto,auto&){};r.symbolicInit=true;r.candidateTsp=true;
  r.configureExploratoryTspResetLow();r.write(0x1f202be8,2,0,0,0xffff,0);r.write(0x1f202be8,2,2,0,0xffff,1);
  r.dramRead=[&](uint32_t address,size_t length){require(address==0x72db00 && length==64,
      "original low/high descriptor fields decode real source and four-byte transfer size");return source;};
  r.write(0x1f202be0,2,0xcb6c,0,0xffff,2);r.write(0x1f202be4,2,16,0,0xffff,3);
  r.write(0x1f202c1c,2,1,0,0xffff,4);r.write(0x1f202be8,2,6,0,0xffff,10);
  require(r.tspDownloadRequests()==1 && r.tspDownloadPending(),"original RIU start edge triggers programmed firmware DMA");
  r.write(0x1f202be8,2,14,0,0xffff,11);
  r.prepareMaskedRead(0x1f202be8,8);
  require(!(r.read(0x1f202be8,2,0,0xffff,12)&8),"software cannot manufacture asynchronous download completion");
  r.prepareMaskedRead(0x1f202be8,8);
  require((r.read(0x1f202be8,2,0,0xffff,14)&8) && r.tspQmemByte(63)==source[63],
      "original poll sees done only after the actual SRAM effect");
  rejects([&]{r.write(0x1f202be8,2,3,0,0xffff,15);});
}
void tspMappingBurstTests(){
  unsigned firstVisits=0;
  manta::Riu r;r.event=[&](const auto& kind,auto,auto,auto,const auto&){if(kind=="or1k_instruction")++firstVisits;};r.symbolicInit=true;r.candidateTsp=true;
  r.configureExploratoryTspResetLow();r.configureExploratoryTspCpu();r.configureOrTraceBudget(true,1000,4);
  std::vector<uint8_t> code(0x200);const uint32_t words[]={0x18000000,0x9c200123,0,0x15000000};
  for(unsigned i=0;i<4;++i)for(unsigned b=0;b<4;++b)code[0x100+4*i+b]=uint8_t(words[i]>>(8*b));
  r.dramRead=[&](uint32_t,size_t n){require(n==code.size(),"mapping fixture DMA copies actual instruction bytes");return code;};
  uint64_t tick=0;auto w=[&](unsigned off,unsigned v){r.write(0x1f202b00+off,2,v,0,65535,tick);};
  w(0xe8,0);w(0xe8,2);
  for(unsigned off:{0x88,0x8c,0x98,0x9c})w(off,0);
  for(unsigned off:{0x90,0xa0})w(off,0xc000);
  for(unsigned off:{0x94,0xa4})w(off,0xffff);
  w(0xe0,0xcb6c);w(0xe4,unsigned(code.size()/4));r.write(0x1f202c1c,2,1,0,65535,tick);
  tick=100;w(0xe8,6);tick=132;r.read(0x1f202be8,2,0,8,tick);tick=133;w(0xe8,3);
  tick=173;r.read(0x1f202be8,2,0,1,tick);
  require(r.tspCpuInstructions()==10 && r.tspCpuPc()==0x108,"actual OR instructions and delay-slot loop execute within a burst");
  require(r.orVisited()==4 && firstVisits==4,"first-visit bitmap logs four distinct original PCs while all ten instructions execute");
  w(0x88,0x4000);tick=177;
  rejects([&]{r.read(0x1f202be8,2,0,1,tick);});
  require(r.tspCpuInstructions()==10 && r.tspCpuPc()==0x108,"mapping changes between bursts cannot reuse stale executable translations");
  w(0x88,0);r.preparePreservedWrite(0x1f202b90,2,65535);w(0x90,0xc000);tick=181;
  rejects([&]{r.read(0x1f202be8,2,0,1,tick);});
  require(r.knownBits(0x1f202b90,2)==0 && r.tspCpuInstructions()==10,"unchanged map values with invalidated provenance still block fetch");
}
void pmPowerInputTests(){
  manta::PmPowerGood p;
  require(!p.configured() && p.sample().known==0,"PM power-good has no implicit reset value");
  p.input(false);require(p.sample().known==7 && (p.sample().value&7)==0 && p.transitions==1,"explicit absent power retains a nonready aggregate input");
  for(unsigned i=0;i<100;++i)p.sample();
  require(p.transitions==1,"PM power-good cannot become ready by polling");
  p.input(false);require(p.transitions==1,"repeated identical power input is not a transition");
  p.input(true);require((p.sample().value&7)==7 && p.sample().known==7 && p.transitions==2,"explicit stable-power hypothesis changes only the aggregate predicate");
  p.disconnect();require(!p.configured() && p.sample().known==0 && p.transitions==3,"disconnect invalidates unknown detector wiring");
  manta::Riu r;r.symbolicInit=true;r.event=[](auto,auto,auto,auto,auto){};
  require(r.knownBits(0x1f001d84,2)==0,"strict RIU power-good and unused high byte stay invalid");
  r.configureExploratoryPmPowerGood(false);
  require(r.knownBits(0x1f001d84,2)==7 && (r.read(0x1f001d84,2,0,7,1)&7)==0,"RIU word read exposes only configured low input bits");
  r.configureExploratoryPmPowerGood(true);
  require((r.read(0x1f001d84,1,0,7,2)&7)==7 && r.knownBits(0x1f001d85,1)==0,"RIU byte and word paths agree without inventing unused high status");
}
void pmActivityScenarioTests(){
  for(unsigned value=0;value<16;++value){
    manta::PmActivityScenario p;
    require(p.arm(0x80404bf4,0x1f001cd1,1,0x8046a198,0),"symbolic PM predicate arms only an invalid original observation");
    require(p.xorPredicate(0x80404bfc,0x38420008,value,true)==(value!=8),"nonresume assumption cannot override an inconsistent representative or manufacture marker eight");
    if(value!=8){
      require(p.resolvePredicate(0x80404c04,0x2c420001,value^8,true),"exact original SLTIU resolves only the constrained boolean");
      require(p.observeBranch(0x8046a198,0x1040fec7,0,0),"original branch result must already be zero and valid, no host register assignment");
    }
    require(!p.arm(0x80404bf4,0x1f001cd1,1,0x8046a198,0),"one predicate assumption never becomes a permanent PM register default");
  }
  manta::PmActivityScenario bad;
  require(!bad.arm(0x80404bf4,0x1f001cd1,1,0,0) && !bad.arm(0x80404bf4,0x1f001cd1,1,0x8046a198,15),"wrong caller or real known status cannot enter symbolic scenario");
  manta::PreservedBits bits;
  bits.seed(2,1,0,[](unsigned){return 0x1f001cd1u;});
  auto reg=[](unsigned){return 0u;};auto key=[](uint32_t a){return a;};
  bits.observe(0x3042000f,reg,key,[](auto,auto,auto){});
  require(bits.exactFieldOrigin(2,0x1f001cd1,15),"masked predicate retains exact low-field identities");
  bits.scenarioXorImmediate(2,8);
  require(bits.exactFieldOrigin(2,0x1f001cd1,15,8) && !bits.exactFieldOrigin(2,0x1f001cd1,15),"symbolic XOR must retain inverted-bit provenance");
  auto storeRegs=[](unsigned r){return r==3?0xbf001cd1u:0u;};
  require(!bits.observe(0xa0620000,storeRegs,key,[](auto,auto,auto){}).empty(),"inverted unknown bit cannot be laundered as a same-origin device write");
  for(bool explore:{false,true}){
    manta::Engine e;e.map(0x00404000,0x1000);e.map(0x00469000,0x2000);e.map(0x006b8000,0x1000);
    const uint32_t helper[]={0x3c02806c,0x8c438d0c,0x90621cd1,0x3042000f,0x38420008,0x03e00008,0x2c420001};
    const uint32_t caller[]={0x0c1012fb,0,0x1040fec7,0};const uint32_t base=0xbf000000;
    e.write(0x00404bec,helper,sizeof(helper));e.write(0x0046a190,caller,sizeof(caller));e.write(0x006b8d0c,&base,4);
    struct Context{manta::Engine* e;manta::PreservedBits bits;manta::PmActivityScenario scenario;bool explore,guarded=false;unsigned reads=0;};
    Context c{&e,{}, {},explore};
    auto read=+[](uc_engine*,uint64_t off,unsigned n,void* data)->uint64_t{
      auto& c=*static_cast<Context*>(data);++c.reads;
      if(off!=0xcd1 || n!=1 || !c.scenario.arm(c.e->reg(UC_MIPS_REG_PC),0x1f001cd1,n,c.e->reg(UC_MIPS_REG_RA),0))
        {c.guarded=true;uc_emu_stop(c.e->handle);}
      c.bits.seed(2,1,0,[](unsigned){return 0x1f001cd1u;});
      return 0xa5; // INVALID representative, not a physical PM value
    };
    manta::check(uc_mmio_map(e.handle,0x1f001000,0x1000,read,&c,nullptr,nullptr));
    uc_hook hook;manta::check(uc_hook_add(e.handle,&hook,UC_HOOK_CODE,reinterpret_cast<void*>(+[](uc_engine* cpu,uint64_t pc,uint32_t,void* data){
      auto& c=*static_cast<Context*>(data);
      if(pc==0x80469cb8){uc_emu_stop(cpu);return;}
      uint32_t w=0;c.e->read(uint32_t(pc)&0x1fffffff,&w,4);
      auto reg=[&](unsigned r){return c.e->reg(UC_MIPS_REG_0+int(r));};
      bool symbolic=c.explore && c.scenario.xorPredicate(uint32_t(pc),w,reg(2),c.bits.exactFieldOrigin(2,0x1f001cd1,15));
      if(symbolic)c.bits.scenarioXorImmediate(2,8);
      auto why=symbolic?std::string{}:c.bits.observe(w,reg,[](uint32_t a){return a;},[](auto,auto,auto){});
      if(!why.empty() && c.explore && c.scenario.resolvePredicate(uint32_t(pc),w,reg(2),c.bits.exactFieldOrigin(2,0x1f001cd1,15,8))){
        c.bits.seed(2,4,0xffffffffu,[](unsigned){return 0u;});why.clear();
      }
      if(!why.empty()){c.guarded=true;uc_emu_stop(cpu);return;}
      c.scenario.observeBranch(uint32_t(pc),w,reg(2),c.bits.mask(2));
    }),&c,1,0));
    manta::check(e.run(0x8046a190,100));
    require(c.reads==1,"original MIPS getter performs its own byte load through the MMIO engine");
    require(explore?(!c.guarded && e.reg(UC_MIPS_REG_PC)==0x80469cb8 && e.reg(UC_MIPS_REG_2)==0 && c.scenario.branches==1):
      (c.guarded && e.reg(UC_MIPS_REG_PC)==0x80404bfc),"original JAL/JR delay-slot SLTIU/BEQ execute unchanged; only explicitly constrained boolean provenance passes");
    std::array<uint32_t,7> after{};e.read(0x00404bec,after.data(),sizeof(helper));
    require(std::equal(after.begin(),after.end(),std::begin(helper)),"symbolic branch analysis never patches guest instruction memory");
  }
}
void r8051UartConfigTests(){
  auto run=[&](const std::vector<uint8_t>& code,bool candidate,auto inspect){
    manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
    for(auto byte:code)p.write(0x18,byte);
    manta::VdMcuCpu c;c.candidateR8051Uart=candidate;unsigned writes=0;
    c.specialRegister=[&](uint8_t,bool write,uint8_t){if(write)++writes;};
    std::string reason;try{c.startup(p,100);}catch(const std::exception& e){reason=e.what();}
    require(!reason.empty(),"UART configuration fixture must reach a genuine unsupported boundary");
    inspect(c,reason,writes);
  };
  run({0x43,0xd8,0x80,0xe5,0xd8,0x54,0x80,0x60,2,0x78,0x2a,0x90,0x2f,0,0xe0},true,[&](auto& c,const auto& reason,unsigned writes){
    require(c.instructions()==6 && c.pc()==14 && reason.find("XDATA read unsupported")!=std::string::npos,"original UART baud select ORL preserves unknown bits, masking permits a known decision");
    require(writes==1 && c.accumulatorKnown()==255 && c.accumulator()==0x80,"guest-written baud selector is observable without a guessed SFR reset");
  });
  run({0x43,0xd8,0x80,0xe5,0xd8,0x60,0},true,[&](auto& c,const auto& reason,unsigned){
    require(c.pc()==5 && c.accumulatorKnown()==0x80 && reason.find("unknown XDATA bits")!=std::string::npos,"unmasked reserved UART control bits cannot affect a branch");
  });
  run({0x75,0xaa,0xf6,0xe5,0xaa,0x90,0x2f,0,0xe0},true,[&](auto& c,const auto& reason,unsigned writes){
    require(c.pc()==8 && writes==1 && c.accumulator()==0xf6 && reason.find("XDATA read unsupported")!=std::string::npos,"baud low register reads its actual programmed value");
  });
  run({0x75,0xba,3,0xe5,0xba,0x54,3,0x60,0,0x90,0x2f,0,0xe0},true,[&](auto& c,const auto& reason,unsigned){
    require(c.pc()==12 && c.accumulator()==3 && reason.find("XDATA read unsupported")!=std::string::npos,"ten-bit reload high field requires masking reserved bits");
  });
  run({0x75,0x98,0x50,0x75,0x99,0x2a,0x30,0x99,0xfd},true,[&](auto& c,const auto& reason,unsigned writes){
    require(c.pc()==6 && writes==1 && reason.find("TX completion")!=std::string::npos,"guest UART transmit queues a byte; unsupported baud-clock completion is not fabricated");
  });
  run({0x43,0xd8,0x80},false,[&](auto& c,const auto& reason,unsigned){
    require(c.instructions()==0 && reason.find("SFR read unsupported")!=std::string::npos,"unchanged CPU mode still rejects unmodeled R8051 extensions");
  });
}
void vifZeroInputTests(){
  using Model=manta::VifZeroInput;
  Model::Configuration valid={{{0x7f,0x7f},{5,0x0d},{0x1a,0x1a},{0x70,255},
    {0,3},{5,31},{31,31},{1,255},{0,255}}};
  Model strict;strict.update(valid,1000);
  require(strict.sample(0x1f224644).known==0 && strict.sample(0x1f22465d).known==0,
    "AGC configuration alone never defines external measurements in strict mode");
  rejects([&]{strict.configure(0);});
  for(auto delay:{1ull,100ull,4320000ull}){
    Model model;model.configure(delay);model.update(valid,10);
    require(!model.settled() && model.sample(0x1f224644).known==0,"ideal IF output invalid before explicit settling deadline");
    model.update(valid,10+delay-1);require(!model.settled(),"IF settling does not complete early");
    model.update(valid,10+delay);
    require(model.sample(0x1f224644).known==255 && model.sample(0x1f224644).value==0 &&
      model.sample(0x1f22465d).known==31 && model.sample(0x1f22465d).value==31,
      "ideal zero envelope produces only mean and guest-programmed PGA2 field after settling");
    require(model.sample(0x1f224650).known==0 && model.sample(0x1f224651).known==0 &&
      model.sample(0x1f22465c).known==0,"VGA and unrelated analog observations remain invalid");
    auto poison=valid;for(auto& field:poison)field.value^=uint8_t(~field.known);
    model.update(poison,11+delay);require(model.settled() && model.settlements()==1,"unknown preserved bits do not restart acquisition");
    auto disabled=valid;disabled[1].value&=0xfe;model.update(disabled,12+delay);
    require(!model.settled() && model.sample(0x1f224644).known==0,"AGC disable immediately invalidates ideal output");
    model.update(valid,13+delay);require(!model.settled(),"reenabling AGC requires new settling interval");
  }
  // Every required bit matters; no unknown clock/control/limit is silently
  // treated as a convenient reset. Gains are derived, not fixed to 31.
  constexpr std::array<uint8_t,9> required={0x7f,0x0d,0x1a,255,3,31,31,255,255};
  for(unsigned field=0;field<valid.size();++field)for(unsigned bit=0;bit<8;++bit)if(required[field]&(1<<bit)){
    Model model;model.configure(1);auto config=valid;config[field].known&=uint8_t(~(1<<bit));
    model.update(config,1);model.update(config,100);
    require(model.sample(0x1f224644).known==0 && model.sample(0x1f22465d).known==0,
      "unknown required VIF field cannot yield a synthetic observation");
  }
  for(unsigned maximum=0;maximum<32;++maximum){
    Model model;model.configure(1);auto config=valid;config[5].value=0;config[6].value=uint8_t(maximum);
    model.update(config,1);model.update(config,2);
    require(model.sample(0x1f22465d).value==maximum,"ideal PGA2 saturation follows all 32 programmed maximum codes");
  }
  Model model;model.configure(1);auto changed=valid;model.update(valid,1);model.update(valid,2);
  changed[4].value=2;model.update(changed,3);
  require(!model.settled(),"gain-override configuration invalidates autonomous gain hypothesis");
  changed=valid;changed[5].value=31;changed[6].value=30;model.update(changed,4);model.update(changed,100);
  require(!model.settled(),"inconsistent PGA limits never produce an observation");
  manta::Riu bus;bus.event=[](const auto&,auto,auto,auto,const auto&){};
  require(bus.knownBits(0x1f224644,1)==0 && bus.knownBits(0x1f224650,2)==0 &&
    bus.knownBits(0x1f22465d,1)==0,"default RIU retains all live VIF observations unknown");
  for(auto address:{0x1f224644u,0x1f224650u,0x1f224651u,0x1f22465du})
    rejects([&]{bus.write(address,1,0,0,255,1);});
  bus.configureExploratoryVifZeroInput(100);
  uint64_t tick=0;
  for(unsigned i=0;i<valid.size();++i)bus.write(Model::configuration[i],1,valid[i].value,0,255,++tick);
  bus.read(0x1f224644,1,0,255,tick+99);
  require(!bus.stopReason.empty() && bus.knownBits(0x1f224644,1)==0,"unsettled model still causes controlled RIU stop");
  bus.stopReason.clear();bus.read(0x1f224644,1,0,255,tick+100);
  require(bus.stopReason.empty() && bus.knownBits(0x1f22465d,1)==31 && bus.knownBits(0x1f224650,2)==0,
    "RIU publishes only the supported hypothetical fields, preserving invalid VGA and PGA high bits");
}
void avdBusTests(){
  {
    // CPU-level differential checks: real ADD/ADDC opcodes, uncertain bus
    // data and their later carry consumer. No peripheral sample is supplied.
    for(auto op:{0x24,0x34})for(auto mask:{0u,0x0fu,0x80u,0xf0u,255u}){
      bool exact=true;
      for(auto value:{0u,1u,0x39u,0x7fu,0xc0u,0xffu}){
        manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
        for(auto b:{0x90,0x11,0x00,0xe0,0xd3,op,0x39,0xf0,0x40,0})p.write(0x18,uint8_t(b));
        manta::VdMcuCpu cpu;cpu.symbolicCodeTables=true;manta::VdByte output{};
        cpu.busRead=[&](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{uint8_t(value),uint8_t(mask),a};};
        cpu.busWrite=[&](uint16_t,manta::VdByte v){output=v;return true;};cpu.resetProgram(p);
        while(cpu.instructions()<5)cpu.step();
        unsigned first=0,known=255,firstCarry=0;bool initialized=false,carryCertain=true;
        for(unsigned source=0;source<256;++source)if(((source^value)&mask)==0){
          auto sum=source+0x39+(op==0x34?1:0);
          if(!initialized){first=sum&255;firstCarry=sum>255;initialized=true;}
          else{known&=~(first^(sum&255));carryCertain &= firstCarry==unsigned(sum>255);}
        }
        exact &= output.value==uint8_t(value+0x39+(op==0x34?1:0)) && output.known==known && output.origin==0;
        bool rejected=false;try{while(cpu.instructions()<6)cpu.step();}catch(const manta::VdHardwareBoundary&){rejected=true;}
        exact &= rejected!=carryCertain;
      }
      require(exact,"symbolic ADD/ADDC executes original CPU arithmetic, preserves exactly common result bits and rejects an uncertain carry branch");
    }
    for(bool complete:{false,true}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0x00,0xe0,0x54,0x03,0x90,0x01,0x00,0x93,0x30,0xe0,0})p.write(0x18,uint8_t(b));
      p.write(8,0);p.write(9,1);for(auto b:{0xa0,0xaa,0xa5})p.write(0x18,uint8_t(b));if(complete)p.write(0x18,0xaf);
      manta::VdMcuCpu cpu;cpu.symbolicCodeTables=true;
      cpu.busRead=[](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{0,0,a};};cpu.resetProgram(p);
      while(cpu.instructions()<4)cpu.step();bool rejected=false;
      try{while(cpu.instructions()<5)cpu.step();}catch(const manta::VdHardwareBoundary&){rejected=true;}
      require(rejected!=complete,"MOVC considers every possible address and rejects even an unwritten alternative not selected by the representative byte");
      if(complete){
        require(cpu.accumulator()==0xa0 && cpu.accumulatorKnown()==0xf0 && cpu.accumulatorOrigin()==0,
          "MOVC marks only bits common to all uploaded table bytes known, with no retained-device identity");
        bool decision=false;try{while(cpu.instructions()<6)cpu.step();}catch(const manta::VdHardwareBoundary&){decision=true;}
        require(decision,"a real branch on uncertain ROM data remains a controlled boundary");
      }else require(cpu.accumulatorKnown()==0xfc && cpu.pc()==9,"failed alternative-code validation leaves the unexecuted MOVC and accumulator validity intact");
    }
    for(auto use:{0xe0,0xf0,0x73}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0,0xe0,0xf5,0x82,0xe4,use})p.write(0x18,uint8_t(b));
      manta::VdMcuCpu cpu;cpu.symbolicCodeTables=true;
      cpu.busRead=[](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{0,0,a};};cpu.resetProgram(p);
      bool stopped=false;try{for(unsigned i=0;i<100;++i)cpu.step();}catch(const manta::VdHardwareBoundary& e){stopped=std::string(e.what()).find("unknown DPTR used")!=std::string::npos;}
      require(stopped && cpu.instructions()==4 && cpu.pc()==7,"uncertain DPTR can never select XDATA reads, XDATA writes or instruction targets");
    }
    for(auto tail:std::vector<std::vector<uint8_t>>{{0xe5,0xd0},{0x85,0xd0,0x10},{0xa8,0xd0},
        {0xc0,0xd0},{0x30,0xd2,0},{0xe4,0x84},{0xe4,0x55,0xf0}}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0,0xe0,0x75,0xf0,8,0xa4})p.write(0x18,uint8_t(b));
      for(auto b:tail)p.write(0x18,b);
      manta::VdMcuCpu cpu;cpu.symbolicCodeTables=true;
      cpu.busRead=[](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{0,0,a};};cpu.resetProgram(p);
      bool rejected=false;std::string reason;try{for(unsigned i=0;i<100;++i)cpu.step();}catch(const manta::VdHardwareBoundary& e){
        reason=e.what();rejected=reason.find("unknown")!=std::string::npos;}
      if(!rejected)throw std::runtime_error("symbolic MUL guard tail="+std::to_string(tail[0])+" PC="+std::to_string(cpu.pc())+" reason="+reason);
      require(rejected,"uncertain multiply flags and B high product cannot leak through PSW copies, branches, DIV or unmodeled direct operations");
    }
    manta::AvdMcuBus bus;bus.symbolicTableConfiguration=true;std::map<uint32_t,manta::VdByte> bytes;
    bus.sharedWrite=[&](uint32_t a,manta::VdByte v){bytes[a]=v;};
    for(unsigned a=0x117c;a<=0x117f;++a)require(bus.write(uint16_t(a),{0xa5,0,0}) && bytes[*bus.sharedAddress(uint16_t(a))].known==0,
      "original CODE-derived configuration stores remain INVALID rather than inventing a selector or threshold");
    for(auto a:{0x11dc,0x11dd,0x11c7,0x11c8,0x113c,0x1200})require(!bus.write(uint16_t(a),{0,0,0}),
      "symbolic threshold stores cannot initialize commands, HTotal samples, raw counters or banked COMB data");
    require(bus.write(0x11d6,{0xa5,0,0x8077}) && bytes[0x1f206bac].known==0 &&
      !bus.write(0x11d6,{0,0,0}) && !bus.write(0x11c7,{0,0,0x8077}),
      "original timer-counter publication preserves unknown private RAM and cannot define HTotal or accept an unrelated source");
  }
  for(auto opcode:{0x43,0x53,0x63}){
    bool exact=true;
    for(unsigned value=0;value<256;++value)for(auto mask:{0u,0x1fu,0xe0u,255u}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0x4c,0xe0,0xff,opcode,0x07,int(mask),0xef,0xf0})p.write(0x18,uint8_t(b));
      manta::VdMcuCpu cpu;manta::VdByte written{};
      cpu.busRead=[&](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{uint8_t(value),0,a};};
      cpu.busWrite=[&](uint16_t,manta::VdByte v){written=v;return true;};cpu.resetProgram(p);
      while(cpu.instructions()<6)cpu.step();
      auto expected=opcode==0x43?(value|mask):opcode==0x53?(value&mask):(value^mask);
      auto known=opcode==0x43?mask:opcode==0x53?uint8_t(~mask):0;
      exact &= written.value==expected && written.known==known && written.origin==0x114c;
    }
    require(exact,"direct IRAM immediate masks preserve unknown low fields and origin while unrelated ACC remains invalid, for every source byte");
  }
  for(bool hypothesis:{false,true}){
    manta::Riu r;r.candidateAvdMcuProgram=true;r.exploratoryAvdHostResetEmpty=hypothesis;
    r.event=[](auto,auto,auto,auto,auto){};
    require(r.knownBits(0x1f206bb8,1)==0 && r.avdHostRequestLatchResets==0,
      "configuring reset-latch hypothesis cannot fabricate an initial command before an actual guest hold");
    r.write(0x1f2068c1,1,0,0,255,1);
    require(r.knownBits(0x1f206bb8,1)==(hypothesis?12u:0u) &&
      r.knownBits(0x1f206bb9,1)==0 && r.knownBits(0x1f206bbc,1)==0 && r.knownBits(0x1f206bbd,1)==0,
      "only explicit nonretained-latch hypothesis makes command bits 0C reset-empty; bank and payload remain invalid");
    if(!hypothesis)continue;
    r.write(0x1f206bb8,1,9,0,255,2);r.write(0x1f206bbd,1,0x5a,0,255,3);
    require(r.read(0x1f206bb8,1,0,255,4)==9 && r.read(0x1f206bb8,1,0,255,5)==9,
      "real pending request remains pending across repeated polls; no automatic completion");
    r.write(0x1f2068c1,1,1,0,255,6);
    require(r.read(0x1f206bb8,1,0,255,7)==9 && r.avdHostRequestLatchResets==1,
      "execution release is not a request reset or completion");
    r.write(0x1f206bb8,1,1,0,255,8);
    require(r.read(0x1f206bb8,1,0,255,9)==1,"only an actual MIPS command clear services the submitted write");
    r.write(0x1f2068c1,1,0,0,255,10);
    require(r.read(0x1f206bb8,1,0,255,11)==1 && r.read(0x1f206bbd,1,0,255,12)==0x5a &&
      r.avdHostRequestLatchResets==2,"later explicit reset retains written bank/data and never creates a completion marker");
    r.write(0x1f206bb8,1,9,0,255,13);r.write(0x1f2068c1,1,0,0,255,14);
    require(r.read(0x1f206bb8,1,0,255,15)==1 && r.read(0x1f206bbd,1,0,255,16)==0x5a &&
      r.avdHostRequestLatchResets==3,"conditional MCU hold aborts a pending latch without a DONE marker, payload change or executed service");
  }
  for(auto opcode:{0x03,0x23,0xc4,0x13,0x33}){
    bool exact=true;
    for(unsigned value=0;value<256;++value)for(auto known:{0u,1u,8u,128u,255u}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0x9c,0xe0,0xc3,opcode,0xf0,0x40,0})p.write(0x18,uint8_t(b));
      manta::VdMcuCpu cpu;manta::VdByte written{};
      cpu.busRead=[&](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{uint8_t(value),uint8_t(known),a};};
      cpu.busWrite=[&](uint16_t,manta::VdByte v){written=v;return true;};cpu.resetProgram(p);
      while(cpu.instructions()<5)cpu.step();
      auto permute=[&](unsigned v,bool validity){switch(opcode){
        case 0x03:return uint8_t((v>>1)|(v<<7));case 0x23:return uint8_t((v<<1)|(v>>7));
        case 0xc4:return uint8_t((v<<4)|(v>>4));case 0x13:return uint8_t((v>>1)|(validity?0x80:0));
        default:return uint8_t((v<<1)|(validity?1:0));}};
      exact &= written.value==permute(value,false) && written.known==permute(known,true);
      if(known!=255)exact &= written.origin==0; // permuted unknowns cannot masquerade as a same-byte RMW
      bool rejected=false;try{while(cpu.instructions()<6)cpu.step();}catch(const manta::VdHardwareBoundary&){rejected=true;}
      bool carryUnknown=(opcode==0x13 && !(known&1)) || (opcode==0x33 && !(known&0x80));
      exact &= rejected==carryUnknown;
    }
    require(exact,"all-byte rotate/swap validity matches executed CPU results; unknown carry cannot control JC; permuted origins cannot escape as a masked restore");
  }
  for(auto tail:std::vector<std::vector<uint8_t>>{{0xc0,0xd0,0xc3,0xd0,0xd0,0x50,0},
       {0xe4,0x34,0},{0xe4,0xa2,0xd7},{0xe4,0x92,0x00},{0xe4,0x24,0,0x50,0}}){
    manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
    for(auto b:{0x90,0x11,0x9c,0xe0,0xc3,0x13})p.write(0x18,uint8_t(b));
    for(auto b:tail)p.write(0x18,b);
    manta::VdMcuCpu cpu;cpu.busRead=[](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{0xa5,0,a};};
    cpu.resetProgram(p);bool unknownCarry=false;
    try{for(unsigned i=0;i<100;++i)cpu.step();}catch(const manta::VdHardwareBoundary& e){unknownCarry=std::string(e.what()).find("unknown carry")!=std::string::npos;}
    require(unknownCarry==(tail[1]!=0x24),"unknown carry survives PSW push/pop and ACC overwrite; ADD replaces it but ADC/PSW/bit writes cannot consume it");
  }
  {
    // Architectural scheduling fixture, not a TV boot or injected firmware
    // reply: the MCU itself executes MOV DPTR / MOV A / MOVX / SJMP.
    manta::Riu r;r.event=[](auto,auto,auto,auto,auto){};r.symbolicInit=true;
    r.candidateAvdMcuProgram=true;r.exploratoryAvdMcuStartup=true;r.exploratoryAvdSharedRun=true;
    uint64_t tick=0;auto w=[&](unsigned off,unsigned v,uint64_t at){r.write(0x1f2068c0+off,1,v,0,255,at);};
    w(1,0,++tick);w(5,0x51,++tick);w(8,0,++tick);w(9,0,++tick);
    for(auto b:{0x90,0x11,0xdc,0x74,0x09,0xf0,0x80,0xfe})w(0x18,b,++tick);
    w(5,0,++tick);w(1,1,++tick);w(8,0,10000);
    require(r.avdMcuCpu().instructions()==0 && r.knownBits(0x1f206bb8,1)==0,
      "new scheduler cannot assume a power-on freeze value or manufacture an idle command");
    w(0,1,10001);w(8,0,1000000);
    require(r.avdMcuCpu().instructions()==0,"original freeze bit pauses genuine MCU dispatch");
    w(0,0,1000001);
    require(r.read(0x1f206bb8,1,0,255,1000321)==9 && r.avdMcuBus().sharedWrites==1,
      "after explicit unfreeze, original MOVX instruction publishes a request into actual MIPS-visible backing");
    auto count=r.avdMcuCpu().instructions();w(0,1,1000321);w(8,0,2000000);
    require(r.avdMcuCpu().instructions()==count && r.knownBits(0x1f206bb8,1)==255,
      "later freeze retains CPU/request state without accruing instruction debt");
    rejects([&]{w(8,0,1999999);});
    require(r.avdMcuCpu().instructions()==count,"AVD scheduler rejects backwards virtual time without executing guest instructions");
  }
  for(auto opcode:{0x4d,0x5d,0x6d}){
    bool exact=true;
    for(unsigned value=0;value<256;++value)for(auto mask:{0u,0x3du,0xc0u,255u}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x90,0x11,0x58,0xe0,0x7d,int(mask),opcode,0xab,0x07,0xc3,0xf0})p.write(0x18,uint8_t(b));
      manta::VdMcuCpu cpu;manta::VdByte written{};unsigned writes=0;
      cpu.busRead=[&](uint16_t a)->std::optional<manta::VdByte>{return manta::VdByte{uint8_t(value),0,a};};
      cpu.busWrite=[&](uint16_t a,manta::VdByte v){written=v;++writes;return a==0x1158;};
      cpu.resetProgram(p);while(cpu.instructions()<7)cpu.step();
      auto expected=opcode==0x4d?(value|mask):opcode==0x5d?(value&mask):(value^mask);
      auto known=opcode==0x4d?mask:opcode==0x5d?uint8_t(~mask):0;
      exact &= writes==1 && written.value==expected && written.known==known && written.origin==0x1158;
    }
    require(exact,"8051 register AND/OR/XOR preserves per-bit unknown provenance for every source byte and boundary mask");
  }
  manta::AvdMcuBus bus;std::map<uint32_t,manta::VdByte> shared;
  bus.sharedRead=[&](uint32_t a){return shared[a];};bus.sharedWrite=[&](uint32_t a,manta::VdByte v){shared[a]=v;};
  require(bus.sharedAddress(0x11dc)==0x1f206bb8 && bus.sharedAddress(0x11dd)==0x1f206bb9 &&
    bus.sharedAddress(0x11de)==0x1f206bbc && bus.sharedAddress(0x11df)==0x1f206bbd,
    "original AVD command/payload aliases match complete original MIPS host-service protocol");
  require(bus.sharedAddress(0x11c0)==0x1f206b80 && bus.sharedAddress(0x1200)==0x1f206c00 &&
    !bus.sharedAddress(0xc008) && !bus.sharedAddress(0x101a),"AVD watchdog/internal config are never confused with AFEC/COMB aliases");
  require(bus.read(0x11dc)->known==0 && bus.read(0x11dc)->origin==0x11dc,
    "new shared bus has no invented idle command");
  require(!bus.write(0xc008,{0x55,255,0}) && !bus.deferSfr(0x8e,{1,255,0}),
    "default AVD hardware rejects unresolved startup writes");
  bus.configureDeferredStartup();
  for(auto [address,value]:std::array<std::pair<uint16_t,uint8_t>,4>{{{0xc008,0x55},{0xc009,0xaa},{0x101a,6},{0x101b,7}}}){
    require(bus.write(address,{value,255,0}) && !bus.read(address),
      "explicit diagnostic retains exact original startup writes without fabricating device readback");
    require(!bus.write(address,{uint8_t(value^1),255,0}) && !bus.write(address,{value,0,address}),
      "other or unknown startup configuration never accepted");
  }
  for(auto [address,value]:std::array<std::pair<uint8_t,uint8_t>,3>{{{0xd8,0},{0x8e,1},{0xe2,0}}})
    require(bus.deferSfr(address,{value,255,0}) && !bus.deferSfr(address,{value,0,0}),
      "diagnostic SFR writes require defined original data; no SFR output supplied");
  require(bus.configurationWrites==7 && bus.read(0x11dc)->known==0,
    "unresolved startup configurations cannot create an AVD command");
  require(bus.write(0x11dc,{9,255,0}) && shared[0x1f206bb8].value==9 &&
    bus.read(0x11dc)->known==255,"actual MCU store publishes command through shared bytes");
  shared[0x1f206bb8]={1,255,0};
  require(bus.read(0x11dc)->value==1,"actual MIPS command clear is visible to MCU; bus does not manufacture completion");
  require(bus.write(0x11dc,{1,12,0x11dc}) && shared[0x1f206bb8].known==12 &&
    !bus.write(0x11dd,{0,0,0x11dc}) && !bus.write(0x1200,{0,1,0x1200}),
    "same-byte AFEC masks preserve invalid fields; foreign and bank-ambiguous COMB restores reject");
  require(bus.read(0x8000)->known==0 && bus.read(0x8000)->origin==0x8000 &&
    bus.read(0x8001)->origin==0x8001,"unwritten private XRAM retains distinct invalid origins");
  require(bus.write(0x8000,{0xa5,0x0f,0x11dc}) && bus.read(0x8000)->known==0x0f &&
    bus.read(0x8000)->origin==0x11dc,"private RAM copying preserves unknown peripheral provenance");
  require(!bus.write(0x8100,{0,255,0}) && !bus.read(0x8100),"private XRAM extent remains bounded");
}
int main() {
  try {
    {
      manta::AvdMcuBus bus;std::map<uint32_t,manta::VdByte> bytes;
      bus.sharedWrite=[&](uint32_t a,manta::VdByte v){bytes[a]=v;};
      require(!bus.write(0x11c7,{0xa5,0,0x113c}) && !bus.write(0x11c8,{5,0xf8,0x113d}) && bytes.empty(),
        "default AVD bus does not enable cross-origin unknown timing publication");
      bus.symbolicTableConfiguration=true;bytes[0x1f206b8d]={0x6f,255,0};bytes[0x1f206b90]={4,255,0};
      require(bus.write(0x11c7,{0xa5,0,0x113c}) && bytes[0x1f206b8d].known==0 && bytes[0x1f206b8d].origin==0x113c,
        "original raw timing data copy invalidates an old known sample rather than inventing a new measurement");
      require(bus.write(0x11c8,{5,0xf8,0x113d}) && bytes[0x1f206b90].known==0xf8 && bytes[0x1f206b90].origin==0x113d,
        "original high-byte AND makes only discarded high bits known; measured low fields remain unknown");
      require(!bus.write(0x11c8,{0,0xf0,0x113d}) && !bus.write(0x11c8,{8,0xf8,0x113d}),
        "partial high timing copy requires the actual mask to have defined all upper five bits as zero");
      require(!bus.write(0x11c7,{0,0,0x113d}) && !bus.write(0x11c8,{0,0xf8,0x113c}) && !bus.write(0x11c7,{0,0,0x8077}),
        "swapped lanes and unrelated data origins cannot become unknown HTotal publications");
      require(!bus.write(0x113c,{0,0,0x11c7}) && !bus.write(0x11fc,{0,0,0x113c}) && !bus.write(0x11dc,{0,0,0x113c}),
        "timing copy permission cannot initialize raw counters, pending events or host requests");
      require(manta::ZuiFlow::watches(0x8049ee1c) && manta::ZuiFlow::watches(0x8049dfc4) && manta::ZuiFlow::watches(0x804afdfc),
        "read-only probes separately observe original window-state, surface and OSD-resource setup");
    }
    {
      using Z=manta::ZuiFlow;std::map<uint32_t,uint32_t> words;
      words[Z::stateAddress]=1;words[Z::activeAddress]=0;words[Z::previousAddress]=0;
      words[Z::headAddress]=words[Z::tailAddress]=0;words[Z::windowCountAddress]=0;words[Z::windowListAddress]=0;
      for(unsigned i=0;i<40;++i)words[Z::queueAddress+12*i]=0xffff;
      unsigned reads=0;auto read=[&](uint32_t a,unsigned)->std::optional<uint32_t>{++reads;if(words.contains(a))return words[a];return {};};
      Z probe;require(!probe.record(0x804e0db0,1,read,true) && reads==0,"ZUI observer never reads RAM outside explicit original routine boundaries");
      require(!probe.record(0x804e0dbc,2,read,false) && reads==0 && probe.signatureFailures==1,
        "mismatched original ZUI signature cannot label or decode arbitrary guest memory");
      require(probe.record(0x804e0d48,3,read,true) && probe.snapshots.back().state==1 &&
        probe.snapshots.back().indicesValid && probe.snapshots.back().occupiedSlots==0,
        "read-only ZUI observer distinguishes standby, empty queue and no windows");
      auto saved=words;auto oldReads=reads;
      require(!probe.record(0x804e0d48,4,read,true) && reads==oldReads && words==saved && probe.dropped==1,
        "repeated ZUI probes are bounded and never create messages, windows or state transitions");
      words[Z::tailAddress]=0xffffffff;words[Z::queueAddress+12]=4;words.erase(Z::queueAddress+24);
      auto partial=Z::capture(0,0,read);
      require(!partial.indicesValid && partial.knownSlots==39 && partial.occupiedSlots==1,
        "invalid queue indices cannot address guest data; unknown slots remain separate from occupied slots");
      words.erase(Z::stateAddress);words.erase(Z::headAddress);
      auto unknown=Z::capture(0,0,read);
      require(!unknown.state && !unknown.head && !unknown.indicesValid,"unknown ZUI globals remain unavailable rather than defaulting to standby or empty");
      require(!unknown.softwareTick && !unknown.sourceTimeoutTimestamp && !unknown.softwareFlags && !unknown.sourcePhase,
        "read-only source timeout diagnostics do not invent initial software state");
      require(!unknown.bitmap196Address && !unknown.bitmap196Valid && !unknown.geContextPointer && !unknown.geStaticSignature,
        "graphics observations retain missing/unknown asset and context fields rather than inventing ready objects");
      words[0x006bb54c]=0x03d6e128;words[0x006bb550]=588;words[0x006bb554]=14;
      words[0x006bb556]=42;words[0x006bb558]=7;words[0x006bb55a]=9;words[0x006bb55b]=1;
      words[0x006ada64]=0xffffffff;words[0x006ff098]=0x55aabeef;words[0x006d4f9c]=0;
      auto graphicWords=words;auto graphics=Z::capture(0,0,read);
      require(graphics.bitmap196Address==0x03d6e128 && graphics.bitmap196Bytes==588 && graphics.bitmap196Pitch==14 &&
        graphics.bitmap196Width==7 && graphics.bitmap196Height==42 && graphics.bitmap196Format==9 && graphics.bitmap196Valid==1,
        "fixed original bitmap slot observations distinguish source bytes, dimensions and validity");
      require(graphics.geContextPointer==0xffffffff && graphics.geStaticSignature==0x55aabeef && graphics.surfaceFlags==0 && words==graphicWords,
        "raw graphics observation never dereferences an invalid context pointer or allocates/enables a surface");
      std::vector<uint8_t> asset(588);for(unsigned i=0;i<asset.size();++i){asset[i]=uint8_t(i);words[0x03d6e128+i]=asset[i];}
      auto knownAsset=Z::capture(0,0,read);words.erase(0x03d6e128+123);auto partialAsset=Z::capture(0,0,read);
      require(!graphics.bitmap196Sha256 && knownAsset.bitmap196Sha256==manta::sha256(asset) && !partialAsset.bitmap196Sha256,
        "fixed original bitmap checksum requires every source byte to be known; a missing byte never becomes a pixel");
      words[0x006e561c]=0xfffffff0;words[0x006b9050]=0;words[0x006bedf0]=4;words[0x006d4a08]=3;
      saved=words;auto source=Z::capture(0,0,read);
      require(source.softwareTick==0xfffffff0u && source.sourceTimeoutTimestamp==0 && source.softwareFlags==4 && source.sourcePhase==3 && words==saved,
        "source timeout observer preserves zero, wraparound and raw flags without modifying guest RAM");
      require(Z::watches(0x80476a10) && Z::watches(0x8048848c) && Z::watches(0x80501bfc),
        "original source service, timeout destination and HTotal boundary are observed separately");
      require(Z::watches(0x804c3684) && Z::watches(0x804e0dc4) && Z::watches(0x8049f040),
        "observer independently watches OSD opening, dispatcher and queue consumption");
    }
    {
      manta::AvdMcuBus b;unsigned writes=0;b.sharedWrite=[&](uint32_t,manta::VdByte){++writes;};
      bool rejected=true;for(unsigned v=0;v<256;++v)rejected &= !b.write(0x11fc,{uint8_t(v),255,0});
      require(rejected && writes==0 && b.sharedWrites==0,"AVD event acknowledgements never become ordinary RW storage that creates pending event bits");
      manta::Riu r;r.candidateAvdMcuProgram=true;r.event=[](auto,auto,auto,auto,auto){};
      for(auto [a,width]:std::array<std::pair<uint32_t,unsigned>,3>{{{0x1f206bf8,1},{0x1f206bf8,2},{0x1f206bf6,4}}})
        rejects([&]{r.write(a,width,0xffffffff,0,0xffffffff,1);});
      require(r.knownBits(0x1f206bf8,1)==0,"overlapping MIPS stores cannot turn unknown AVD event bits into status or acknowledgements");
      {
        manta::AvdMcuBus clearOnly;unsigned stored=0;clearOnly.sharedWrite=[&](uint32_t,manta::VdByte){++stored;};
        clearOnly.eventAckClearOnly=true;unsigned events=0;
        clearOnly.event=[&](const auto& k,auto,auto,auto,const auto&){events+=k=="avd_event_ack_clear_only";};
        bool accepted=true;for(unsigned v=0;v<256;++v)accepted&=clearOnly.write(0x11fc,{uint8_t(v),255,0});
        require(accepted && stored==0 && clearOnly.sharedWrites==0 && clearOnly.eventAcks==256 && events==256,
          "opt-in clear-only event acknowledgement accepts every value but never stores or creates an event bit");
        require(!clearOnly.write(0x11dc,{0,0,0x113c}) && clearOnly.sharedWrites==0,
          "clear-only acknowledgement mode does not loosen any other AVD register write rule");
      }
    }
    std::vector<uint8_t> abc = {'a', 'b', 'c'};
    require(manta::sha256(abc) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA abc");
    require(manta::sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA empty");
    cpuTests();
    pmPowerInputTests();
    pmActivityScenarioTests();
    r8051UartConfigTests();
    openRiscTests();
    irDmaTests();
    gopResetTests();
    manta::PmBootReason pm;
    require(!pm.cause(),"PM cause remains unknown without external AC-on event");
    pm.acApplied();require(pm.cause()==0xff,"documented PM ACON event is distinct from standby and ready flags");
    pm.acRemoved();require(!pm.cause(),"external power loss invalidates retained PM cause");
    manta::SarAdc sar;sar.configure(0x20,0xff,2,0xff,0,0xff,10);sar.advance(18);
    require(!sar.result(0),"SAR no undefined external input is invented");
    sar.input(0,0);sar.input(1,3300);sar.configure(0x60,0xff,2,0xff,0,0xff,20);
    sar.advance(100);require(!sar.result(0),"SAR digital powerdown prevents sampling");
    sar.configure(0x20,0xff,2,0xff,0,0xff,100);sar.advance(107);
    require(!sar.result(0),"SAR sample is not ready before conversion time");sar.advance(108);
    require(sar.result(0)==0 && sar.result(1)==255,"SAR input voltage determines actual converted codes");
    sar.input(0,1650);sar.advance(109);require(sar.result(0)==0,"changing voltage does not change latched result until next conversion");
    sar.advance(116);require(sar.result(0)==127,"SAR next sample quantizes midpoint voltage");
    sar.configure(0x20,0xff,3,0xff,0,0xff,117);require(!sar.result(0),"SAR analog powerdown invalidates output");
    sar.configure(0x20,0,2,0xff,0,0xff,118);sar.advance(200);require(!sar.result(0),"SAR unknown power controls cannot manufacture output");
    rejects([&]{sar.input(8,3300);});rejects([&]{sar.input(0,3301);});
    require(manta::SarAdc::channel(0x1f002838)==2 && manta::SarAdc::channel(0x1f002839)==3 &&
      manta::SarAdc::channel(0x1f002841)==7 && !manta::SarAdc::channel(0x1f002836),
      "all eight original SAR jump-table byte addresses map independently, including SCART ID channels2/3");
    manta::Riu id;id.event=[](auto&,auto,auto,auto,auto&){};id.configureExploratoryScartIdGrounded();
    require(id.knownBits(0x1f002838,1)==0 && id.knownBits(0x1f002839,1)==0,"grounded SCART fixture alone does not create an ADC conversion");
    id.write(0x1f002800,1,0x20,0,0,10);id.write(0x1f002801,1,2,0,0,10);id.write(0x1f002804,1,0,0,0,10);
    id.read(0x1f002838,1,0,0,17);require(!id.stopReason.empty(),"SCART voltage cannot be sampled before programmed SAR latency");id.stopReason.clear();
    require(id.read(0x1f002838,1,0,0,18)==0 && id.read(0x1f002839,1,0,0,18)==0 && id.scartIdSamples()==2,
      "original power and period commands produce grounded ADC2/3 samples, rather than host source-readiness constants");
    require(id.knownBits(0x1f002834,1)==0 && id.knownBits(0x1f00283c,1)==0,"SCART ID fixture defines no keypad or unrelated analog input");
    id.write(0x1f002800,1,0x60,0,0,19);require(id.knownBits(0x1f002838,1)==0,"SAR powerdown withdraws SCART voltage validity");
    // Original GetScart1IDLevel arithmetic and original three-level classifier.
    constexpr uint32_t scale[]={0x00021900,0x00021080,0x00431021,0x3042fffc};
    constexpr uint32_t classify[]={0x00402021,0x2c4208fd,0x10400003,0x24030002,0x2c8203e9,0x38430001};
    for(auto code:{0u,50u,51u,115u,116u,255u}){
      manta::Engine cpu;cpu.map(0,0x1000);cpu.map(0x00486000,0x1000);
      cpu.write(0,scale,sizeof(scale));cpu.write(0x00486b10,classify,sizeof(classify));cpu.reg(UC_MIPS_REG_2,code);
      require(cpu.run(0x80000000,4)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_2)==code*20,"original SCART1 scaling executes its actual shifts/add/ANDI");
      auto level=code*20;require(cpu.run(0x80486b10,level>=2301?4:6)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_PC)==0x80486b28 &&
        cpu.reg(UC_MIPS_REG_3)==(level>=2301?2u:level>=1001?1u:0u),"original SCART ID classification crosses its actual1001/2301 boundaries; grounded input chooses no ID");
    }
    manta::Riu scartStrict;scartStrict.event=[](auto&,auto,auto,auto,auto&){};
    scartStrict.write(0x1f204b10,2,0x7f20,0,0xffff,1);
    scartStrict.prepareMaskedRead(0x1f204b15,15);
    scartStrict.read(0x1f204b15,1,0,255,2);
    require(!scartStrict.stopReason.empty(),"SCART original config alone never invents an external input");
    manta::Riu scartFixture;scartFixture.event=scartStrict.event;scartFixture.configureExploratoryScartGrounded();
    require(scartFixture.knownBits(0x1f204b15,1)==0,"SCART grounded fixture cannot enable an unconfigured ADC");
    scartFixture.write(0x1f204b10,2,0x7f20,0,0xffff,1);
    require(scartFixture.knownBits(0x1f204b15,1)==15,"SCART fixture defines only consumed nibble; upper fields stay unknown");
    scartFixture.prepareMaskedRead(0x1f204b15,15);
    require((scartFixture.read(0x1f204b15,1,0,255,2)&15)==0 && scartFixture.stopReason.empty(),"SCART explicit grounded-input experiment is observed through original mask");
    scartFixture.prepareMaskedRead(0x1f204b15,15);
    require((scartFixture.read(0x1f204b15,1,0,255,3)&15)==0 && scartFixture.scartReads()==2,"SCART reads do not toggle external signal");
    scartFixture.write(0x1f204b10,2,0x7f10,0,0xffff,3);
    scartFixture.write(0x1f204b10,2,0x10,0,0xffff,3);
    require(scartFixture.knownBits(0x1f204b15,1)==15,"SCART original scaler-table configuration follows boot setup; ground remains explicit");
    manta::ScartGroundedInput uninitialized;uninitialized.ground();uninitialized.configure(0x10,0xffff);
    require(!uninitialized.sample(),"SCART table configuration cannot substitute for unobserved boot setup");
    scartFixture.read(0x1f204b15,1,0,255,4);
    require(!scartFixture.stopReason.empty(),"SCART unmasked upper status cannot silently become valid");
    scartFixture.write(0x1f204b10,2,0,0,0xffff,5);
    require(scartFixture.knownBits(0x1f204b15,1)==0,"SCART reconfiguration withdraws experimental observation");
    manta::Riu scmi;scmi.event=scartStrict.event;scmi.configureExploratoryScBank(0x12);
    require(scmi.knownBits(0x1f205e08,2)==0,"SCMI format reset is unknown by default");
    scmi.configureExploratoryScmiFormat(0xf0);
    require(scmi.knownBits(0x1f205e08,2)==255 && scmi.knownBits(0x1f205f08,2)==255,"SCMI fixture defines only main/sub low byte");
    require(scmi.read(0x1f205e08,1,0,255,1)==0xf0,"SCMI configured byte is read back, not a constant success response");
    scmi.symbolicInit=true;scmi.prepareRmw(0x1f205e08,0xf0);
    scmi.write(0x1f205e08,2,0xa500,0,0xffff,2);
    require(scmi.read(0x1f205e08,1,0,255,3)==0 && scmi.knownBits(0x1f205e08,2)==255,"SCMI guest masked change alters format and leaves upper byte invalid");
    require(scmi.read(0x1f205f08,1,0,255,4)==0xf0,"SCMI main and sub configuration are independent");
    rejects([&]{scmi.configureExploratoryScmiFormat(256);});
    manta::Riu detect;detect.event=scartStrict.event;detect.configureExploratoryScBank(1);
    detect.write(0x1f205ea4,1,0x0c,0,255,1);
    require((detect.knownBits(0x1f205ea4,1)&12)==0,"Euro HDTV status cannot be defined by a software register write");
    detect.prepareMaskedRead(0x1f205ea4,12);detect.read(0x1f205ea4,1,0,255,2);
    require(!detect.stopReason.empty(),"Euro HDTV status stays unknown without input-state evidence");
    detect.stopReason.clear();detect.configureExploratoryAnalogDisconnected();detect.prepareMaskedRead(0x1f205ea4,12);
    require((detect.read(0x1f205ea4,1,0,255,3)&12)==0 && detect.stopReason.empty(),"disconnected analog input produces no special HDTV detection");
    detect.write(0x1f205ea4,1,0x8e,0,255,4);detect.prepareMaskedRead(0x1f205ea4,12);
    require((detect.read(0x1f205ea4,1,0,255,5)&12)==0,"detector control write cannot fabricate positive input detection");
    detect.configureExploratoryAnalogDisconnected(false);
    require((detect.knownBits(0x1f205ea4,1)&12)==0,"unknown connected signal withdraws detection validity");
    detect.configureExploratoryScBank(2);detect.write(0x1f205ea4,1,12,0,255,6);
    require(detect.read(0x1f205ea4,1,0,255,7)==12,"Euro input detector does not change unrelated scaler bank fields");
    manta::PqSram pq;
    require(!pq.known(),"PQ SRAM reset busy state is not fabricated");
    pq.exploratoryIdle();pq.submit(1,128,{1,2,3,4,5},10);
    require(pq.busy() && !pq.written[0][128],"PQ coefficient write remains pending until deterministic deadline");
    require(!pq.advance(73) && pq.busy(),"PQ reads do not toggle completion");
    require(pq.advance(74) && !pq.busy() && pq.data[0][128]==std::array<uint8_t,5>{1,2,3,4,5},"PQ commits all five original bytes at programmed address");
    pq.submit(2,0xc0,{9,8,7,6,5},75);pq.advance(139);
    require(pq.data[1][0xc0][0]==9 && !pq.written[0][0xc0] && pq.commits==2,"PQ Y and C SRAM use separate backing arrays");
    bool pqCRejected=false;try{pq.submit(2,0x1c0,{0,0,0,0,0},140);}catch(const std::exception&){pqCRejected=true;}
    require(pqCRejected,"related Marlon C address constants do not become validated Manta addresses");
    bool pqRejected=false;try{pq.submit(3,0,{0,0,0,0,0},140);}catch(const std::exception&){pqRejected=true;}
    require(pqRejected,"PQ unknown combined plane mode is rejected");
    manta::Riu port;port.event=[](auto,auto,auto,auto,auto){};port.configureExploratoryScBank(0x23);
    port.write(0x1f205f04,2,0,0,255,1);
    require(!(port.knownBits(0x1f205f04,2)&0x100),"PQ control write cannot define hardware busy status");
    port.configureExploratoryPqSramIdle();port.prepareMaskedRead(0x1f205f04,0x100);
    require(!(port.read(0x1f205f04,2,0,255,2)&0x100),"explicit idle fixture supplies only idle command status");
    manta::Riu pqReset;pqReset.event=[](auto,auto,auto,auto,auto){};pqReset.configureExploratoryPqDisabledPorts();pqReset.configureExploratoryScBank(0x23);
    require(pqReset.knownBits(0x1f205f04,2)==3,"PQ disabled-port reset hypothesis defines only two enable bits");
    port.write(0x1f205f08,2,128,0,255,3);port.write(0x1f205f0c,2,0x0201,0,255,4);
    port.write(0x1f205f10,2,0x0403,0,255,5);port.write(0x1f205f14,2,5,0,255,6);
    port.configureExploratoryScBank(0);port.write(0x1f205e0c,2,0,0,255,7);
    port.configureExploratoryScBank(0x23);port.write(0x1f205f04,2,0x101,0,255,8);
    require(port.scBank()==0x23 && (port.read(0x1f205f04,2,0,255,9)&0x100),"original banked port strobe sets busy without changing selector");
    port.read(0x1f205f04,2,0,255,72);
    require(port.pqSram().commits==1 && port.pqSram().data[0][128][4]==5,"RIU deadline commits guest coefficient payload");
    port.write(0x1f205f05,1,1,0,255,73);
    require(port.knownBits(0x1f205f05,1)==255 && port.read(0x1f205f05,1,0,255,74)==1,"PQ byte alias accesses the same busy bit and transfer state");
    port.write(0x1f205f04,2,0x100,0,255,80);
    require(port.pqSram().busy() && port.pqSram().submissions==2,"PQ disabled CPU ports cannot retrigger preserved busy field");
    port.read(0x1f205f04,2,0,255,137);
    require(port.pqSram().commits==2 && !port.pqSram().busy(),"captured final coefficient completes after CPU port disable");
    manta::Riu cUnknown;cUnknown.symbolicInit=true;cUnknown.event=[](auto,auto,auto,auto,auto){};
    cUnknown.configureExploratoryPqSramIdle();cUnknown.configureExploratoryPqDisabledPorts();
    cUnknown.configureExploratoryScBank(0);cUnknown.write(0x1f205e0c,2,0,0,255,1);
    cUnknown.configureExploratoryScBank(0x23);cUnknown.prepareRmw(0x1f205f08,255);
    cUnknown.write(0x1f205f08,2,0xc0,0,255,2);
    cUnknown.write(0x1f205f0c,2,0x0201,0,255,3);cUnknown.write(0x1f205f10,2,0x0403,0,255,4);cUnknown.write(0x1f205f14,2,5,0,255,5);
    bool unknownCRejected=false;try{cUnknown.write(0x1f205f04,2,0x102,0,255,6);}catch(const std::exception&){unknownCRejected=true;}
    require(unknownCRejected && cUnknown.pqSram().submissions==0,"C writer low-byte mask cannot silently discard an unknown ninth address bit");
    manta::Riu revision;revision.event=[](auto,auto,auto,auto,auto){};
    revision.read(0x1f003d9d,1,0x8050a5f0,255,0);
    require(!revision.stopReason.empty() && revision.knownBits(0x1f003d9d,1)==0,"actual VIF chip revision is unknown by default");revision.stopReason.clear();
    rejects([&]{revision.configureExploratoryVifRevision(256);});
    for(unsigned byte:{0u,1u,2u,255u}){
      manta::Riu bus;bus.event=revision.event;bus.configureExploratoryVifRevision(byte);
      require(bus.knownBits(0x1f003d9c,2)==0xff00 && bus.knownBits(0x1f003c05,1)==0,
        "VIF stepping fixture defines only logical1ECF high byte, never fetch3C05 or version1ECE");
      bus.write(0x1f003c71,1,0xa5,0,0,0); // known test setup, never a reset assertion
      manta::Engine cpu;cpu.map(0,0x1000);cpu.map(0x0050a000,0x1000);cpu.map(0x006ad000,0x1000);
      constexpr uint32_t branch[]={0x90433d9d,0x14600088,0x8e18d97c,0x3c040002};
      constexpr uint32_t conditional[]={0x93023c71,0x304200fe,0xa3023c71,0x08142980,0x3c040002};
      cpu.write(0x0050a5f0,branch,sizeof(branch));cpu.write(0x0050a818,conditional,sizeof(conditional));
      uint32_t base=0xbf000000;cpu.write(0x006ad97c,&base,4);cpu.reg(UC_MIPS_REG_2,base);cpu.reg(UC_MIPS_REG_16,0x806b0000);
      struct Bus{manta::Riu* r;unsigned writes=0;} context{&bus};
      auto read=+[](uc_engine*,uint64_t offset,unsigned n,void* user)->uint64_t{
        return static_cast<Bus*>(user)->r->read(0x1f003000+uint32_t(offset),n,0,255,1);};
      auto write=+[](uc_engine*,uint64_t offset,unsigned n,uint64_t value,void* user){
        auto& c=*static_cast<Bus*>(user);++c.writes;c.r->write(0x1f003000+uint32_t(offset),n,uint32_t(value),0,0,1);};
      manta::check(uc_mmio_map(cpu.handle,0x1f003000,0x1000,read,&context,write,&context));
      uc_hook join;manta::check(uc_hook_add(cpu.handle,&join,UC_HOOK_CODE,
        reinterpret_cast<void*>(+[](uc_engine* engine,uint64_t,uint32_t,void*){uc_emu_stop(engine);}),nullptr,0x8050a600,0x8050a600));
      require(cpu.run(0x8050a5f0,20)==UC_ERR_OK && cpu.reg(UC_MIPS_REG_PC)==0x8050a600 &&
        context.writes==unsigned(byte!=0) && bus.vifRevisionReads()==1,
        "both original VIF stepping branches execute naturally and rejoin; nonzero revision alone causes the extra masked control write");
      require(bus.read(0x1f003c71,1,0,255,2)==(byte?0xa4u:0xa5u),"original VIF branch preserves every control bit except bit0");
      rejects([&]{bus.write(0x1f003d9c,2,0,0,0,3);});
    }
    require(revision.knownBits(0x1f003c05,1)==0,"fetch revision register is unknown without an explicit experiment");
    revision.read(0x1f003c05,1,0x806006d4,255,1);
    require(!revision.stopReason.empty(),"strict fetch predicate cannot consume unverified silicon identity");
    revision.stopReason.clear();revision.configureExploratoryFetchRevisionByte(1);
    require(revision.knownBits(0x1f003c04,2)==0xff00 && revision.knownBits(0x1f003c00,4)==0,
      "revision fixture supplies one high byte only; no nearby chip IDs are invented");
    require(revision.read(0x1f003c05,1,0x806006d4,255,2)==1 && revision.read(0x1f003c05,1,0x806006d4,255,3)==1,
      "explicit immutable revision fixture is stable, not read-driven status");
    revision.prepareMaskedRead(0x1f003c04,0xff00);
    require((revision.read(0x1f003c04,2,0,255,4)>>8)==1,"word high-byte alias shares the same revision fixture");
    rejects([&]{revision.write(0x1f003c04,2,0,0,0xffff,5);});
    rejects([&]{revision.configureExploratoryFetchRevisionByte(256);});
    for(unsigned byte:{0u,1u,2u,255u}){
      manta::Riu bus;bus.event=[](auto,auto,auto,auto,auto){};bus.configureExploratoryFetchRevisionByte(byte);
      manta::Engine engine;engine.map(0x00600000,0x1000);engine.map(0x006b9000,0x1000);
      // Exact original five-instruction predicate at 806006CC..DC.
      const uint32_t original[]={0x3c02806c,0x8c43963c,0x90623c05,0x03e00008,0x2c420002};
      const uint32_t base=0xbf000000;engine.write(0x006006cc,original,sizeof(original));
      engine.write(0x006b963c,&base,4);engine.reg(UC_MIPS_REG_RA,0x80600700);
      manta::check(uc_mmio_map(engine.handle,0x1f003000,0x1000,
        [](uc_engine*,uint64_t offset,unsigned size,void* data)->uint64_t{
          return static_cast<manta::Riu*>(data)->read(0x1f003000+uint32_t(offset),size,0x806006d4,255,0);
        },&bus,nullptr,nullptr));
      manta::check(engine.run(0x806006cc,5));
      require(engine.reg(UC_MIPS_REG_2)==unsigned(byte<2),"original MIPS load, JR delay slot and unsigned threshold choose the actual experimental branch");
      engine.map(0x005f4000,0x1000);engine.map(0x00601000,0x1000);
      // Exact caller 805F4B10..40: original JAL, conditional branch, add-two,
      // cap comparison and MOVZ. Stop at its branch join, never alter the PC.
      const uint32_t caller[]={0x0c1801b3,0,0x1040000a,0x02551021,0x00541021,0x00021080,
        0x03c21021,0x8c440078,0x26030002,0x3062ffff,0x3090ffff,0x0083182b,0x0043800a};
      engine.write(0x005f4b10,caller,sizeof(caller));
      engine.reg(UC_MIPS_REG_18,0);engine.reg(UC_MIPS_REG_21,0);engine.reg(UC_MIPS_REG_20,0);
      engine.reg(UC_MIPS_REG_30,0x80601000);
      uc_hook joinHook;
      manta::check(uc_hook_add(engine.handle,&joinHook,UC_HOOK_CODE,
        reinterpret_cast<void*>(+[](uc_engine* cpu,uint64_t,uint32_t,void*){uc_emu_stop(cpu);}),
        nullptr,0x805f4b44,0x805f4b44));
      for(uint32_t cap:{1000u,901u}){
        engine.write(0x00601078,&cap,4);engine.reg(UC_MIPS_REG_16,900);
        manta::check(engine.run(0x805f4b10,100));
        require(engine.reg(UC_MIPS_REG_PC)==0x805f4b44 &&
          engine.reg(UC_MIPS_REG_16)==(byte<2?std::min(902u,cap):900u),
          "original caller selects the revision workaround, applies two and saturates at its guest parameter cap");
      }
    }
    manta::VdMcuProgram vd;
    require(vd.contiguousImage().empty(),"VD MCU program begins unwritten; no substitute firmware installed");
    rejects([&]{vd.read();});rejects([&]{vd.write(0x18,2);});
    vd.write(0,1);vd.write(1,0);vd.write(5,0x50);vd.write(5,0x51);vd.write(8,0);vd.write(9,0);
    const std::vector<uint8_t> originalVector={2,0x4d,0xf3,2,0x4a,0x8a};
    for(auto b:originalVector)vd.write(0x18,b);
    require(vd.writes==6 && vd.cursor()==6 && vd.contiguousImage()==originalVector,
      "sequential VD port stores actual guest bytes at incremented addresses");
    rejects([&]{vd.read();});
    vd.write(8,0);vd.write(9,0);
    for(auto b:originalVector)require(vd.read()==b,"VD readback returns the uploaded byte, not a successful comparison constant");
    require(vd.reads==6 && vd.cursor()==6,"VD verification advances the shared address exactly once per read");
    vd.write(8,1);vd.write(9,0);vd.write(0x18,0x99);vd.write(8,1);vd.write(9,0);
    require(vd.read()==0x99 && vd.contiguousImage()[1]==0x99,"changed guest program bytes alter readback; immutable source is never secretly recopied");
    vd.write(5,0x50);rejects([&]{vd.read();});vd.write(5,0);vd.write(1,1);
    require(vd.write(0,0) && vd.startRequests==1,"original release sequence requests auxiliary execution; no CPU ready response produced");
    rejects([&]{vd.write(5,0x52);});
    manta::Riu vdBus;vdBus.event=[](auto,auto,auto,auto,auto){};vdBus.candidateVdMcuProgram=true;
    uint64_t vdTick=0;auto vb=[&](unsigned port,unsigned value){vdBus.write(0x1f206900+port,1,value,0,255,++vdTick);};
    vb(0,1);vb(1,0);vb(5,0x50);vb(5,0x51);vb(8,0);vb(9,0);vb(0x18,2);vb(0x18,0x4d);
    vb(8,0);vb(9,0);
    require(vdBus.read(0x1f206920,1,0x804f5760,255,++vdTick)==2 &&
      vdBus.read(0x1f206920,1,0x804f5760,255,++vdTick)==0x4d,"RIU data ports share the addressed VD program storage");
    vb(5,0x50);vb(5,0);vb(1,1);rejects([&]{vb(0,0);});
    require(vdBus.vdMcuProgram().startRequests==1 && vdBus.vdMcuProgram().reads==2,
      "native bus stops at original VD start instead of fabricating an executed auxiliary CPU");
    manta::VdMcuProgram boot8051;boot8051.write(0,1);boot8051.write(1,0);boot8051.write(5,0x51);boot8051.write(8,0);boot8051.write(9,0);
    // Architectural fixture with the original Keil reset clear loop followed
    // by MOVX. All external memory is deliberately undefined.
    const std::vector<uint8_t> clearLoop={2,0,3,0x78,3,0xe4,0xf6,0xd8,0xfd,0x75,0x81,0xa5,0x90,0x2f,0,0xe0};
    for(auto b:clearLoop)boot8051.write(0x18,b);
    manta::VdMcuCpu aux8051;unsigned attempted8051=0,external8051=0;
    aux8051.instruction=[&](uint16_t pc,uint8_t opcode,const std::string&){
      require(pc<clearLoop.size() && clearLoop[pc]==opcode,"auxiliary 8051 engine fetches the uploaded program storage");++attempted8051;
    };
    aux8051.external=[&](uint16_t address,bool write,uint8_t){
      require(address==0x2f00 && !write,"8051 MOVX uses the actual guest DPTR address");++external8051;
    };
    std::string auxStop;
    try{aux8051.startup(boot8051,100);}catch(const std::exception& error){auxStop=error.what();}
    if(aux8051.instructions()!=11 || attempted8051!=12 || external8051!=1)
      std::cerr<<"VD fixture completed="<<aux8051.instructions()<<" attempts="<<attempted8051<<" external="<<external8051<<" pc="<<aux8051.pc()<<" reason="<<auxStop<<'\n';
    require(aux8051.instructions()==11 && attempted8051==12 && external8051==1,
      "8051 reset branch and RAM loop execute; rejected MOVX is excluded from completed instructions");
    require(aux8051.pc()==15 && aux8051.stack()==0xa5 && auxStop.find("XDATA read unsupported")!=std::string::npos,
      "8051 stops on the exact unsupported external access with original stack initialization intact");
    require(boot8051.contiguousImage()==clearLoop,"auxiliary reset never erases or replaces uploaded firmware");
    auto test8051=[&](const std::vector<uint8_t>& code,auto inspect){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:code)p.write(0x18,b);
      manta::VdMcuCpu c;std::string reason;
      try{c.startup(p,100);}catch(const std::exception& error){reason=error.what();}
      require(reason.find("XDATA read unsupported")!=std::string::npos,"8051 core fixture must stop on the final external read, never silently advance");
      inspect(c);
    };
    test8051({0x75,0x81,0xa5,0x12,0,12,0x90,0x2f,0,0xe0,0,0,0x74,0x55,0x22},[&](auto& c){
      require(c.stack()==0xa5 && c.accumulator()==0x55 && c.pc()==9 && c.instructions()==5,
        "LCALL/RET use upper internal RAM above 7F and preserve the return address, rather than treating stack bytes as SFRs");
    });
    {
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      const std::vector<uint8_t> code={2,0,3,0x90,0x10,0x80,0x74,0xaa,0xf0,0xa3,0x74,0x55,0xf0,
        0xa3,0x74,0xaa,0xf0,0xa3,0x74,0x55,0xf0,0x90,0x2f,0,0xe0};
      for(auto b:code)p.write(0x18,b);
      manta::VdMcuCpu c;c.deferResetKeyWrites=true;
      std::vector<std::pair<uint16_t,uint8_t>> deferred;
      c.deferredResetKey=[&](uint16_t address,uint8_t value){deferred.emplace_back(address,value);};
      std::string stop;try{c.startup(p,100);}catch(const std::exception& e){stop=e.what();}
      require(deferred==std::vector<std::pair<uint16_t,uint8_t>>{{0x1080,0xaa},{0x1081,0x55},{0x1082,0xaa},{0x1083,0x55}} &&
        c.pc()==0x18 && stop.find("XDATA read unsupported")!=std::string::npos,
        "original key-write pattern may retire only under explicit deferral; first unmapped read still stops");
      p.codeStorage()[7]=0xab;deferred.clear();stop.clear();
      manta::VdMcuCpu altered;altered.deferResetKeyWrites=true;
      altered.deferredResetKey=[&](uint16_t address,uint8_t value){deferred.emplace_back(address,value);};
      try{altered.startup(p,100);}catch(const std::exception& e){stop=e.what();}
      require(deferred.empty() && altered.pc()==8 && stop.find("XDATA write unsupported")!=std::string::npos,
        "different original key data cannot pass the narrow deferred sequence");
    }
    for(auto [a,op,ac]:std::array<std::array<unsigned,3>,4>{{{7,0x24,0},{15,0x24,1},{8,0x94,0},{16,0x94,1}}}){
      test8051({0x74,uint8_t(a),uint8_t(op),1,0x90,0x2f,0,0xe0},[&](auto& c){
        require(bool(c.status()&0x40)==bool(ac) && c.accumulator()==(op==0x24?a+1:a-1),
          "8051 auxiliary carry/borrow uses bit3 carry into bit4, not the three-bit boundary");
      });
    }
    for(bool readBack:{false,true}){
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      const std::vector<uint8_t> code=readBack?std::vector<uint8_t>{0x75,0x8e,2,0xe5,0x8e}:
        std::vector<uint8_t>{0x75,0x8e,2,0x75,0xa8,0x80,0x90,0x2f,0,0xe0};
      for(auto b:code)p.write(0x18,b);
      manta::VdMcuCpu c;c.deferSfr8eWrite=true;unsigned deferred=0;
      c.deferredSfr8e=[&](uint8_t v){require(v==2,"deferred SFR write records the original CPU operand");++deferred;};
      std::string reason;try{c.startup(p);}catch(const std::exception& error){reason=error.what();}
      require(deferred==1 && c.instructions()==(readBack?1u:3u),"explicit deferral executes the original write; EA alone admits no disabled interrupt source");
      require(reason.find(readBack?"SFR read unsupported":"XDATA read unsupported")!=std::string::npos,
        "deferred SFR effects never fabricate valid readback or an external hardware response");
    }
    {
      manta::VdMcuProgram p;p.write(0,1);p.write(1,0);p.write(5,0x51);p.write(8,0);p.write(9,0);
      for(auto b:{0x75u,0xa8u,0x81u})p.write(0x18,uint8_t(b));
      manta::VdMcuCpu c;std::string reason;try{c.startup(p);}catch(const std::exception& error){reason=error.what();}
      require(c.instructions()==0 && reason.find("interrupt-source enable")!=std::string::npos,
        "enabling an actual MCU interrupt source remains blocked without verified MStar wiring");
    }
    for(bool suspend:{false,true}){
      manta::Riu r;r.event=[](auto,auto,auto,auto,auto){};
      r.candidateVdMcuProgram=true;r.exploratoryVdMcuStartup=true;r.exploratoryVdSuspendedBoundary=suspend;
      uint64_t tick=0;auto w=[&](unsigned p,unsigned v){r.write(0x1f206900+p,1,v,0,255,++tick);};
      w(0,1);w(1,0);w(5,0x51);w(8,0);w(9,0);
      const std::vector<uint8_t> code={2,0,3,0x75,0x8e,2};for(auto v:code)w(0x18,v);
      w(8,0);w(9,0);for(auto v:code)require(r.read(0x1f206920,1,0,255,++tick)==v,"stalled-device experiment still requires actual uploaded bytes");
      w(5,0x50);w(5,0);w(1,1);bool rejected=false;
      try{w(0,0);}catch(const manta::VdHardwareBoundary&){rejected=true;}
      require(rejected!=suspend && r.vdMcuSuspended()==suspend,"only explicit auxiliary suspension lets the original main-CPU store retire");
      require(r.vdMcuCpu().instructions()==1 && r.vdMcuCpu().pc()==3,"suspension retains actual original CPU execution at its unresolved instruction");
      if(suspend){
        r.write(0x1f224c00,1,2,0,255,++tick);w(5,2);w(5,0);
        require(r.vdMcuProgram().interruptRequests==1 && r.vdMcuProgram().interruptPending,
          "original high/low mailbox doorbell retains a pending request to the suspended MCU");
        require(r.read(0x1f224c00,1,0,255,++tick)==2,
          "interrupt request cannot become a fabricated FF mailbox completion");
        w(5,2);w(5,0);
        require(r.vdMcuProgram().interruptRequests==2 && r.vdMcuProgram().interruptPending,
          "each rising doorbell edge is counted; no polling-driven acknowledgment is invented");
        manta::Engine e;e.map(0x004f2000,0x1000);e.map(0x006b8000,0x1000);
        const uint32_t base=0xbf000000;e.write(0x006b8d0c,&base,4);
        // Exact original mailbox write function 804F2E5C..2F10.
        const uint32_t original[]={0x3c08806c,0x8d068d0c,0x3c070022,0x34e24c04,0x00c23021,0x7c833a00,
          0xa0c30000,0x8d028d0c,0x34e34c01,0x308400ff,0x00431021,0xa0440000,0x8d038d0c,0x34e24c05,
          0x30a500ff,0x00621821,0xa0650000,0x8d028d0c,0x34e74c00,0x24050002,0x00471021,0xa0450000,
          0x8d038d0c,0x3c040020,0x34846905,0x00641821,0xa0650000,0x8d028d0c,0x00002821,0x240600ff,
          0x00441021,0xa0400000,0x8d038d0c,0x0813cbbc,0x00671821,0x10a60008,0x3085ffff,0x90620000,
          0x304200ff,0x1446fffb,0x24a40001,0xa0600000,0x03e00008,0x24020001,0x03e00008,0x00001021};
        e.write(0x004f2e5c,original,sizeof(original));
        struct BusContext{manta::Riu* bus;uint32_t base;uint64_t* tick;};
        BusContext control{&r,0x1f206000,&tick},mailbox{&r,0x1f224000,&tick};
        auto read=+[](uc_engine*,uint64_t off,unsigned n,void* p)->uint64_t{
          auto& c=*static_cast<BusContext*>(p);return c.bus->read(c.base+uint32_t(off),n,0,255,++*c.tick);
        };
        auto write=+[](uc_engine*,uint64_t off,unsigned n,uint64_t v,void* p){
          auto& c=*static_cast<BusContext*>(p);c.bus->write(c.base+uint32_t(off),n,uint32_t(v),0,255,++*c.tick);
        };
        manta::check(uc_mmio_map(e.handle,control.base,0x1000,read,&control,write,&control));
        manta::check(uc_mmio_map(e.handle,mailbox.base,0x1000,read,&mailbox,write,&mailbox));
        e.reg(UC_MIPS_REG_4,0x2200);e.reg(UC_MIPS_REG_5,255);e.reg(UC_MIPS_REG_RA,0x804f2100);
        uc_hook returnHook;manta::check(uc_hook_add(e.handle,&returnHook,UC_HOOK_CODE,
          reinterpret_cast<void*>(+[](uc_engine* cpu,uint64_t,uint32_t,void*){uc_emu_stop(cpu);}),nullptr,0x804f2100,0x804f2100));
        manta::check(e.run(0x804f2e5c,4000));
        require(e.reg(UC_MIPS_REG_PC)==0x804f2100 && e.reg(UC_MIPS_REG_2)==0 &&
          r.vdMcuProgram().interruptRequests==3 && r.read(0x1f224c00,1,0,255,++tick)==2,
          "original MIPS mailbox routine executes its finite timeout and failure return when the real MCU cannot acknowledge");
      }
      require(r.knownBits(0x1f206600,1)==0 && r.knownBits(0x1f20667c,1)==0,"stalled auxiliary CPU produces no invented decoder status or reply");
      r.read(0x1f20667c,1,0,255,++tick);
      require(!r.stopReason.empty(),"main-CPU use of unknown decoder output still stops in the suspended-device experiment");
    }
    vdSharedExecutionTests();
    vifZeroInputTests();
    avdBusTests();
    flashTests();
    riuTests();
    bdmaCopyTests();
    gpioTests();
    miuTests();
    unknownObservationTests();
    rtcTests();
    utmiTests();
    virtualTimeTests();
    spiTests();
    sharedMiuTests();
    displayHardwareTests();
    gopGuestBankTests();
    gopMiuAddressTests();
    scalerTimingTests();
    gopUpdateTests();
    preservedBitsTests();
    combRestoreTests();
    preservedRegisterRestoreTests();
    semaphoreTests();
    geQueueTests();
    geRectFillTests();
    geBitBltTests();
    geStretchRequestTests();
    htotalBranchJoinTests();
    readMaskTests();
    opaquePolicyTests();
    gopSelectorProvenanceTests();
    gopPaletteTests();
    tspProvenanceTests();
    tspQueueTests();
    tspIndirectTests();
    tspDownloadTests();
    tspMappingBurstTests();
    dlcStatusTests();
    combBankTests();
#ifdef UC_MIPS_MANTA_TIMER_API
    timerTests();
#endif
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
