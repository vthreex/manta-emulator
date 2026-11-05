#pragma once
#include <cstddef>
#include <cstdint>
namespace manta {
inline bool mappedDramRange(uint32_t address, size_t size, uint32_t miu0, uint32_t miu1, size_t backing) {
  if (!size || address >= backing || size > backing-address) return false;
  const uint64_t end=uint64_t(address)+size;
  return (address<miu0 && end<=miu0) ||
         (address>=0x08000000u && end<=0x08000000ull+miu1);
}
}
