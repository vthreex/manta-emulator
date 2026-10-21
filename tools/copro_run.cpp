// Standalone harness for the multimedia coprocessor firmware (EXPLORATORY, evidence gathering only): runs the original resource 61 image on the
// ORBIS32 core with a flat DRAM and logs every access outside DRAM. Nothing is supplied for unmodelled hardware unless a labelled --hyp is given.
//   copro_run IMAGE.bin [--jpeg FILE@0x3230000] [--max N] [--entry 0x100] [--hyp ADDR=VALUE ...] [--log-io] [--be]
#include "cpu/or1k.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
using namespace manta;
static std::vector<uint8_t> load(const char* p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { std::fprintf(stderr, "cannot read %s\n", p); std::exit(2); }
  return {std::istreambuf_iterator<char>(f), {}};
}
struct Stop { std::string why; };
int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: copro_run IMAGE.bin [--jpeg FILE@ADDR] [--max N] [--entry PC] [--hyp ADDR=VAL] [--log-io] [--be]\n"); return 2; }
  std::vector<uint8_t> ram(0x08000000, 0);
  auto img = load(argv[1]);
  std::memcpy(ram.data(), img.data(), std::min(img.size(), ram.size()));
  uint64_t traceA = 0, traceB = 0; uint32_t dumpAddr = 0, dumpLen = 0; uint32_t watchAddr = 0xffffffff; uint32_t breakPc = 0xffffffff; size_t showLast = 40;
  bool autoZero = false; std::map<uint32_t, uint64_t> zeroReads;
  std::string stopUart; uint64_t afterStop = 0, stopAt = 0;
  struct Poke { uint64_t at; uint32_t addr, val; int irq; bool done = false; };   // EXPLORATORY stimulus: at instruction N set io[addr]=val (16-bit RIU halfword) and raise PIC line irq
  std::vector<Poke> pokes;
  uint64_t maxInstr = 10000000; uint32_t entry = 0x100; bool logIo = false, be = false;
  std::map<uint32_t, uint32_t> hyp, io;   // hypotheses (read values) and last written values
  std::string uart;                        // text written to the 16550-style UART at 0x90000000 (THR)
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--max") maxInstr = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--entry") entry = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--log-io") logIo = true;
    else if (a == "--auto-zero") autoZero = true;
    else if (a == "--trace-win") { traceA = std::strtoull(argv[++i], nullptr, 0); traceB = std::strtoull(argv[++i], nullptr, 0); }
    else if (a == "--dump") { dumpAddr = uint32_t(std::strtoul(argv[++i], nullptr, 0)); dumpLen = uint32_t(std::strtoul(argv[++i], nullptr, 0)); }
    else if (a == "--watch-write") watchAddr = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--break-pc") breakPc = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--show") showLast = std::strtoul(argv[++i], nullptr, 0);
    else if (a == "--stop-uart") stopUart = argv[++i];
    else if (a == "--after") afterStop = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--be") be = true;
    else if (a == "--poke") { Poke k{}; k.at = std::strtoull(argv[++i], nullptr, 0); k.addr = uint32_t(std::strtoul(argv[++i], nullptr, 0)); k.val = uint32_t(std::strtoul(argv[++i], nullptr, 0)); k.irq = int(std::strtol(argv[++i], nullptr, 0)); pokes.push_back(k); }
    else if (a == "--ram") { auto r = load(argv[++i]); std::memcpy(ram.data(), r.data(), std::min(r.size(), ram.size())); }
    else if (a == "--jpeg") {
      std::string s = argv[++i]; auto at = s.find('@'); auto data = load(s.substr(0, at).c_str());
      uint32_t base = uint32_t(std::strtoul(s.c_str() + at + 1, nullptr, 0));
      std::memcpy(ram.data() + base, data.data(), data.size());
    } else if (a == "--hyp") {
      std::string s = argv[++i]; auto eq = s.find('=');
      hyp[uint32_t(std::strtoul(s.c_str(), nullptr, 0))] = uint32_t(std::strtoul(s.c_str() + eq + 1, nullptr, 0));
    }
  }
  Or1k cpu; cpu.bigEndian = be;
  auto dram = [&](uint32_t a, uint32_t& off) { off = a; return a < ram.size(); };   // only the low 128 MiB are DRAM for this CPU; 0xA0200000 is the register window

  cpu.read = [&](uint32_t a, unsigned n) -> uint32_t {
    uint32_t off;
    if (dram(a, off) && off + n <= ram.size()) { uint32_t v = 0; std::memcpy(&v, ram.data() + off, n); return v; }
    if (a >= 0x90000000u && a < 0x90000008u) {   // 16550 UART: transmitter always ready (THRE|TEMT), no received data
      uint32_t v = (a & 7) == 5 ? 0x60u : (a & 7) == 2 ? 0x01u : io.count(a) ? io[a] : 0u;
      if (logIo) std::printf("io r%u %08x = %x (uart)\n", n, a, v);
      return v;
    }
    auto h = hyp.find(a); if (h != hyp.end()) { if (logIo) std::printf("io r%u %08x = %x (HYPOTHESIS)\n", n, a, h->second); return h->second; }
    auto w = io.find(a); if (w != io.end()) { if (logIo) std::printf("io r%u %08x = %x (last written)\n", n, a, w->second); return w->second; }
    if (autoZero) { ++zeroReads[a]; return 0; }   // EXPLORATORY: unmodelled reads return 0 and are listed at the end
    throw Stop{"unmodelled read" + std::to_string(n) + " at " + [&] { char b[16]; std::snprintf(b, sizeof b, "%08x", a); return std::string(b); }()};
  };
  cpu.write = [&](uint32_t a, unsigned n, uint32_t v) {
    uint32_t off;
    if (dram(a, off) && off + n <= ram.size()) {
      if (watchAddr != 0xffffffffu && a < watchAddr + 4 && a + n > watchAddr) std::printf("watch: pc=%08x write%u %08x = %x (n=%llu)\n", cpu.pc(), n, a, v, (unsigned long long)cpu.instructions());
      std::memcpy(ram.data() + off, &v, n); return;
    }
    if (a == 0x90000000u) { bool dlab = io.count(0x90000003u) && (io[0x90000003u] & 0x80); if (!dlab) { uart.push_back(char(v)); return; } }
    io[a] = v; if (logIo) std::printf("io w%u %08x = %x\n", n, a, v);
  };
  cpu.log = [&](const std::string& m) { if (logIo) std::printf("%s\n", m.c_str()); };
  cpu.reset(entry);
  std::deque<uint32_t> last;
  std::string stopWhy = "instruction budget exhausted";
  try {
    while (cpu.instructions() < maxInstr) {
      if (cpu.pc() == breakPc) { stopWhy = "break pc"; break; }
      if (cpu.instructions() >= traceA && cpu.instructions() < traceB) std::printf("T %llu pc=%08x r1=%x r2=%x r3=%x r9=%x r11=%x sr=%x\n", (unsigned long long)cpu.instructions(), cpu.pc(), cpu.reg(1), cpu.reg(2), cpu.reg(3), cpu.reg(9), cpu.reg(11), cpu.sr());
      last.push_back(cpu.pc()); if (last.size() > 600) last.pop_front();
      for (auto& k : pokes) if (!k.done && cpu.instructions() >= k.at) { k.done = true; io[k.addr] = k.val; if (k.irq >= 0) cpu.setIrq(unsigned(k.irq), true); std::printf("POKE at %llu: %08x=%x irq=%d%c", (unsigned long long)cpu.instructions(), k.addr, k.val, k.irq, 10); }
      if (!cpu.step()) { stopWhy = "halted"; break; }
      if (!stopUart.empty() && !stopAt && uart.find(stopUart) != std::string::npos) stopAt = cpu.instructions() + afterStop + 1;
      if (stopAt && cpu.instructions() >= stopAt) { stopWhy = "uart text reached (+after)"; break; }
    }
  } catch (const Stop& s) { stopWhy = s.why; }
  std::printf("stop: %s\ninstructions: %llu pc=%08x sr=%08x\n", stopWhy.c_str(), (unsigned long long)cpu.instructions(), cpu.pc(), cpu.sr());
  {   // return-address candidates found on the stack (a word whose predecessor-minus-8 is a l.jal / l.jalr)
    std::printf("stack return candidates:");
    uint32_t sp = cpu.reg(1);
    for (uint32_t k = 0; k < 400; ++k) {
      uint32_t off = sp + k * 4; if (off + 4 > ram.size()) break;
      uint32_t v; std::memcpy(&v, ram.data() + off, 4);
      if (v >= 16 && v < 0x180000 && !(v & 3)) {
        uint32_t w; std::memcpy(&w, ram.data() + v - 8, 4);
        if ((w >> 26) == 1 || (w >> 26) == 0x12) std::printf(" %x(@+%x)", v - 8, k * 4);
      }
    }
    std::printf("\n");
  }
  if (!zeroReads.empty()) { std::printf("hypothesis reads (value 0):"); for (auto& kv : zeroReads) std::printf(" %08x(x%llu)", kv.first, (unsigned long long)kv.second); std::printf("\n"); }
  if (dumpLen) { std::printf("dump %08x:", dumpAddr); for (uint32_t k = 0; k < dumpLen; k += 4) { uint32_t v; std::memcpy(&v, ram.data() + dumpAddr + k, 4); std::printf("%s%08x", (k % 32) ? " " : "\n  ", v); } std::printf("\n"); }
  std::printf("uart output:\n%s\n", uart.c_str());
  std::printf("last pcs:"); for (size_t k = last.size() > showLast ? last.size() - showLast : 0; k < last.size(); ++k) std::printf(" %x", last[k]); std::printf("\nregs:");
  for (int i = 0; i < 32; ++i) std::printf(" r%d=%x", i, cpu.reg(i));
  std::printf("\n");
  return 0;
}
