#pragma once
#include "tsp_packet_window.hpp"
#include "tsp_section_dma.hpp"
#include <functional>

namespace manta {
// Candidate unpaced post-FEC TS pin input. Hardware PID/window/DMA work only;
// all section lengths, continuity, software states and notifications remain
// responsibilities of the ORIGINAL TSP program. No service metadata is parsed.
class TspTransport {
  TspPacketWindow window_;
  bool done_=false;
  int station_=-1;
  uint64_t packetIndex_=0;
  std::array<uint8_t,188> pending_{};
  std::vector<unsigned> filters_;
  uint32_t filterIndex_=0;
public:
  std::function<OrWord(uint32_t)> regRead;
  std::function<void(uint32_t,uint32_t)> regWrite;
  std::function<bool(uint32_t,size_t)> memoryWritable;
  std::function<void(uint32_t,std::span<const uint8_t>)> memoryWrite;
  uint64_t inputPackets=0,filteredPackets=0,dmaBytes=0,dmaTransfers=0;
  uint64_t packetCycles=0,nextPacketTick=0,droppedPackets=0,lastInputTick=0;
  std::string inputIdentity;
  uint64_t delivered()const{return window_.captured;}
  unsigned activeFilter()const{return window_.filter();}
  unsigned activeCursor()const{return window_.cursor();}
  void serialize(snap::Ar& a){a.tag("TspTransport1");auto identity=inputIdentity;a(identity);
    if(a.loading() && identity!=inputIdentity)throw snap::SnapshotError("TSP transport input changed since snapshot");
    window_.serialize(a);a(done_,station_,packetIndex_,pending_,filters_,filterIndex_,inputPackets,filteredPackets,dmaBytes,dmaTransfers,packetCycles,nextPacketTick,droppedPackets,lastInputTick);}
  void tune(int station,uint64_t now){if(station!=station_){station_=station;packetIndex_=0;filters_.clear();filterIndex_=0;nextPacketTick=now;}}
  bool hasStation()const{return station_>=0;}
  bool pendingReplica()const{return !window_.occupied() && filterIndex_<filters_.size();}
  bool pumpReplica(){return pendingReplica() && window_.capture(pending_,filters_[filterIndex_++]);}
  bool clockedSupply(int station,std::span<const uint8_t> transport,uint64_t now){
    if(now<lastInputTick)throw std::runtime_error("TSP input clock moved backwards");lastInputTick=now;
    tune(station,now);
    bool delivered=pumpReplica();
    if(station<0 || transport.empty() || now<nextPacketTick)return delivered;
    if(!packetCycles)throw std::runtime_error("paced TSP input has no packet period");
    const auto elapsed=(now-nextPacketTick)/packetCycles;
    if(elapsed==UINT64_MAX || elapsed+1>(UINT64_MAX-nextPacketTick)/packetCycles)throw std::runtime_error("TSP input clock overflow");
    const auto arrivals=elapsed+1;nextPacketTick+=arrivals*packetCycles;
    // Live input cannot pause the transmitter when a one-slot receiver is
    // occupied. Drop that actual arrival; original continuity checks see gaps.
    for(uint64_t i=0;i<arrivals;++i){
      if(window_.occupied() || filterIndex_<filters_.size()){const auto lost=arrivals-i;packetIndex_+=lost;inputPackets+=lost;droppedPackets+=lost;break;}
      delivered=supply(station,transport) || delivered;
    }
    return delivered;
  }
  // Called at an external input boundary, never as a side effect of reading a
  // status register. False means no new packet was delivered to the window.
  bool supply(int station,std::span<const uint8_t> transport){
    tune(station,0);
    if(station<0 || transport.empty() || window_.occupied())return false;
    if(filterIndex_>=filters_.size()){
      filters_.clear();filterIndex_=0;
      if(transport.size()%188)throw std::runtime_error("TSP input is not a packet sequence");
      const auto offset=(packetIndex_++%(transport.size()/188))*188;
      std::copy_n(transport.begin()+offset,188,pending_.begin());++inputPackets;
      const auto pid=uint32_t(pending_[1]&31)<<8|pending_[2];
      for(unsigned i=0;i<48;++i){const auto word=regRead(0x210000+4*i);
        // Unknown programmed filters cannot silently become enabled/disabled.
        if((word.known&0x1fff)!=0x1fff)throw std::runtime_error("TSP input PID filter contains unknown PID bits");
        if((word.value&0x1fff)!=pid)continue;
        if((word.known&0x02200000)!=0x02200000)throw std::runtime_error("TSP input PID routing contains unknown bits");
        if((word.value&0x02200000)!=0x00200000)continue; // section output, LIVE pin input
        if((word.known&0x001f0000)!=0x001f0000)throw std::runtime_error("TSP section filter selection contains unknown bits");
        const auto id=(word.value>>16)&31;
        if(std::find(filters_.begin(),filters_.end(),id)==filters_.end())filters_.push_back(id);
      }
      if(filters_.empty()){++filteredPackets;return false;}
    }
    return window_.capture(pending_,filters_[filterIndex_++]);
  }
  bool ownsWrite(uint32_t a)const{return a==0x210200 || a==0x210214 || a==0x210218;}
  std::optional<OrWord> read(uint32_t address)const{
    auto word=window_.read(address);if(!word || address!=0x210214)return word;
    word->known|=3;word->value|=done_?1:0;
    if(window_.occupied()){
      // Positive match uses actual selected bytes and guest Match/Mask words.
      // Original5C4 handles negative matching in software; it uses this bit9
      // fast path only when its negative-match words are all zero.
      bool uncertain=false,mismatch=false;const auto section=0x211000+128*window_.filter();
      for(unsigned w=0;w<4;++w){const auto pattern=regRead(section+4+4*w),mask=regRead(section+0x14+4*w);
        for(unsigned byte=0;byte<4;++byte){const auto offset=window_.cursor()+4*w+byte,shift=8*byte;
          const auto mk=uint8_t(mask.known>>shift),mv=uint8_t(mask.value>>shift),pk=uint8_t(pattern.known>>shift);
          const auto dataKnown=offset<188?255u:0u;
          const auto diff=offset<188?uint8_t(window_.packet()[offset]^uint8_t(pattern.value>>shift)):0;
          if(diff & pk & dataKnown & mk & ~mv)mismatch=true;
          // Unknown ignore bits are harmless when their data/pattern bits
          // already agree. A single definite mismatch proves false even if
          // unrelated bytes remain opaque; no poison value chooses a result.
          if((~pk | ~dataKnown | diff) & (~mk | ~mv) & 255u)uncertain=true;
        }
      }
      if(mismatch || !uncertain){word->known|=0x200;if(!mismatch)word->value|=0x200;}
    }
    return word;
  }
  bool write(uint32_t address,OrWord word){
    if(address==0x210214){
      if(!(word.known&1))throw std::runtime_error("TSP DMA status write has unknown completion bit");
      if(!(word.value&1))done_=false;
      return window_.write(address,word);
    }
    if(address!=0x210218 || TspPacketWindow::releaseCommand(word))return window_.write(address,word);
    if(!window_.occupied())throw std::runtime_error("TSP section DMA has no received packet");
    const auto section=0x211000+128*window_.filter();
    const auto transfer=TspSectionDma::plan(window_.packet(),regRead(0x210220),regRead(0x210224),word,
      {regRead(section+0x24),regRead(section+0x28),regRead(section+0x2c),regRead(section+0x34),regRead(section+0x3c)});
    if(!memoryWritable || !memoryWrite)throw std::runtime_error("TSP section DMA has no MIU bus");
    for(const auto& [a,v]:transfer.writes)if(!memoryWritable(a,1))throw std::runtime_error("TSP section DMA destination is not guest RAM");
    for(const auto& [a,v]:transfer.writes){const std::array<uint8_t,1> b{v};memoryWrite(a,b);}
    regWrite(section+0x34,transfer.current);regWrite(section+0x3c,transfer.crc);
    dmaBytes+=transfer.writes.size();++dmaTransfers;done_=true;return true;
  }
};
}
