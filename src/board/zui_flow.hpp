#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
#include "storage/sha256.hpp"
namespace manta {
// Read-only observations of original MIPS globals. No queue producer, event
// injection, guest write, hardware read or CPU control operation exists here.
struct ZuiFlowSnapshot {
  uint32_t pc=0;uint64_t tick=0;
  std::optional<uint32_t> state,previous,active,head,tail,windowCount,windowList;
  std::optional<uint32_t> softwareTick,wrapperState,inputServiceState,inputKey,
    sourceId,sourcePhase,sourceTimeoutTimestamp,softwareFlags;
  std::optional<uint32_t> bitmap196Address,bitmap196Bytes,bitmap196Pitch,bitmap196Width,
    bitmap196Height,bitmap196Format,bitmap196Valid,surfaceFlags,geContextPointer,
    geStaticSignature,geStaticFormat,geStaticDestinationLow,geStaticDestinationHigh,geStaticDestinationPitch;
  std::optional<std::string> bitmap196Sha256;
  unsigned knownSlots=0,occupiedSlots=0;
  bool indicesValid=false;
};
class ZuiFlow {
public:
  using Reader=std::function<std::optional<uint32_t>(uint32_t,unsigned)>;
  static constexpr uint32_t stateAddress=0x006d7e98,previousAddress=0x006d7ea0,
    activeAddress=0x006d7e9c,queueAddress=0x006d4fd0,headAddress=0x006d51b0,
    tailAddress=0x006d51b4,windowCountAddress=0x006b9190,windowListAddress=0x006b9198;
  static constexpr unsigned queueSize=40,entryBytes=12;
  static constexpr std::array<uint32_t,27> watchAddresses{{0x804e0d48u,0x804e0dbcu,0x8046ca74u,0x80488864u,0x8047602cu,
        0x804e0dc4u,0x804c3684u,0x804c370cu,0x804c3574u,0x8049f0dcu,0x8049f040u,
        0x8049f824u,0x804a0640u,0x8049f180u,0x804a0770u,0x8049ee1cu,0x8049dfc4u,
        0x804afdfcu,0x804a071cu,0x80476a10u,0x8046d9dcu,0x80476b88u,
        0x8048848cu,0x80482da8u,0x80480ca8u,0x8046b690u,0x80501bfcu}};
  static bool watches(uint32_t pc){
    for(auto address:watchAddresses)if(pc==address)return true;
    return false;
  }
  static ZuiFlowSnapshot capture(uint32_t pc,uint64_t tick,const Reader& read){
    ZuiFlowSnapshot s;s.pc=pc;s.tick=tick;
    s.state=read(stateAddress,4);s.previous=read(previousAddress,4);s.active=read(activeAddress,4);
    s.head=read(headAddress,4);s.tail=read(tailAddress,4);
    s.windowCount=read(windowCountAddress,2);s.windowList=read(windowListAddress,4);
    // Raw original software state, not decoded peripheral status or a timer
    // producer. Unknown fields stay absent. All addresses are fixed and bounded.
    s.softwareTick=read(0x006e561c,4);s.wrapperState=read(0x006c0918,4);
    s.inputServiceState=read(0x006c08a0,4);s.inputKey=read(0x006b910c,1);
    s.sourceId=read(0x006f8f8c,4);s.sourcePhase=read(0x006d4a08,4);
    s.sourceTimeoutTimestamp=read(0x006b9050,4);s.softwareFlags=read(0x006bedf0,4);
    // Fixed original bitmap table slot and original static GE context. Never
    // follow the data/context pointers or treat these raw fields as scan-out.
    s.bitmap196Address=read(0x006bb54c,4);s.bitmap196Bytes=read(0x006bb550,4);
    s.bitmap196Pitch=read(0x006bb554,2);s.bitmap196Height=read(0x006bb556,2);
    s.bitmap196Width=read(0x006bb558,2);s.bitmap196Format=read(0x006bb55a,1);s.bitmap196Valid=read(0x006bb55b,1);
    s.surfaceFlags=read(0x006d4f9c,4);s.geContextPointer=read(0x006ada64,4);
    s.geStaticSignature=read(0x006ff098,4);s.geStaticFormat=read(0x006febdc,2);
    s.geStaticDestinationLow=read(0x006febc0,2);s.geStaticDestinationHigh=read(0x006febc2,2);
    s.geStaticDestinationPitch=read(0x006febda,2);
    if(s.bitmap196Address==0x03d6e128 && s.bitmap196Bytes==588 && s.bitmap196Pitch==14 &&
       s.bitmap196Width==7 && s.bitmap196Height==42 && s.bitmap196Valid==1){
      // Fixed, bounded original asset range, not an arbitrary guest pointer.
      // A checksum is absent if even one byte retains unknown provenance.
      std::vector<uint8_t> asset;asset.reserve(588);
      for(unsigned i=0;i<588;++i){auto byte=read(0x03d6e128+i,1);if(!byte)break;asset.push_back(uint8_t(*byte));}
      if(asset.size()==588)s.bitmap196Sha256=sha256(asset);
    }
    s.indicesValid=s.head && s.tail && *s.head<queueSize && *s.tail<queueSize;
    // Fixed bounded storage only: never dereference a guest queue index or
    // resource pointer, including an invalid or uncertain value.
    for(unsigned i=0;i<queueSize;++i)if(auto hwnd=read(queueAddress+entryBytes*i,2)){
      ++s.knownSlots;if(*hwnd!=0xffff)++s.occupiedSlots;
    }
    return s;
  }
  std::map<uint32_t,uint64_t> entries;
  std::vector<ZuiFlowSnapshot> snapshots;
  unsigned signatureFailures=0,dropped=0;
  bool record(uint32_t pc,uint64_t tick,const Reader& read,bool signature){
    if(!watches(pc))return false;
    auto count=++entries[pc];if(!signature){++signatureFailures;return false;}
    // Bound diagnostics even if a future guest polls a queue indefinitely.
    if(count>1 || snapshots.size()>=64){++dropped;return false;}
    snapshots.push_back(capture(pc,tick,read));return true;
  }
};
}
