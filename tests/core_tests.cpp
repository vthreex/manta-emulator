#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "storage/sha256.hpp"
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
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
