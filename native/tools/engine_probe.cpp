// Probe: does the Unicorn-based Engine initialise inside an executable that also contains the recompiled firmware?
#include "cpu/engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
  size_t big = argc > 1 ? size_t(std::atoi(argv[1])) : 0;   // MB to allocate and touch before creating the engine
  std::vector<char> blob(big << 20, 1);
  std::fprintf(stderr, "allocated %zu MB\n", big);
  manta::Engine e;
  std::fprintf(stderr, "engine constructed\n");
  e.map(0, 0x1000000);
  std::fprintf(stderr, "mapped\n");
  uint32_t nop = 0;
  e.write(0x1000, &nop, 4);
  e.write(0x1004, &nop, 4);
  auto err = e.run(0x1000, 2);
  std::fprintf(stderr, "ran, err=%d pc=%08x\n", int(err), e.reg(UC_MIPS_REG_PC));
  return 0;
}
