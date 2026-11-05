#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <span>
namespace manta {
// Original 805393xx uploads a 32-bit color to registers 3/4, selects an
// index in register5, and pulses bit8 with mode bits12/13 set. No reset
// colors or read-mode responses are supplied.
class GopPalette {
  friend void ioClass(snap::Ar&, GopPalette&);
  struct Pending { uint8_t index; uint32_t color; uint16_t mode; };
  std::array<std::array<std::optional<uint32_t>,256>,2> colors_{};
  std::array<std::array<std::optional<uint16_t>,256>,2> modes_{};
  std::array<std::optional<Pending>,2> pending_{};
  uint64_t commits_=0;
public:
  void loadDma(unsigned bank,std::span<const uint8_t> bytes) {
    if((bank!=0 && bank!=3) || bytes.empty() || bytes.size()%8 || bytes.size()>4096)
      throw std::runtime_error("GOP palette DMA framing unsupported");
    const unsigned plane=bank==0?0:1;
    if(pending_[plane])throw std::runtime_error("GOP palette DMA overlaps a CPU pulse");
    for(size_t at=0;at<bytes.size();at+=8)
      if(bytes[at+5] || bytes[at+6] || bytes[at+7])throw std::runtime_error("GOP palette DMA record reserved bytes nonzero");
    for(size_t at=0;at<bytes.size();at+=8) {
      const unsigned index=bytes[at+4];
      colors_[plane][index]=uint32_t(bytes[at])|(uint32_t(bytes[at+1])<<8)|(uint32_t(bytes[at+2])<<16)|(uint32_t(bytes[at+3])<<24);
      modes_[plane][index]=0x1000;++commits_;
    }
  }
  bool pulse(unsigned bank,uint16_t control,uint16_t known,uint32_t color,uint32_t colorKnown) {
    if(bank!=0 && bank!=3)throw std::runtime_error("GOP palette bank unsupported");
    auto plane=bank==0?0u:1u;
    auto mode=uint16_t(control&0x3000);
    if((known&0x3100)!=0x3100 || (mode!=0x2000 && mode!=0x3000))return false;
    if((known&255)!=255 && ((control&0x100) || pending_[plane]))
      throw std::runtime_error("GOP palette command contains unknown index bits");
    auto index=uint8_t(control);
    if(control&0x100) {
      if(colorKnown!=0xffffffffu)throw std::runtime_error("GOP palette command contains unknown color bits");
      pending_[plane]=Pending{index,color,mode};return false;
    }
    if(!pending_[plane])return false;
    auto prior=*pending_[plane];
    if(prior.index!=index || prior.color!=color || prior.mode!=mode || colorKnown!=0xffffffffu)
      throw std::runtime_error("GOP palette data/index changed during original write pulse");
    colors_[plane][index]=color;modes_[plane][index]=mode;pending_[plane].reset();++commits_;return true;
  }
  std::optional<uint32_t> color(unsigned plane,unsigned index) const { return colors_.at(plane).at(index); }
  std::optional<uint16_t> mode(unsigned plane,unsigned index) const { return modes_.at(plane).at(index); }
  unsigned defined(unsigned plane) const {
    unsigned n=0;for(auto v:colors_.at(plane))if(v)++n;return n;
  }
  uint64_t commits() const { return commits_; }
};
}
