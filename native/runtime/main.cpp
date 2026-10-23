// manta_native: the Manta LCD4214 firmware, statically recompiled to native code (milestone 1: CPU only, minimal bus).
#include "rt.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

uint64_t run_guest(CPU& c, uint32_t pc, uint64_t limit);

// Minimal lenient bus: UART capture, flash window, everything else reads 0 (counted). The interpreter-based emulator models the real peripherals.
struct SimpleBus : Bus {
  std::vector<uint8_t> flash;
  std::map<uint32_t, uint64_t> unknownReads, unknownWrites;
  std::string uart;
  uint32_t read(CPU&, uint32_t a, unsigned n, uint32_t) override {
    uint32_t p = a & 0x1fffffffu;
    if (p >= 0x1fc00000u && !flash.empty()) {
      uint32_t off = p - 0x1fc00000u, v = 0;
      for (unsigned i = 0; i < n && off + i < flash.size(); ++i) v |= uint32_t(flash[off + i]) << (8 * i);
      return v;
    }
    ++unknownReads[p & ~3u];
    return 0;
  }
  void write(CPU&, uint32_t a, unsigned n, uint32_t v, uint32_t) override {
    uint32_t p = a & 0x1fffffffu;
    if (p == 0x1f201300u) { uart.push_back(char(v & 255)); std::fputc(int(v & 255), stdout); return; }
    ++unknownWrites[p & ~3u];
  }
};

int main(int argc, char** argv) {
  std::string imagePath, flashPath;
  uint64_t limit = 100000000;
  uint32_t base = 0x803b0180u, entry = 0x803b0600u;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--image" && i + 1 < argc) imagePath = argv[++i];
    else if (a == "--flash" && i + 1 < argc) flashPath = argv[++i];
    else if (a == "--instructions" && i + 1 < argc) limit = std::stoull(argv[++i], nullptr, 0);
    else if (a == "--entry" && i + 1 < argc) entry = uint32_t(std::stoul(argv[++i], nullptr, 0));
  }
  if (imagePath.empty()) { std::fprintf(stderr, "usage: manta_native --image runtime_image.bin [--flash flash.bin] [--instructions N]\n"); return 2; }
  std::ifstream f(imagePath, std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)), {});
  CPU c;
  c.ram_size = 0x08000000u;
  std::vector<uint8_t> ram(c.ram_size, 0);
  c.ram = ram.data();
  std::memcpy(ram.data() + (base & 0x1fffffffu), image.data(), image.size());
  SimpleBus bus;
  if (!flashPath.empty()) { std::ifstream ff(flashPath, std::ios::binary); bus.flash.assign(std::istreambuf_iterator<char>(ff), {}); }
  c.bus = &bus;
  c.cp0[12][0] = 0x10400000u;   // Status: ASSUMPTION for the runtime entry state (CU0 set, BEV clear)
  auto t0 = std::chrono::steady_clock::now();
  uint64_t n = run_guest(c, entry, limit);
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "\nexecuted %llu guest instructions in %.2f s (%.1f MIPS); stopped at pc %08x; timer irqs %llu, external %llu, exceptions %llu; unknown MMIO read addrs %zu, write addrs %zu\n",
               (unsigned long long)n, s, s > 0 ? n / s / 1e6 : 0.0, c.last_pc, (unsigned long long)c.timer_irqs, (unsigned long long)c.ext_irqs, (unsigned long long)c.exceptions,
               bus.unknownReads.size(), bus.unknownWrites.size());
  size_t shown = 0;
  for (auto& [a, k] : bus.unknownReads) if (shown++ < 12) std::fprintf(stderr, "  read  %08x x%llu\n", a, (unsigned long long)k);
  return c.fatal ? 1 : 0;
}
