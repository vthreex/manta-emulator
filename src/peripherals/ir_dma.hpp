#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
namespace manta {
// Original IR driver uses a 16-bit producer index and 48-byte capture records.
// Command 82h reset/index coupling is an explicit experiment. Captures are
// uninterpreted external records, never host-generated menu/key events.
class IrDma {
  std::optional<uint32_t> base_;
  unsigned capacity_=0,index_=0;uint64_t starts_=0,captures_=0;
  bool capturedSinceStart_=false;
public:
  void configure(uint32_t address,unsigned capacity){
    if((address&7) || !capacity || capacity>65535 || uint64_t(address)+uint64_t(capacity)*48>0x10000000)
      throw std::runtime_error("IR DMA capture descriptor outside guest DRAM/alignment bounds");
    base_=address;capacity_=capacity;index_=0;capturedSinceStart_=false;++starts_;
  }
  std::optional<unsigned> producer()const{return base_?std::optional<unsigned>(index_):std::nullopt;}
  std::optional<bool> capturePresent()const{return base_?std::optional<bool>(capturedSinceStart_):std::nullopt;}
  void capture(std::span<const uint8_t> record,const std::function<void(uint32_t,std::span<const uint8_t>)>&store){
    if(!base_ || !store || record.size()!=48)throw std::runtime_error("IR capture requires configured DMA and an exact external 48-byte record");
    store(*base_+index_*48,record);index_=(index_+1)%capacity_;++captures_;capturedSinceStart_=true;
  }
  uint64_t starts()const{return starts_;}
  uint64_t captures()const{return captures_;}
  uint32_t address()const{return base_.value_or(0);}
  unsigned capacity()const{return capacity_;}
};
}
