#pragma once
#include <cstdint>
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
  Engine();
  ~Engine();
  Engine(const Engine &) = delete;
  uint32_t reg(int id) const;
  void reg(int id, uint32_t value);
  uc_err run(uint32_t start, uint64_t instructions);
  void map(uint32_t address, size_t size, uint32_t permissions = UC_PROT_ALL);
  void mapShared(uint32_t address, size_t size, void *backing, uint32_t permissions = UC_PROT_ALL);
  void write(uint32_t address, const void *data, size_t size);
  void read(uint32_t address, void *data, size_t size) const;
  void tickCount(uint32_t delta);
};
} // namespace manta
