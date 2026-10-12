#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "storage/sha256.hpp"
#include "board/unknown_observations.hpp"
#include "board/preserved_bits.hpp"
#include "board/mmio_read_mask.hpp"
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
  manta::IrDma dma;std::array<uint8_t,96> ram{};std::array<uint8_t,48> record{};
  record.fill(0x35);auto store=[&](uint32_t a,std::span<const uint8_t> b){
    require(a>=0x1000 && a+b.size()<=0x1060,"IR capture addresses actual configured ring");
    std::copy(b.begin(),b.end(),ram.begin()+(a-0x1000));
  };
  require(!dma.producer(),"IR producer remains unknown before descriptor/reset experiment");
  rejects([&]{dma.capture(record,store);});dma.configure(0x1000,2);
  require(dma.producer()==0 && dma.captures()==0,"IR empty cursor derives from configured capture state, not reads");
  dma.capture(record,store);require(dma.producer()==1 && ram[0]==0x35 && ram[47]==0x35,"IR captured record causes actual DMA bytes and cursor advance");
  record.fill(0x62);dma.capture(record,store);
  require(dma.producer()==0 && ram[48]==0x62 && dma.captures()==2,"IR capture ring wraps after actual second transfer");
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
void displayHardwareTests() {
  manta::Riu r; r.event = [](auto&,auto,auto,auto,auto&){};
  require(!r.gopBankKnown(), "GOP reset selector remains unknown");
  rejects([&]{r.write(0x1f203e04,2,123,0,0,0);});
  r.write(0x1f203ffc,2,1,0,0,0);
  require((r.knownBits(0x1f203ffc,2)&0xf0f0)==0, "GOP bank writes cannot fabricate interrupt or hardware write-ack flags");
  auto w = [&](unsigned index, uint16_t value) { r.write(0x1f203e00+index*4,2,value,0,0,0); };
  w(0,0x51); w(1,0x1234); w(2,1); w(9,960); w(4,0); w(5,960); w(6,0); w(7,1080);
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
}
void preservedBitsTests() {
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
  require(step(0x1051ffd8).empty(),"known selector comparison is allowed after actual guest mask");
  p.seed(2,1,0,[&](unsigned b){return key(0x1f203ffc+b);});step(0x3042000f);
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
void readMaskTests() {
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
void tspProvenanceTests() {
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
  require(r.tspIndirectReads()==1 && r.knownBits(0x1f202b34,2)==0 && r.knownBits(0x1f202b38,2)==0,
      "original indirect read command retains unknown filter SRAM reset contents");
  require(r.controlByteKey(0x1f202b34)==r.preservedStoreKey(0x1f202b2c) &&
      r.controlByteKey(0x1f202b38)==r.preservedStoreKey(0x1f202b30),
      "separate TSP read/write ports share exact internal register bit provenance");
  r.write(0x1f202b2c,2,0x5678,0,0xffff,6);r.write(0x1f202b30,2,0x1234,0,0xffff,7);
  command(0x21);require(r.knownBits(0x1f202b34,2)==0,"data staging without a write command cannot change filter SRAM");
  command(0x23);command(0x21);
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
  rejects([&]{command(0x25);});
  select(0x430000);rejects([&]{command(0x21);});
  select(0x420002);rejects([&]{command(0x21);});
  r.write(0x1f202b24,2,0,0,0xffff,12);r.write(0x1f202b28,2,0x8000,0,0xffff,13);
  rejects([&]{command(0x21);});
  select(0x420660);r.write(0x1f202b2c,2,0,0,0xffff,14);r.write(0x1f202b30,2,0x100,0,0xffff,15);
  command(0x23);
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
int main() {
  try {
    std::vector<uint8_t> abc = {'a', 'b', 'c'};
    require(manta::sha256(abc) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA abc");
    require(manta::sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA empty");
    cpuTests();
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
    rejects([&]{sar.input(2,3300);});rejects([&]{sar.input(0,3301);});
    flashTests();
    riuTests();
    gpioTests();
    miuTests();
    unknownObservationTests();
    rtcTests();
    utmiTests();
    spiTests();
    sharedMiuTests();
    displayHardwareTests();
    gopGuestBankTests();
    scalerTimingTests();
    preservedBitsTests();
    combRestoreTests();
    preservedRegisterRestoreTests();
    semaphoreTests();
    geQueueTests();
    readMaskTests();
    gopSelectorProvenanceTests();
    gopPaletteTests();
    tspProvenanceTests();
    tspQueueTests();
    tspIndirectTests();
    tspDownloadTests();
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
