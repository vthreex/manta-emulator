// Isolated original AVD MCU program driven by a configurable register provider (exploratory signal-input experiments).
// Every supplied register value is a labelled fixture/hypothesis: nothing here is measured television hardware.
//   avd_signal_probe AVD.bin [--set XDATA VALUE]... [--max N] [--trace-reads]
#include "cpu/vd_mcu.hpp"
#include "peripherals/avd_mcu_bus.hpp"
#include "storage/sha256.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>
static void check(bool c, const char* s) { if (!c) throw std::runtime_error(s); }
int main(int argc, char** argv) { try {
  check(argc >= 2, "usage: avd_signal_probe AVD.bin [--set XDATA VALUE]... [--max N] [--trace-reads]");
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> code(std::istreambuf_iterator<char>(f), {});
  check(code.size() == 13312 && manta::sha256(code) == "48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd", "original AVD size/hash mismatch");
  std::map<uint32_t, manta::VdByte> registers;
  unsigned long maxSteps = 3000000;
  bool traceReads = false, mailboxZero = false, xramZero = false;
  std::map<uint16_t, uint64_t> visits;
  unsigned long fieldPeriod = 0;   // EXPLORATORY: MCU steps per field event (0 = no events)
  unsigned eventByte = 0;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--set" && i + 2 < argc) {
      auto x = unsigned(std::stoul(argv[i + 1], nullptr, 0)); auto v = unsigned(std::stoul(argv[i + 2], nullptr, 0)); i += 2;
      auto phys = manta::AvdMcuBus::sharedAddress(uint16_t(x));
      check(phys && v <= 255, "--set needs a shared 11xx address and a byte");
      registers[*phys] = {uint8_t(v), 255, 0};
    } else if (a == "--max" && i + 1 < argc) { maxSteps = std::stoul(argv[++i], nullptr, 0); }
    else if (a == "--trace-reads") traceReads = true;
    else if (a == "--mailbox-zero") mailboxZero = true;
    else if (a == "--xram-zero") xramZero = true;
    else if (a == "--field-events" && i + 1 < argc) fieldPeriod = std::stoul(argv[++i], nullptr, 0);
    else if (a == "--visit" && i + 1 < argc) visits[uint16_t(std::stoul(argv[++i], nullptr, 0))] = 0;
    else check(false, "unknown option");
  }
  manta::AvdMcuProgram p; p.write(1, 0, 255); p.write(5, 0x51, 255); p.write(8, 0, 255); p.write(9, 0, 255);
  for (auto b : code) p.write(0x18, b, 255);
  p.write(8, 0, 255); p.write(9, 0, 255); for (auto b : code) check(p.read() == b, "AVD port readback mismatch");
  p.write(5, 0, 255); check(p.write(1, 1, 255), "original program release missing");
  manta::VdMcuCpu cpu; manta::AvdMcuBus bus;
  cpu.symbolicCodeTables = true; bus.symbolicTableConfiguration = true; bus.eventAckClearOnly = true; bus.invalidDataPublication = true;
  bus.configureDeferredStartup();
  uint16_t lastRead = 0; unsigned reads = 0;
  // EXPLORATORY (--mailbox-zero): the host/MCU mailbox block 11C0..11DF reads 0 until somebody writes it (reset content unknown)
  bus.sharedRead = [&](uint32_t a) {
    if (fieldPeriod && a == *manta::AvdMcuBus::sharedAddress(0x11fc)) return manta::VdByte{uint8_t(eventByte), 255, 0};
    auto it = registers.find(a);
    if (it != registers.end() && it->second.known) return it->second;
    if (mailboxZero) for (unsigned x = 0x11c0; x < 0x11e0; ++x) if (*manta::AvdMcuBus::sharedAddress(uint16_t(x)) == a) { registers[a] = {0, 255, 0}; return registers[a]; }
    return registers[a];
  };
  bus.sharedWrite = [&](uint32_t a, manta::VdByte v) { registers[a] = v; };
  bus.event = [&](const std::string& k, uint32_t, uint32_t, uint32_t v, const std::string&) { if (k == "avd_event_ack_clear_only") eventByte &= v; };
  cpu.busRead = [&](uint16_t a) { if (a >= 0x1100 && a < 0x1300) { lastRead = a; ++reads; if (traceReads) std::printf("read %04x\n", a); } return bus.read(a); }; cpu.busWrite = [&](uint16_t a, manta::VdByte v) { return bus.write(a, v); };
  cpu.unsupportedSfrWrite = [&](uint8_t a, manta::VdByte v) { return bus.deferSfr(a, v); };
  cpu.standardInterrupts = true;
  cpu.external = [&](uint16_t a, bool w, uint8_t v) {
    if (w && a >= 0x11c0 && a < 0x11e0 && traceReads) std::printf("write %04x <- %02x\n", a, v);
  };
  // EXPLORATORY (--xram-zero): the private XRAM 8000..80FF powers up as zero (the firmware does not initialise it before depending on it)
  if (xramZero) for (unsigned i = 0; i < 256; ++i) bus.write(uint16_t(0x8000 + i), {0, 255, 0});
  cpu.instruction = [&](uint16_t pc, uint8_t, const std::string&) { auto it = visits.find(pc); if (it != visits.end()) ++it->second; };
  cpu.resetProgram(p);
  std::string boundary; unsigned long steps = 0;
  try { for (; steps < maxSteps; ++steps) { if (fieldPeriod && steps && steps % fieldPeriod == 0) eventByte |= 1; cpu.step(); } }
  catch (const manta::VdHardwareBoundary& e) { boundary = e.what(); }
  std::printf("steps %lu pc %04x shared_reads %u last_read %04x boundary: %s\n", steps, unsigned(cpu.pc()), reads, unsigned(lastRead), boundary.empty() ? "(budget)" : boundary.c_str());
  for (auto& [pc, n] : visits) std::printf("visit %04x x%llu\n", unsigned(pc), (unsigned long long)n);
  std::printf("IRAM unknown bytes (addr:value/known@origin):");
  for (unsigned x = 0x00; x < 0x80; ++x) { auto [v, k, o] = cpu.iramInfo(x); if (k != 255) std::printf(" %02x:%02x/%02x@%04x", x, v, k, o); }
  std::printf("\n");
  std::printf("published 11C0..11DF:");
  for (unsigned x = 0x11c0; x < 0x11e0; ++x) {
    auto phys = *manta::AvdMcuBus::sharedAddress(uint16_t(x)); auto it = registers.find(phys);
    if (it == registers.end() || !it->second.known) std::printf(" ??"); else std::printf(" %02x", it->second.value);
    if ((x & 15) == 15) std::printf(" |");
  }
  std::printf("\n");
  return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; } }
