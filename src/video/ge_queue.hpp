#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
namespace manta {
// Stock 80526240/805265D0 distinguish submission space (11..15),
// execution space (3..7), and busy (0). Both empty counts are 16.
// Stage topology and latency are experimental; no physical reset is implied.
class GeQueue {
  friend void ioClass(snap::Ar&, GeQueue&);
  struct Entry { uint16_t reg, value; uint64_t due; std::function<void()> raster; };
  std::deque<Entry> submission_, execution_;
  bool initialized_=false, running_=false;
  uint64_t submitted_=0, completed_=0, transferred_=0, tick_=0, cursor_=0;
  std::optional<uint16_t> completedTag_;
public:
  static constexpr unsigned capacity=16;
  static constexpr uint16_t statusKnown=0xf8f9;
  static bool aperture(uint32_t a) { return a>=0x1f205000 && a<0x1f205200; }
  void exploratoryReset() { initialized_=running_=true; submission_.clear(); execution_.clear(); }
  bool initialized() const { return initialized_; }
  bool idle() const { return submission_.empty() && execution_.empty(); }
  void clock(bool enabled) { running_=enabled; }
  void advance(uint64_t tick) {
    if(tick<tick_)throw std::runtime_error("GE virtual time moved backwards");
    tick_=tick;
    if(!initialized_ || !running_)return;
    while(true) {
      auto out=execution_.empty()?UINT64_MAX:execution_.front().due;
      auto in=submission_.empty() || execution_.size()==capacity?UINT64_MAX:
          (submission_.front().due>cursor_?submission_.front().due:cursor_);
      auto next=out<in?out:in;
      if(next>tick || next==UINT64_MAX)break;
      cursor_=next;
      if(out<=in) {
        // A failed/unmapped operation never retires or advances the tag.
        if(execution_.front().raster)execution_.front().raster();
        if(execution_.front().reg==0x32)completedTag_=execution_.front().value;
        execution_.pop_front(); ++completed_;
      }
      else {
        auto entry=submission_.front(); submission_.pop_front();
        auto last=execution_.empty()?next:execution_.back().due;
        entry.due=(last>next?last:next)+16;
        execution_.push_back(entry); ++transferred_;
      }
    }
  }
  bool submit(uint16_t reg,uint16_t value,uint64_t tick,std::function<void()> raster={}) {
    advance(tick);
    if(!initialized_ || submission_.size()==capacity)return false;
    // Register 60h is excluded by original context restoration and is the
    // drawing-command trigger. Never retire an undecoded raster operation.
    if(reg==0x60 && !raster)throw std::runtime_error("GE raster command 60h unsupported; no pixels or completion fabricated");
    if(reg!=0x60 && raster)throw std::runtime_error("GE raster work requires original command register");
    auto last=submission_.empty()?tick:submission_.back().due;
    submission_.push_back({reg,value,(last>tick?last:tick)+8,std::move(raster)}); ++submitted_;
    return true;
  }
  uint16_t status() const {
    if(!initialized_)throw std::runtime_error("GE reset/queue occupancy unknown");
    return uint16_t(((capacity-submission_.size())<<11) |
        ((capacity-execution_.size())<<3) | (!execution_.empty()));
  }
  uint64_t submitted() const { return submitted_; }
  uint64_t transferred() const { return transferred_; }
  uint64_t completed() const { return completed_; }
  std::optional<uint16_t> completedTag() const { return completedTag_; }
};
}
