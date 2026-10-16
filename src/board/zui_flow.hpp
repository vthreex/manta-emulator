#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
namespace manta {
// Read-only observations of original MIPS globals. No queue producer, event
// injection, guest write, hardware read or CPU control operation exists here.
struct ZuiFlowSnapshot {
  uint32_t pc=0;uint64_t tick=0;
  std::optional<uint32_t> state,previous,active,head,tail,windowCount,windowList;
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
  static bool watches(uint32_t pc){
    for(auto address:{0x804e0d48u,0x804e0dbcu,0x8046ca74u,0x80488864u,0x8047602cu,
        0x804e0dc4u,0x804c3684u,0x804c370cu,0x804c3574u,0x8049f0dcu,0x8049f040u,
        0x8049f824u,0x804a0640u,0x8049f180u,0x804a0770u})if(pc==address)return true;
    return false;
  }
  static ZuiFlowSnapshot capture(uint32_t pc,uint64_t tick,const Reader& read){
    ZuiFlowSnapshot s;s.pc=pc;s.tick=tick;
    s.state=read(stateAddress,4);s.previous=read(previousAddress,4);s.active=read(activeAddress,4);
    s.head=read(headAddress,4);s.tail=read(tailAddress,4);
    s.windowCount=read(windowCountAddress,2);s.windowList=read(windowListAddress,4);
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
