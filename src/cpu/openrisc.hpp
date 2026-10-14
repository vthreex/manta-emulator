#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
namespace manta {
// ORBIS32 subset interpreter, independent of the existing MIPS engine.
// Unknown register/peripheral bits never become successful status values.
struct OrWord {uint32_t value=0,known=0;std::optional<uint32_t> origin;};
class OpenRisc {
  std::array<OrWord,32> r_{};
  std::map<unsigned,uint32_t> spr_;
  uint32_t pc_=0,ppc_=0,sr_=0x8001;
  std::optional<uint32_t> branch_;
  uint64_t instructions_=0,exceptions_=0;
  bool running_=false;
  static uint32_t require(OrWord v);
  static OrWord logic(OrWord a,OrWord b,unsigned op);
  void exception(unsigned vector,uint32_t address);
  uint32_t arithmetic(uint32_t a,uint32_t b,bool sub,bool carry);
public:
  std::function<OrWord(uint32_t,unsigned,bool)> read;
  std::function<void(uint32_t,unsigned,OrWord)> write;
  std::function<void(uint32_t,uint32_t)> trace;
  void reset(uint32_t pc,uint32_t sr=0x8001,std::optional<uint32_t> configuredResetR0=std::nullopt);
  void halt(){running_=false;}
  void step();
  uint32_t pc()const{return pc_;}
  uint32_t sr()const{return sr_;}
  OrWord reg(unsigned n)const{return r_.at(n);}
  uint64_t instructions()const{return instructions_;}
  uint64_t exceptions()const{return exceptions_;}
  bool running()const{return running_;}
  std::vector<uint8_t> stateBytes()const;
};
}
