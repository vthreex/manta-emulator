// Differential test, part 1 of 2: execute every recompiled run (straight-line block) natively from random register/memory state and write the
// inputs and results to a vector file. Part 2 (diff_check.cpp, a separate small program linked with Unicorn) replays each vector in the Unicorn MIPS
// interpreter and compares registers, HI/LO, next pc and all scratch memory. Two programs because the Unicorn JIT buffer allocation crashes inside
// the very large static executable that contains the recompiled firmware.
// Runs touching CP0/system state, MMIO, traps or beyond-RAM addresses are skipped (counted). Built with -DRT_TRACE_STORES.
#include "rt.hpp"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct NullBus : Bus {
  uint32_t read(CPU&, uint32_t, unsigned, uint32_t) override { return 0; }
  void write(CPU&, uint32_t, unsigned, uint32_t, uint32_t) override {}
};

static uint64_t rng_state = 0x1234567887654321ull;
static uint32_t rnd() {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return uint32_t(rng_state >> 16);
}
static uint8_t scratch_byte(uint32_t i) { uint32_t h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return uint8_t(h); }

template <class T> static void put(std::vector<uint8_t>& v, T x) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&x); v.insert(v.end(), p, p + sizeof(T)); }

int main(int argc, char** argv) {
  std::string imagePath = argc > 1 ? argv[1] : "runtime_image.bin";
  std::string outPath = argc > 2 ? argv[2] : "diff_vectors.bin";
  unsigned trialsPerRun = argc > 3 ? unsigned(std::atoi(argv[3])) : 4;
  unsigned stride = argc > 4 ? unsigned(std::atoi(argv[4])) : 1;
  const uint32_t RAM = 0x01000000u, base = 0x803b0180u, SCRATCH = 0x00100000u, SCRATCH_LEN = 0x40000u;
  std::ifstream f(imagePath, std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)), {});
  std::vector<uint8_t> nram(RAM, 0), pristine(RAM, 0);
  std::memcpy(pristine.data() + (base & 0x1fffffffu), image.data(), image.size());
  for (uint32_t i = 0; i < SCRATCH_LEN; ++i) pristine[SCRATCH + i] = scratch_byte(i);
  nram = pristine;
  CPU c;
  NullBus nb;
  c.ram = nram.data(); c.ram_size = RAM; c.bus = &nb;

  std::vector<std::pair<uint32_t, uint32_t>> runs(GEN_RUN_COUNT + 1, {0, 0});
  for (uint32_t i = 0; i < GEN_WORDS; ++i) {
    uint32_t id = GEN_INDEX[i];
    if (!id) continue;
    auto& r = runs[id];
    uint32_t pc = GEN_BASE + i * 4;
    if (!r.first) r.first = pc;
    r.second = pc + 4;
  }
  auto restore_all = [&] {   // the store log is capped (256 entries), so restore the whole scratch window plus every logged store
    std::memcpy(nram.data() + SCRATCH, pristine.data() + SCRATCH, SCRATCH_LEN);
    for (unsigned k = 0; k < g_store_n; ++k) std::memcpy(nram.data() + g_store_addr[k], pristine.data() + g_store_addr[k], g_store_len[k]);
  };
  std::vector<uint8_t> out;
  uint64_t records = 0, skippedSys = 0, skippedFault = 0, runsTested = 0;
  for (uint32_t id = 1; id <= GEN_RUN_COUNT; id += stride) {
    auto [s, e] = runs[id];
    if (!s) continue;
    bool sys = false;
    for (uint32_t pc = s; pc < e; pc += 4) {
      uint32_t w; std::memcpy(&w, pristine.data() + (pc & 0x1fffffffu), 4);
      uint32_t op = w >> 26, fn = w & 63;
      if (op == 0x10 || op == 0x11 || op == 0x12 || op == 0x13 || (op == 0 && (fn == 0x0c || fn == 0x0d || (fn >= 0x30 && fn <= 0x36))) || op == 0x2f || op == 0x33 ||
          op == 0x30 || op == 0x38 || (op == 1 && (((w >> 16) & 31) >= 8 && ((w >> 16) & 31) <= 0x0e)))
        { sys = true; break; }
    }
    if (sys) { ++skippedSys; continue; }
    ++runsTested;
    for (unsigned t = 0; t < trialsPerRun; ++t) {
      bool modeB = (t & 1) == 0;   // pointers into scratch vs fully random values
      uint32_t regs[32] = {};
      for (int i = 1; i < 32; ++i) regs[i] = modeB ? (0x80000000u | (SCRATCH + 0x8000u + ((rnd() % 0x4000u) << 2))) : rnd();
      if (modeB && (t & 2)) for (int i = 1; i < 32; ++i) if (rnd() % 4 == 0) regs[i] = rnd() % 64;
      uint32_t hi = rnd(), lo = rnd();
      std::memcpy(c.r, regs, sizeof regs); c.hi = hi; c.lo = lo; c.icount = 0; g_store_n = 0; g_bus_touched = false;
      uint32_t nextN = 0;
      bool threw = false;
      try { nextN = GEN_RUNS[id](&c, s); } catch (...) { threw = true; }
      if (threw || g_bus_touched) {
        ++skippedFault;
        restore_all();
        continue;
      }
      put(out, s); put(out, e); put(out, t);
      for (int i = 0; i < 32; ++i) put(out, regs[i]);
      put(out, hi); put(out, lo);
      put(out, nextN); put(out, uint32_t(c.icount));
      for (int i = 0; i < 32; ++i) put(out, c.r[i]);
      put(out, c.hi); put(out, c.lo);
      put(out, uint32_t(g_store_n));
      for (unsigned k = 0; k < g_store_n; ++k) { put(out, g_store_addr[k]); put(out, g_store_len[k]); }
      for (unsigned k = 0; k < g_store_n; ++k) out.insert(out.end(), nram.data() + g_store_addr[k], nram.data() + g_store_addr[k] + g_store_len[k]);
      ++records;
      restore_all();
    }
  }
  std::ofstream o(outPath, std::ios::binary);
  o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  std::printf("native side: runs tested %llu (skipped system runs %llu), records %llu, skipped (fault/MMIO) %llu\n", (unsigned long long)runsTested,
              (unsigned long long)skippedSys, (unsigned long long)records, (unsigned long long)skippedFault);
  return 0;
}
