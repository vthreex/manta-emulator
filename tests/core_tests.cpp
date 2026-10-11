#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "storage/sha256.hpp"
#include "board/unknown_observations.hpp"
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
void spiTests() {
  manta::SpiIsp s;
  require(!s.read(0x5c, 1, 100),
          "SPI engine has no invented command readiness before guest enable");
  s.write(0x18, 0x40, 0);
  s.write(0, 0xaaaa, 100);
  require(s.read(0x5c, 1, 101) == 0 && s.read(0x5c, 1, 164) == 1,
          "SPI command FIFO ready after original engine enable and deadline");
  s.write(4, 5, 200);
  s.write(0x30, 1, 300);
  require(s.read(0x54, 1, 301) == 0 && s.read(0x54, 1, 364) == 1,
          "SPI RDSR data-ready follows command/read strobe");
  require(!s.read(0x14, 255, 364), "unknown NOR protection bits cannot be consumed");
  require((s.read(0x14, 1, 364).value() & 1) == 0,
          "actual idle NOR WIP bit reaches guest status path");
  s.write(0x20, 1, 400);
  s.write(0xa8, 0x3333, 400);
  s.write(0x10, 0x9f, 400);
  for (auto b : {0xef, 0x40, 0x17}) {
    s.write(0x30, 1, 500);
    require((s.read(0x14, 255, 564).value() & 255) == unsigned(b),
            "candidate W25Q64BV JEDEC bytes follow serial read clocks");
  }
  s.write(0x20, 1, 600);
  s.write(0xa8, 0x3333, 600);
  s.write(0x10, 0xcf, 600);
  s.write(0x30, 1, 700);
  require(!s.error.empty(), "unsupported CF command stops instead of inventing chip response");
  manta::SpiIsp noClock;
  noClock.write(0, 0xaaaa, 0);
  require(!noClock.error.empty(), "SPI command readiness requires guest clock configuration");
  manta::SpiIsp read;
  read.flashByte = [](uint32_t a) { return uint8_t(a ^ 0x5a); };
  read.write(0x18, 0x40, 0);
  read.write(0, 0xaaaa, 0);
  read.write(0xa8, 0x3333, 100);
  for (auto b : {3, 0x7f, 0, 0})
    read.write(0x10, uint16_t(b), 100);
  read.write(0x30, 1, 200);
  require((read.read(0x14, 255, 264).value() & 255) == 0x5a,
          "SPI read addresses reach host-backed NOR top sectors");
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
    flashTests();
    riuTests();
    gpioTests();
    miuTests();
    unknownObservationTests();
    rtcTests();
    utmiTests();
    spiTests();
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
