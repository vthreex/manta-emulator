#pragma once
#include <cstdint>
#include <optional>
namespace manta {
// T4's original DVB-T MCU uses TOP/FEC XDATA 20xx..25xx. The original
// MIPS mailbox sends those same 16-bit addresses. Related T4 driver symbols
// name their RIU banks 1120xx..1125xx. Reset values/effects are NOT inferred.
inline std::optional<uint32_t> dvbtSharedAddress(uint16_t address) {
  if(address<0x2000 || address>=0x2600)return {};
  return 0x1f220000u+(uint32_t(address&0xfffe)<<1)+(address&1);
}
}
