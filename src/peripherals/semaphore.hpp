#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace manta {
// Original HAL at 8053B924..BA20: 16-bit resource IDs, four-byte RIU stride,
// zero releases/resets. Reset ownership is unknown unless explicitly supplied.
class HardwareSemaphore {
  std::array<std::optional<uint16_t>,9> owners_{};
  uint64_t acquisitions_=0,releases_=0,contentions_=0;
public:
  static bool aperture(uint32_t a) { return a>=0x1f203000 && a<0x1f203024; }
  static unsigned slot(uint32_t a,unsigned n) {
    if(!aperture(a) || (a&3) || n!=2)throw std::runtime_error("Unverified semaphore register lane/access width");
    return (a-0x1f203000)/4;
  }
  void exploratoryIdle() { for(auto &v:owners_)v=0; }
  auto owner(uint32_t a,unsigned n) const { return owners_.at(slot(a,n)); }
  bool write(uint32_t a,unsigned n,uint16_t id) {
    auto &v=owners_.at(slot(a,n));
    if(!id) { v=0;++releases_;return true; }
    if(!v)return false; // unknown initial owner must not be replaced by success
    if(*v && *v!=id) { ++contentions_;return true; } // failed acquire: retain owner
    if(!*v)++acquisitions_;
    v=id;return true;
  }
  uint64_t acquisitions() const { return acquisitions_; }
  uint64_t releases() const { return releases_; }
  uint64_t contentions() const { return contentions_; }
};
}
