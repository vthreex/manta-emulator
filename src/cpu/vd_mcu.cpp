#include "cpu/vd_mcu.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include <array>
#include <cstddef>
#include <sstream>
namespace manta {
struct VdMcuCpu::Impl {
  struct Context { em8051 cpu{}; Impl* owner; } context{};
  static_assert(offsetof(Context,cpu)==0);
  VdMcuCpu* outer;
  std::array<uint8_t,128> upper{};
  uint64_t executed=0,ticks=0;
  unsigned deferredKeyCount=0;
  static Impl& self(em8051* cpu){return *reinterpret_cast<Context*>(cpu)->owner;}
  static uint8_t xread(em8051* cpu,uint16_t address){
    auto& m=self(cpu);if(m.outer->external)m.outer->external(address,false,0);
    std::ostringstream s;s<<"VD MCU XDATA read unsupported at PC 0x"<<std::hex<<cpu->mPC<<", address 0x"<<address;
    throw VdHardwareBoundary(s.str());
  }
  static void xwrite(em8051* cpu,uint16_t address,uint8_t value){
    auto& m=self(cpu);
    if(m.outer->deferResetKeyWrites && m.deferredKeyCount<4 &&
       address==0x1080+m.deferredKeyCount && value==(m.deferredKeyCount&1?0x55:0xaa)){
      ++m.deferredKeyCount;
      if(m.outer->deferredResetKey)m.outer->deferredResetKey(address,value);
      // Only the observed four writes are deferred. Their hardware effects
      // and every later XDATA read remain unresolved.
      return;
    }
    if(m.outer->external)m.outer->external(address,true,value);
    std::ostringstream s;s<<"VD MCU XDATA write unsupported at PC 0x"<<std::hex<<cpu->mPC<<", address 0x"<<address;
    throw VdHardwareBoundary(s.str());
  }
  static uint8_t unknownSfrRead(em8051* cpu,uint8_t address){
    auto& m=self(cpu);if(m.outer->specialRegister)m.outer->specialRegister(address,false,0);
    std::ostringstream s;s<<"VD MCU nonstandard SFR read unsupported at PC 0x"<<std::hex<<cpu->mPC<<", SFR 0x"<<unsigned(address);
    throw VdHardwareBoundary(s.str());
  }
  static void unknownSfrWrite(em8051* cpu,uint8_t address){
    auto& m=self(cpu);auto value=cpu->mSFR[address-128];
    if(address==0x8e && m.outer->deferSfr8eWrite){
      if(m.outer->deferredSfr8e)m.outer->deferredSfr8e(value);
      // Side effects remain unresolved. Reading this SFR still throws.
      // This explicit diagnostic deferral finds the next external dependency;
      // it does not model CKCON/AUXR or produce clock/ready/status outputs.
      return;
    }
    if(m.outer->specialRegister)m.outer->specialRegister(address,true,value);
    std::ostringstream s;s<<"VD MCU nonstandard SFR write unsupported at PC 0x"<<std::hex<<cpu->mPC<<", SFR 0x"<<unsigned(address)<<", value 0x"<<unsigned(value);
    throw VdHardwareBoundary(s.str());
  }
  static void interruptEnable(em8051* cpu,uint8_t){
    // EA alone cannot admit an interrupt while all individually enabled
    // sources are disabled in this explicitly selected standard reset model.
    if((cpu->mSFR[REG_IE]&0x80) && (cpu->mSFR[REG_IE]&0x7f))
      throw std::runtime_error("VD MCU interrupt-source enable requires verified MStar interrupt/clock wiring");
  }
  static void exception(em8051*,int code){throw std::runtime_error("VD MCU architecture exception "+std::to_string(code));}
};
VdMcuCpu::VdMcuCpu():impl_(std::make_unique<Impl>()){
  impl_->outer=this;impl_->context.owner=impl_.get();
}
VdMcuCpu::~VdMcuCpu()=default;
void VdMcuCpu::startup(VdMcuProgram& program,uint64_t budget){
  auto& m=*impl_;auto& cpu=m.context.cpu;
  if(!program.writes)throw std::runtime_error("VD MCU startup without original uploaded program");
  cpu.mCodeMem=program.codeStorage().data();cpu.mCodeMemMaxIdx=65535;
  cpu.mUpperData=m.upper.data();cpu.xread=Impl::xread;cpu.xwrite=Impl::xwrite;cpu.except=Impl::exception;
  // Never use reset(true): that would erase the guest's own uploaded code.
  reset(&cpu,false);
  // This is an explicit standard 8052 reset experiment. Nonstandard SFRs
  // and all XDATA remain unsupported; none receives a guessed reset value.
  for(unsigned i=0;i<128;++i){cpu.sfrread[i]=Impl::unknownSfrRead;cpu.sfrwrite[i]=Impl::unknownSfrWrite;}
  for(unsigned address:{0x80u,0x81u,0x82u,0x83u,0x87u,0x88u,0x89u,0x8au,0x8bu,0x8cu,0x8du,
       0x90u,0x98u,0x99u,0xa0u,0xa8u,0xb0u,0xb8u,0xd0u,0xe0u,0xf0u}){
    cpu.sfrread[address-128]=nullptr;cpu.sfrwrite[address-128]=nullptr;
  }
  cpu.sfrwrite[REG_IE]=Impl::interruptEnable;
  m.executed=m.ticks=0;m.deferredKeyCount=0;
  while(m.ticks<budget){
    // Upstream tick decrements delay before testing for a new operation.
    // A delay of one therefore also fetches in this tick: validate/trace it.
    if(cpu.mTickDelay<=1){
      auto pc=cpu.mPC;
      if(!program.byteKnown(pc))throw std::runtime_error("VD MCU instruction fetch from unwritten program memory");
      char text[128];auto size=decode(&cpu,pc,text);
      if(!size)throw std::runtime_error("VD MCU idle/powerdown reached without modeled interrupt source");
      for(unsigned i=0;i<size;++i)if(!program.byteKnown(uint16_t(pc+i)))
        throw std::runtime_error("VD MCU instruction operand outside uploaded program");
      auto opcode=cpu.mCodeMem[pc];
      if(opcode==0x83 || opcode==0x93){
        auto base=opcode==0x83?uint16_t(pc+1):uint16_t(cpu.mSFR[REG_DPL]|(cpu.mSFR[REG_DPH]<<8));
        if(!program.byteKnown(uint16_t(base+cpu.mSFR[REG_ACC])))throw std::runtime_error("VD MCU MOVC reads unwritten program byte");
      }
      if(instruction)instruction(pc,opcode,text);
    }
    if(tick(&cpu))++m.executed;
    ++m.ticks;
  }
  throw std::runtime_error("VD MCU bounded startup cycle budget reached; no readiness inferred");
}
uint64_t VdMcuCpu::instructions()const{return impl_->executed;}
uint64_t VdMcuCpu::cycles()const{return impl_->ticks;}
uint16_t VdMcuCpu::pc()const{return impl_->context.cpu.mPC;}
uint8_t VdMcuCpu::stack()const{return impl_->context.cpu.mSFR[REG_SP];}
uint8_t VdMcuCpu::accumulator()const{return impl_->context.cpu.mSFR[REG_ACC];}
uint8_t VdMcuCpu::status()const{return impl_->context.cpu.mSFR[REG_PSW];}
}
