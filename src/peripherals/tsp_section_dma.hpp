#pragma once
#include "cpu/openrisc.hpp"
#include <span>
#include <vector>
#include <utility>

namespace manta {
// Candidate section DMA data path, NOT a section parser. Source offsets/counts
// and ring addresses come from guest registers. Original TSP10C4/1B84 use89/09
// for first/continuing section fragments; original115C checks the resulting CRC.
// Unsupported commands, invalid descriptors and insufficient space fail closed.
struct TspSectionDma {
  struct Ring { OrWord start,end,read,current,crc; };
  struct Transfer {std::vector<std::pair<uint32_t,uint8_t>> writes;uint32_t current=0,crc=0;};
  static uint32_t require(OrWord word){
    if(word.known!=0xffffffffu)throw std::runtime_error("TSP section DMA descriptor contains unknown bits");
    return word.value;
  }
  static uint32_t crcByte(uint32_t crc,uint8_t value){
    crc^=uint32_t(value)<<24;
    for(unsigned bit=0;bit<8;++bit)crc=(crc<<1)^((crc&0x80000000u)?0x04c11db7u:0);
    return crc;
  }
  static uint32_t miuAddress(uint32_t address){
    if(address>=0x80000000 && address<0x88000000)return address-0x80000000;
    if(address<0x08000000)return address;
    throw std::runtime_error("TSP DMA address is outside the supported shared DRAM aliases");
  }
  static Transfer plan(std::span<const uint8_t> packet,OrWord offset,OrWord count,OrWord command,const Ring& ring){
    const auto cmd=require(command),source=require(offset),length=require(count);
    if(cmd!=0x89 && cmd!=0x09)throw std::runtime_error("TSP section DMA command not yet modeled");
    if(packet.size()!=188 || source>188 || !length || length>188-source)
      throw std::runtime_error("TSP section DMA exceeds the actual packet");
    const auto start=require(ring.start),end=require(ring.end),read=require(ring.read),cur=require(ring.current);
    if((start&7) || start>=end || read<start || read>=end || cur<start || cur>=end || miuAddress(end-1)-miuAddress(start)!=end-start-1)
      throw std::runtime_error("TSP section DMA ring is outside the supported descriptor layout");
    const uint32_t free=read>cur?read-cur-1:(end-cur)+(read-start)-1;
    if(length>free)throw std::runtime_error("TSP section DMA overflow policy unresolved");
    Transfer transfer;transfer.current=cur;transfer.crc=cmd==0x89?0xffffffffu:require(ring.crc);
    for(unsigned i=0;i<length;++i){const auto byte=packet[source+i];
      transfer.writes.emplace_back(miuAddress(transfer.current),byte);transfer.crc=crcByte(transfer.crc,byte);
      if(++transfer.current==end)transfer.current=start;
    }
    return transfer; // no completion or pointer publication until the caller commits every byte
  }
};
}
