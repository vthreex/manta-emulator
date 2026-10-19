#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
namespace manta {
// Titania4 IR_RPT_FIFOEMPTY3DAD bit1; IR_FIFO_CTRL3DA3 bit7 clears;
// IR_FIFO_READ_PULSE3DB0 bit0 advances. Reset occupancy is UNKNOWN unless
// the explicit cold-empty experiment or an actual guest clear establishes it.
class PmIrFifo {
  struct Entry {uint8_t key;bool repeat;};
  std::deque<Entry> entries_;
  bool known_=false;
  bool rptLatch_=false;                 // HYPOTHESIS: RPT (3DAD bit 0) latches the repeat flag of the entry just popped
  std::optional<bool> lastRepeat_;
  uint64_t clears_=0,pops_=0,received_=0;
public:
  void coldEmptyHypothesis(){entries_.clear();known_=true;}
  void enableRptLatchHypothesis(){rptLatch_=true;}
  void clear(){entries_.clear();known_=true;++clears_;}
  bool known()const{return known_;}
  void uncertainMutation(){if(!known_ || !entries_.empty())known_=false;}
  std::optional<bool> empty()const{return known_?std::optional<bool>(entries_.empty()):std::nullopt;}
  std::optional<uint8_t> key()const{return known_&&!entries_.empty()?std::optional<uint8_t>(entries_.front().key):std::nullopt;}
  // Original804F24B4 and the Titania4 Linux driver read RPT after a pop.
  // Whether that reports the consumed entry, new head or a separate latch is
  // unresolved. Retain external metadata, but never guess its hardware timing.
  std::optional<bool> repeat()const{return rptLatch_?lastRepeat_:std::nullopt;}
  // External decoded packet boundary only. Raw pulse decoding/protocol and
  // host keyboard translation are NOT supplied by this FIFO implementation.
  bool receive(uint8_t key,bool repeat){
    if(!known_)throw std::runtime_error("PM IR packet before known FIFO state");
    if(entries_.size()==8)return false;
    entries_.push_back({key,repeat});++received_;return true;
  }
  void pop(){
    if(!known_)throw std::runtime_error("PM IR read pulse before known FIFO state");
    if(!entries_.empty()){lastRepeat_=entries_.front().repeat;entries_.pop_front();++pops_;}
  }
  size_t size()const{return entries_.size();}
  uint64_t clears()const{return clears_;}
  uint64_t pops()const{return pops_;}
  uint64_t received()const{return received_;}
};
}
