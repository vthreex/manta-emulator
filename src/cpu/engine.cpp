#include "cpu/engine.hpp"
namespace manta {
Engine::Engine() {
  check(uc_open(UC_ARCH_MIPS, uc_mode(UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN), &handle));
  check(uc_ctl_set_cpu_model(handle, cpuModel));
  calibrateDirectRegisters();
}
Engine::~Engine() {
  if (handle)
    uc_close(handle);
}
uint32_t Engine::regSlow(int id) const {
  uint32_t v = 0;
  check(uc_reg_read(handle, id, &v));
  return v;
}
void Engine::reg(int id, uint32_t v) { ++writeGen; check(uc_reg_write(handle, id, &v)); }
#ifdef _WIN32
#include <windows.h>
#endif
// Find the engine's own copy of the general registers so the hot path can read them without an API call: write a distinctive value into
// every register, look for that pattern in the memory reachable from the engine object (two pointer levels), then clear the registers again.
// The caller cross-checks the view against uc_reg_read before relying on it; any failure leaves gpr == nullptr (slow path).
void Engine::calibrateDirectRegisters() {
#ifdef _WIN32
  uint32_t pat[32];
  for (int i = 1; i < 32; ++i) { pat[i] = 0xC0DE0000u + uint32_t(i) * 0x0101u; check(uc_reg_write(handle, UC_MIPS_REG_0 + i, &pat[i])); }
  auto readable = [](const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return reinterpret_cast<const char *>(p) + n <= reinterpret_cast<const char *>(mbi.BaseAddress) + mbi.RegionSize;
  };
  auto matches = [&](const uint32_t *q) {
    if (!readable(q, 34 * 4)) return false;
    for (int i = 1; i < 32; ++i) if (q[i] != pat[i]) return false;
    return q[0] == 0;
  };
  const uint32_t *found = nullptr;
  const auto *l0 = reinterpret_cast<const uintptr_t *>(handle);
  for (size_t i = 0; i < 2048 && !found; ++i) {
    if (!readable(l0 + i, sizeof(uintptr_t))) break;
    const auto *p1 = reinterpret_cast<const uintptr_t *>(l0[i]);
    if (!readable(p1, 8192)) continue;
    for (size_t j = 0; j < 1024 && !found; ++j) {
      const auto *p2 = reinterpret_cast<const uint32_t *>(p1[j]);
      if (matches(p2)) found = p2;
    }
  }
  for (int i = 1; i < 32; ++i) { uint32_t z = 0; check(uc_reg_write(handle, UC_MIPS_REG_0 + i, &z)); }
  if (found) gpr = const_cast<uint32_t *>(found);
#endif
}
uc_err Engine::run(uint32_t start, uint64_t n) {
  return uc_emu_start(handle, start, 0, 0, size_t(n));
}
void Engine::map(uint32_t a, size_t n, uint32_t p) { check(uc_mem_map(handle, a, n, p)); }
void Engine::mapShared(uint32_t a, size_t n, void *backing, uint32_t p) {
  check(uc_mem_map_ptr(handle, a, n, p, backing));
}
void Engine::write(uint32_t a, const void *p, size_t n) { check(uc_mem_write(handle, a, p, n)); }
void Engine::readSlow(uint32_t a, void *p, size_t n) const { check(uc_mem_read(handle, a, p, n)); }
void Engine::tickCount(uint32_t delta) {
#ifdef UC_MIPS_MANTA_TIMER_API
  uint32_t v = delta;
  check(uc_reg_write(handle, UC_MIPS_REG_CP0_TICK, &v));   // timer tick only: general registers are untouched
#else
  (void)delta;
  throw std::runtime_error("--cp0-timer requires the source-built Unicorn timer extension");
#endif
}
void Engine::setIrqLines(uint32_t mask) {
#ifdef UC_MIPS_MANTA_IRQ_API
  check(uc_reg_write(handle, UC_MIPS_REG_CP0_IRQ, &mask));
#else
  (void)mask;
  throw std::runtime_error("MIPS external interrupt lines require the Manta IRQ extension of the engine (tools/patch_unicorn_irq.py)");
#endif
}
} // namespace manta
