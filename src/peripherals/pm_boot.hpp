#pragma once
#include <optional>
#include <cstdint>
namespace manta {
// Documented PM_PowerOnMode enum in matching MStar driver: ACON=ff,
// standby=1, sleep=2, deep sleep=3. This is an explicit external PM boundary,
// not PM51 execution, a firmware-ready response or a silicon reset default.
class PmBootReason {
  std::optional<uint8_t> cause_;
public:
  void acApplied(){cause_=0xff;}
  void acRemoved(){cause_.reset();}
  std::optional<uint8_t> cause()const{return cause_;}
};
}
