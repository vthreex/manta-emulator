#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
namespace manta {
// Stock 803C8250/803C8424: bank 23h, reg41 write/busy bit8,
// reg42 nine-bit destination; original C writer updates only low eight bits.
// The model supports only the observed C banks, not unverified upper banks.
// reg43/44/45 carry five original coefficient bytes.
// Initial idle state and 64-dispatch transfer delay are explicit experiments.
class PqSram {
  friend void ioClass(snap::Ar&, PqSram&);
  struct Transfer { unsigned plane,address; std::array<uint8_t,5> bytes; uint64_t ready; };
  bool idleKnown_=false;
  std::optional<Transfer> pending_;
public:
  std::array<std::array<std::array<uint8_t,5>,512>,2> data{};
  std::array<std::array<bool,512>,2> written{};
  uint64_t submissions=0,commits=0;
  void exploratoryIdle(){idleKnown_=true;}
  bool known()const{return idleKnown_;}
  bool busy()const{return bool(pending_);}
  std::vector<uint8_t> stateBytes()const{
    std::vector<uint8_t> bytes;bytes.reserve(6144);
    for(unsigned plane=0;plane<2;++plane)for(unsigned address=0;address<512;++address){
      bytes.push_back(written[plane][address]?1:0);
      for(auto b:data[plane][address])bytes.push_back(written[plane][address]?b:0);
    }
    return bytes;
  }
  bool advance(uint64_t tick){
    if(!pending_ || tick<pending_->ready)return false;
    auto t=*pending_;data[t.plane][t.address]=t.bytes;written[t.plane][t.address]=true;
    pending_.reset();++commits;return true;
  }
  void submit(unsigned mode,unsigned address,std::array<uint8_t,5> bytes,uint64_t tick){
    advance(tick);
    if(!idleKnown_)throw std::runtime_error("PQ SRAM initial busy state unknown");
    if(pending_)throw std::runtime_error("PQ SRAM write while transfer busy");
    if(mode!=1 && mode!=2)throw std::runtime_error("PQ SRAM unsupported read/write mode");
    if(address>=(mode==1?512u:256u))throw std::runtime_error("PQ SRAM address out of range");
    pending_=Transfer{mode==1?0u:1u,address,bytes,tick+64};++submissions;
  }
};
}
