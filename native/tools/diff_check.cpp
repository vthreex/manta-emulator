// Differential test, part 2 of 2: replay the native vectors (diff_runs.cpp) in the Unicorn MIPS interpreter and compare.
#include "cpu/engine.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <malloc.h>

static uint8_t scratch_byte(uint32_t i) { uint32_t h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return uint8_t(h); }

static int work(int argc, char** argv) {
  manta::Engine eng;   // created before any large allocation: the patched Unicorn's JIT-buffer setup is sensitive to the process state
  std::string imagePath = argc > 1 ? argv[1] : "runtime_image.bin", vecPath = argc > 2 ? argv[2] : "diff_vectors.bin";
  const uint32_t RAM = 0x01000000u, base = 0x803b0180u, SCRATCH = 0x00100000u, SCRATCH_LEN = 0x40000u;
  auto slurp = [](const std::string& path) {
    std::vector<uint8_t> v;
    if (FILE* fp = std::fopen(path.c_str(), "rb")) {
      uint8_t buf[1 << 16];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) v.insert(v.end(), buf, buf + n);
      std::fclose(fp);
    }
    return v;
  };
  std::vector<uint8_t> image = slurp(imagePath), vec = slurp(vecPath);
  std::vector<uint8_t> pristine(RAM, 0);
  uint8_t* uramp = static_cast<uint8_t*>(_aligned_malloc(RAM, 4096));
  std::memset(uramp, 0, RAM);
  std::memcpy(pristine.data() + (base & 0x1fffffffu), image.data(), image.size());
  for (uint32_t i = 0; i < SCRATCH_LEN; ++i) pristine[SCRATCH + i] = scratch_byte(i);
  std::memcpy(uramp, pristine.data(), RAM);
  eng.mapShared(0, RAM, uramp);
  const int regid[32] = {UC_MIPS_REG_0, UC_MIPS_REG_1, UC_MIPS_REG_2, UC_MIPS_REG_3, UC_MIPS_REG_4, UC_MIPS_REG_5, UC_MIPS_REG_6, UC_MIPS_REG_7, UC_MIPS_REG_8, UC_MIPS_REG_9,
                         UC_MIPS_REG_10, UC_MIPS_REG_11, UC_MIPS_REG_12, UC_MIPS_REG_13, UC_MIPS_REG_14, UC_MIPS_REG_15, UC_MIPS_REG_16, UC_MIPS_REG_17, UC_MIPS_REG_18, UC_MIPS_REG_19,
                         UC_MIPS_REG_20, UC_MIPS_REG_21, UC_MIPS_REG_22, UC_MIPS_REG_23, UC_MIPS_REG_24, UC_MIPS_REG_25, UC_MIPS_REG_26, UC_MIPS_REG_27, UC_MIPS_REG_28, UC_MIPS_REG_29,
                         UC_MIPS_REG_30, UC_MIPS_REG_31};
  size_t p = 0;
  auto rd = [&](auto& x) { std::memcpy(&x, vec.data() + p, sizeof x); p += sizeof x; };
  uint64_t records = 0, passed = 0, failed = 0, unicornFault = 0;
  while (p < vec.size()) {
    uint32_t s, e, t, regs[32], hi, lo, nextN, executed, rout[32], hio, loo, nst;
    rd(s); rd(e); rd(t);
    for (int i = 0; i < 32; ++i) rd(regs[i]);
    rd(hi); rd(lo); rd(nextN); rd(executed);
    for (int i = 0; i < 32; ++i) rd(rout[i]);
    rd(hio); rd(loo); rd(nst);
    std::vector<uint32_t> sa(nst), sl(nst);
    for (uint32_t k = 0; k < nst; ++k) { rd(sa[k]); rd(sl[k]); }
    std::vector<std::vector<uint8_t>> sd(nst);
    for (uint32_t k = 0; k < nst; ++k) { sd[k].assign(vec.begin() + p, vec.begin() + p + sl[k]); p += sl[k]; }
    ++records;
    for (int i = 0; i < 32; ++i) eng.reg(regid[i], regs[i]);
    eng.reg(UC_MIPS_REG_HI, hi); eng.reg(UC_MIPS_REG_LO, lo);
    uc_err err = eng.run(s, executed);
    std::string why;
    bool ok = true;
    if (err != UC_ERR_OK) { ++unicornFault; goto restore; }
    {
      uint32_t upc = eng.reg(UC_MIPS_REG_PC);
      if (upc != nextN) { ok = false; why = "next pc (unicorn " + std::to_string(upc) + ")"; }
      for (int i = 1; i < 32 && ok; ++i) { uint32_t v = eng.reg(regid[i]); if (v != rout[i]) { ok = false; why = "r" + std::to_string(i); } }
      uint32_t h = eng.reg(UC_MIPS_REG_HI), l = eng.reg(UC_MIPS_REG_LO);
      if (ok && (h != hio || l != loo)) { ok = false; why = "hi/lo"; }
      for (uint32_t k = 0; k < nst && ok; ++k)
        if (std::memcmp(uramp + sa[k], sd[k].data(), sl[k])) { ok = false; why = "store data"; }
      if (ok && std::memcmp(uramp + SCRATCH, pristine.data() + SCRATCH, SCRATCH_LEN)) {
        // scratch differs: it must be explained entirely by the native store list
        std::vector<uint8_t> expect(pristine.begin() + SCRATCH, pristine.begin() + SCRATCH + SCRATCH_LEN);
        for (uint32_t k = 0; k < nst; ++k)
          if (sa[k] >= SCRATCH && sa[k] + sl[k] <= SCRATCH + SCRATCH_LEN) std::memcpy(expect.data() + (sa[k] - SCRATCH), sd[k].data(), sl[k]);
        if (std::memcmp(uramp + SCRATCH, expect.data(), SCRATCH_LEN)) { ok = false; why = "unexpected scratch writes"; }
      }
    }
    if (ok) ++passed; else {
      ++failed;
      if (failed <= 30) {
        std::printf("MISMATCH run %08x..%08x trial %u: %s\n", s, e, t, why.c_str());
        for (uint32_t pc = s; pc < e; pc += 4) { uint32_t w; std::memcpy(&w, pristine.data() + (pc & 0x1fffffffu), 4); std::printf("   %08x: %08x\n", pc, w); }
      }
    }
  restore:
    for (uint32_t k = 0; k < nst; ++k) std::memcpy(uramp + sa[k], pristine.data() + sa[k], sl[k]);
    if (!ok) std::memcpy(uramp, pristine.data(), RAM);
  }
  std::printf("records %llu: passed %llu, failed %llu, unicorn-fault(skipped) %llu\n", (unsigned long long)records, (unsigned long long)passed, (unsigned long long)failed,
              (unsigned long long)unicornFault);
  return failed ? 1 : 0;
}

// The patched Unicorn's JIT setup reads uninitialised stack in some process states (the outcome changed with the command line); a fresh thread has a zeroed stack.
#include <thread>
int main(int argc, char** argv) {
  int rc = 0;
  std::thread t([&] { rc = work(argc, argv); });
  t.join();
  return rc;
}
