#pragma once
#include "snapshot/archive.hpp"
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <array>
#include <span>
#include <vector>
#include <algorithm>
namespace manta {
// Stock HAL CmdCount/EmptyCount imply a capacity of 16. Reset polarity and
// coupling to engine-control bit1 are a separate, explicitly selected experiment.
// File-in descriptors may be accepted, but no unimplemented TS DMA is retired.
class TspQueue {
  friend void ioClass(snap::Ar&, TspQueue&);
public:
  struct FileIn { uint32_t address,size; uint16_t control; uint64_t tick; };
  static constexpr unsigned capacity=16;
  static constexpr uint16_t countStatusMask=0x00df;
private:
  std::deque<FileIn> pending_;
  bool resetKnown_=false,held_=true,experimentalResetLow_=false;
  uint64_t resets_=0,releases_=0,submissions_=0;
public:
  void enableExploratoryResetLow() { experimentalResetLow_=true; }
  bool control(uint16_t value,uint16_t known) {
    if(!experimentalResetLow_ || !(known&2))return false;
    if(!(value&2)) {
      if(!resetKnown_ || !held_)++resets_;
      resetKnown_=true;held_=true;pending_.clear();return true;
    }
    if(resetKnown_ && held_) {held_=false;++releases_;return true;}
    return false;
  }
  bool commandReset(uint16_t value,uint16_t known) {
    if(!(known&0x800) || !(value&0x800))return false;
    resetKnown_=true;pending_.clear();++resets_;return true;
  }
  bool known() const {return resetKnown_;}
  bool held() const {return held_;}
  unsigned count() const {
    if(!resetKnown_)throw std::runtime_error("TSP command queue occupancy/reset unknown");
    return unsigned(pending_.size());
  }
  uint16_t status() const {
    auto n=count();return uint16_t(n|(n==capacity?0x40:0)|(n==0?0x80:0));
  }
  void submit(FileIn descriptor) {
    if(!resetKnown_ || held_)throw std::runtime_error("TSP file-in command before known reset release");
    if(pending_.size()==capacity)throw std::runtime_error("TSP command queue full");
    if(!descriptor.size || descriptor.address>0x03ffffff || descriptor.size>0x00ffffff)
      throw std::runtime_error("TSP invalid file-in descriptor");
    pending_.push_back(descriptor);++submissions_;
  }
  void execute(uint64_t) const {
    if(!pending_.empty())throw std::runtime_error("TSP TS DMA/demux execution unsupported; queued data not consumed and command not completed");
  }
  uint64_t resets() const {return resets_;}
  uint64_t releases() const {return releases_;}
  uint64_t submissions() const {return submissions_;}
};
// Firmware download is distinct from file-in CmdQ. Original T4 encodes the
// source in 64-byte units and NUM in four-byte units. Payload is copied into
// internal QMEM; untouched SRAM bytes remain unknown. Bus latency provisional.
class TspDownload {
  friend void ioClass(snap::Ar&, TspDownload&);
  std::array<uint8_t,0x4000> qmem_{};
  std::array<bool,0x4000> valid_{};
  std::vector<uint8_t> payload_;
  uint64_t due_=0,requests_=0,completions_=0;
  bool pending_=false,known_=false,done_=false;
public:
  void start(std::span<const uint8_t> payload,uint64_t tick) {
    if(pending_)throw std::runtime_error("TSP firmware download already active");
    if(payload.empty() || payload.size()>qmem_.size() || (payload.size()&3))
      throw std::runtime_error("TSP firmware download outside QMEM bounds/alignment");
    payload_.assign(payload.begin(),payload.end());pending_=known_=true;done_=false;
    due_=tick+(payload.size()+15)/16;++requests_;
  }
  bool advance(uint64_t tick) {
    if(!pending_ || tick<due_)return false;
    std::copy(payload_.begin(),payload_.end(),qmem_.begin());
    std::fill(valid_.begin(),valid_.begin()+payload_.size(),true);
    pending_=false;done_=true;++completions_;return true;
  }
  bool known() const {return known_;}
  bool done() const {return done_;}
  bool pending() const {return pending_;}
  uint64_t requests() const {return requests_;}
  uint64_t completions() const {return completions_;}
  size_t bytes() const {return payload_.size();}
  std::vector<uint8_t> transferred() const {return {qmem_.begin(),qmem_.begin()+payload_.size()};}
  uint8_t byte(size_t offset) const {
    if(offset>=qmem_.size() || !valid_[offset])throw std::runtime_error("TSP QMEM byte not initialized by a completed transfer");
    return qmem_[offset];
  }
  void byte(size_t offset,uint8_t value) {
    if(offset>=qmem_.size())throw std::runtime_error("TSP QMEM store outside bounds");
    qmem_[offset]=value;valid_[offset]=true;
  }
  std::optional<uint8_t> originalByte(size_t offset)const{
    if(offset>=payload_.size())return {};
    return payload_[offset];
  }
  std::vector<uint8_t> stateBytes()const{
    std::vector<uint8_t> out;out.reserve(qmem_.size()*2);
    for(size_t i=0;i<qmem_.size();++i){out.push_back(valid_[i]);out.push_back(valid_[i]?qmem_[i]:0);}return out;
  }
};
}
