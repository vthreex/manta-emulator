#include "cpu/engine.hpp"
namespace manta {
Engine::Engine() {
  check(uc_open(UC_ARCH_MIPS, uc_mode(UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN), &handle));
  check(uc_ctl_set_cpu_model(handle, UC_CPU_MIPS32_24KC));
}
Engine::~Engine() {
  if (handle)
    uc_close(handle);
}
uint32_t Engine::reg(int id) const {
  uint32_t v = 0;
  check(uc_reg_read(handle, id, &v));
  return v;
}
void Engine::reg(int id, uint32_t v) { check(uc_reg_write(handle, id, &v)); }
uc_err Engine::run(uint32_t start, uint64_t n) {
  return uc_emu_start(handle, start, 0, 0, size_t(n));
}
void Engine::map(uint32_t a, size_t n, uint32_t p) { check(uc_mem_map(handle, a, n, p)); }
void Engine::mapShared(uint32_t a, size_t n, void *backing, uint32_t p) {
  check(uc_mem_map_ptr(handle, a, n, p, backing));
}
void Engine::write(uint32_t a, const void *p, size_t n) { check(uc_mem_write(handle, a, p, n)); }
void Engine::read(uint32_t a, void *p, size_t n) const { check(uc_mem_read(handle, a, p, n)); }
void Engine::tickCount(uint32_t delta) {
#ifdef UC_MIPS_MANTA_TIMER_API
  reg(UC_MIPS_REG_CP0_TICK, delta);
#else
  (void)delta;
  throw std::runtime_error("--cp0-timer requires the source-built Unicorn timer extension");
#endif
}
} // namespace manta
