#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unicorn/mips.h>
#include <unicorn/unicorn.h>
namespace manta {
inline void check(uc_err e) {
  if (e != UC_ERR_OK)
    throw std::runtime_error(uc_strerror(e));
}
class Engine {
public:
  uc_engine *handle = nullptr;
  const uint8_t *backing_ = nullptr;
  size_t backingSize_ = 0;
  uint32_t *gpr = nullptr;   // direct view of the engine's general registers (found by calibration; null if unavailable)
  uint64_t writeGen = 0;   // incremented by every host register write except the CP0 timer tick (lets callers cache registers)
  Engine();
  ~Engine();
  Engine(const Engine &) = delete;
  uint32_t reg(int id) const {
    if (gpr && unsigned(id - UC_MIPS_REG_0) < 32u) return gpr[id - UC_MIPS_REG_0];
    if (gpr && id == UC_MIPS_REG_PC) return gpr[32];
    return regSlow(id);
  }
  uint32_t regSlow(int id) const;
  void reg(int id, uint32_t value);
  uc_err run(uint32_t start, uint64_t instructions);
  void map(uint32_t address, size_t size, uint32_t permissions = UC_PROT_ALL);
  void mapShared(uint32_t address, size_t size, void *backing, uint32_t permissions = UC_PROT_ALL);
  void write(uint32_t address, const void *data, size_t size);
  void read(uint32_t address, void *data, size_t size) const {
    if (backing_ && uint64_t(address) + size <= backingSize_) { std::memcpy(data, backing_ + address, size); return; }
    readSlow(address, data, size);
  }
  void readSlow(uint32_t address, void *data, size_t size) const;
  // Guest RAM shared with the engine (mapShared at address 0): reads inside it bypass the engine API.
  void setBacking(const uint8_t *base, size_t size) { backing_ = base; backingSize_ = size; }
  void tickCount(uint32_t delta);
  void calibrateDirectRegisters();
};
} // namespace manta
