#include "cpu/vd_mcu.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include "storage/sha256.hpp"
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
  std::function<bool(uint16_t)> byteKnown;
  const char* name="VD MCU";
  bool running=false;
  uint64_t total=0,interrupts=0;
  uint8_t aKnown=255;
  uint16_t aOrigin=0;
  bool parityUnknown=false;
  static Impl& self(em8051* cpu){return *reinterpret_cast<Context*>(cpu)->owner;}
  static uint8_t xread(em8051* cpu,uint16_t address){
    auto& m=self(cpu);
    if(m.outer->busRead)if(auto v=m.outer->busRead(address)){
      m.aKnown=v->known;m.aOrigin=v->origin;m.parityUnknown=v->known!=255;
      return v->value;
    }
    if(m.outer->external)m.outer->external(address,false,0);
    std::ostringstream s;s<<m.name<<" XDATA read unsupported at PC 0x"<<std::hex<<cpu->mPC<<", address 0x"<<address;
    throw VdHardwareBoundary(s.str());
  }
  static void xwrite(em8051* cpu,uint16_t address,uint8_t value){
    auto& m=self(cpu);
    if(m.outer->deferResetKeyWrites &&
       address==0x1080+(m.deferredKeyCount%4) && value==(m.deferredKeyCount&1?0x55:0xaa)){
      ++m.deferredKeyCount;
      if(m.outer->deferredResetKey)m.outer->deferredResetKey(address,value);
      // Each complete original sequence re-arms its first address. Unknown
      // effects and every key-register read remain unresolved.
      return;
    }
    if(m.outer->busWrite && m.outer->busWrite(address,{value,m.aKnown,m.aOrigin}))return;
    if(m.outer->external)m.outer->external(address,true,value);
    std::ostringstream s;s<<m.name<<" XDATA write unsupported at PC 0x"<<std::hex<<cpu->mPC<<", address 0x"<<address;
    throw VdHardwareBoundary(s.str());
  }
  static uint8_t unknownSfrRead(em8051* cpu,uint8_t address){
    auto& m=self(cpu);
    if(m.outer->specialRegister)m.outer->specialRegister(address,false,0);
    std::ostringstream s;s<<m.name<<" nonstandard SFR read unsupported at PC 0x"<<std::hex<<cpu->mPC<<", SFR 0x"<<unsigned(address);
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
    std::ostringstream s;s<<m.name<<" nonstandard SFR write unsupported at PC 0x"<<std::hex<<cpu->mPC<<", SFR 0x"<<unsigned(address)<<", value 0x"<<unsigned(value);
    throw VdHardwareBoundary(s.str());
  }
  static void interruptEnable(em8051* cpu,uint8_t){
    // EA alone cannot admit an interrupt while all individually enabled
    // sources are disabled in this explicitly selected standard reset model.
    auto& m=self(cpu);
    if(m.outer->standardInterrupts){
      if(cpu->mSFR[REG_IE]&0x7c)
        throw VdHardwareBoundary("VD MCU interrupt source other than INT0/Timer0 unsupported");
      return;
    }
    if((cpu->mSFR[REG_IE]&0x80) && (cpu->mSFR[REG_IE]&0x7f))
      throw std::runtime_error("VD MCU interrupt-source enable requires verified MStar interrupt/clock wiring");
  }
  static void boundary(em8051* cpu){
    auto& m=self(cpu);auto pc=cpu->mPC;
    if(!m.byteKnown(pc))throw VdHardwareBoundary("VD MCU instruction fetch from unwritten program memory");
    char text[128];auto size=decode(cpu,pc,text);
    if(!size)throw VdHardwareBoundary("VD MCU invalid instruction decode");
    for(unsigned i=0;i<size;++i)if(!m.byteKnown(uint16_t(pc+i)))
      throw VdHardwareBoundary("VD MCU operand outside uploaded program");
    auto opcode=cpu->mCodeMem[pc];
    if(m.aKnown!=255){
      bool independent=opcode==0 || opcode==0x22 || opcode==0x32 || opcode==0x02 ||
        opcode==0x12 || opcode==0x80 || opcode==0x90 || opcode==0xa3 || opcode==0xf0;
      if(opcode==0x54 || opcode==0x44){
        auto bits=cpu->mCodeMem[uint16_t(pc+1)];
        m.aKnown|=opcode==0x54?uint8_t(~bits):bits;
        m.parityUnknown=m.aKnown!=255;independent=true;
      }
      bool overwrite=opcode==0xe4 || opcode==0x74 || opcode==0xe6 || opcode==0xe7 ||
        (opcode>=0xe8 && opcode<=0xef) || opcode==0xe0 ||
        (opcode==0xe5 && cpu->mCodeMem[uint16_t(pc+1)]!=0xd0 && cpu->mCodeMem[uint16_t(pc+1)]!=0xe0);
      if(overwrite){m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;independent=true;}
      if(opcode==0x20 || opcode==0x30){
        auto bit=cpu->mCodeMem[uint16_t(pc+1)];
        independent=bit>=0xe0 && bit<=0xe7 && (m.aKnown&(1u<<(bit-0xe0)));
      }
      if(!independent)throw VdHardwareBoundary("VD MCU unknown XDATA bits affect instruction/decision/address");
    }
    if(m.parityUnknown && ((opcode==0xc0 || opcode==0xe5) && cpu->mCodeMem[uint16_t(pc+1)]==0xd0))
      throw VdHardwareBoundary("VD MCU unknown parity consumed through PSW");
    if(opcode==0x83 || opcode==0x93){
      auto base=opcode==0x83?uint16_t(pc+1):uint16_t(cpu->mSFR[REG_DPL]|(cpu->mSFR[REG_DPH]<<8));
      if(!m.byteKnown(uint16_t(base+cpu->mSFR[REG_ACC])))throw VdHardwareBoundary("VD MCU MOVC reads unwritten code byte");
    }
    if((pc==3 || pc==0xb) && cpu->mInterruptActive)++m.interrupts;
    if(m.outer->instruction)m.outer->instruction(pc,opcode,text);
  }
  static void exception(em8051* cpu,int code){
    // Upstream ISR register-preservation checks are debugger heuristics,
    // not 8051 architectural exceptions. ISR code may intentionally change
    // ACC, PSW or SP. Preserve the actual RETI semantics and report them.
    if(code==EXCEPTION_IRET_PSW_MISMATCH || code==EXCEPTION_IRET_SP_MISMATCH || code==EXCEPTION_IRET_ACC_MISMATCH){
      if(self(cpu).outer->coreDiagnostic)self(cpu).outer->coreDiagnostic(code);
      return;
    }
    throw std::runtime_error("VD MCU unsupported CPU operation "+std::to_string(code));
  }
};
VdMcuCpu::VdMcuCpu():impl_(std::make_unique<Impl>()){
  impl_->outer=this;impl_->context.owner=impl_.get();
}
VdMcuCpu::~VdMcuCpu()=default;
void VdMcuCpu::resetProgram(VdMcuProgram& program){
  impl_->name="VD MCU";
  resetProgramStorage(program.codeStorage(),[&program](uint16_t a){return program.byteKnown(a);},program.writes);
}
void VdMcuCpu::resetProgram(AvdMcuProgram& program){
  impl_->name="AVD MCU";
  if(program.held())throw VdHardwareBoundary("AVD MCU startup while held/reset");
  resetProgramStorage(program.codeStorage(),[&program](uint16_t a){return program.byteKnown(a);},program.writes);
}
void VdMcuCpu::resetPmCandidate(std::vector<uint8_t>& code){
  if(code.size()!=65536 || sha256(code)!=
     "5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c")
    throw VdHardwareBoundary("original PM51 candidate size/hash mismatch");
  impl_->name="PM51 candidate";
  resetProgramStorage(code,[](uint16_t){return true;},code.size());
}
void VdMcuCpu::resetProgramStorage(std::vector<uint8_t>& code,
    std::function<bool(uint16_t)> byteKnown,uint64_t writes){
  auto& m=*impl_;auto& cpu=m.context.cpu;
  if(!writes || code.size()!=65536)throw std::runtime_error("MCU startup without complete uploaded-program storage");
  cpu.mCodeMem=code.data();cpu.mCodeMemMaxIdx=65535;
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
  cpu.instruction_boundary=Impl::boundary;
  m.executed=m.ticks=0;m.deferredKeyCount=0;
  m.byteKnown=std::move(byteKnown);m.running=true;m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;
}
void VdMcuCpu::step(){
  auto& m=*impl_;
  if(!m.running)throw VdHardwareBoundary("VD MCU dispatch while stopped");
  try{if(tick(&m.context.cpu)){++m.executed;++m.total;}++m.ticks;}
  catch(...){m.running=false;throw;}
}
void VdMcuCpu::requestInt0(){
  if(!standardInterrupts)throw VdHardwareBoundary("VD MCU INT0 input unavailable");
  impl_->context.cpu.mSFR[REG_TCON]|=TCONMASK_IE0;
}
bool VdMcuCpu::running()const{return impl_->running;}
uint64_t VdMcuCpu::totalInstructions()const{return impl_->total;}
uint64_t VdMcuCpu::interruptEntries()const{return impl_->interrupts;}
void VdMcuCpu::startup(VdMcuProgram& program,uint64_t budget){
  resetProgram(program);
  while(impl_->ticks<budget)step();
  throw std::runtime_error("VD MCU bounded startup cycle budget reached; no readiness inferred");
}
uint64_t VdMcuCpu::instructions()const{return impl_->executed;}
uint64_t VdMcuCpu::cycles()const{return impl_->ticks;}
uint16_t VdMcuCpu::pc()const{return impl_->context.cpu.mPC;}
uint8_t VdMcuCpu::stack()const{return impl_->context.cpu.mSFR[REG_SP];}
uint8_t VdMcuCpu::accumulator()const{return impl_->context.cpu.mSFR[REG_ACC];}
uint8_t VdMcuCpu::status()const{return impl_->context.cpu.mSFR[REG_PSW];}
}
