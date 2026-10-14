#include "cpu/vd_mcu.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include "storage/sha256.hpp"
#include <array>
#include <cstddef>
#include <exception>
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
  std::array<uint8_t,32> registerKnown{};
  std::array<uint16_t,32> registerOrigin{};
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
    // Failed validation must not commit metadata for an unexecuted copy,
    // push or address operation. Architectural registers remain untouched.
    struct Rollback {
      Impl& m;decltype(registerKnown) known;decltype(registerOrigin) origins;
      uint8_t a;uint16_t origin;bool parity;int exceptions;
      explicit Rollback(Impl& state):m(state),known(state.registerKnown),origins(state.registerOrigin),
        a(state.aKnown),origin(state.aOrigin),parity(state.parityUnknown),exceptions(std::uncaught_exceptions()){}
      ~Rollback(){if(std::uncaught_exceptions()>exceptions){m.registerKnown=known;m.registerOrigin=origins;
        m.aKnown=a;m.aOrigin=origin;m.parityUnknown=parity;}}
    } rollback(m);
    // Rn are banked IRAM bytes, not permanently known values. Preserve an
    // invalid MOVX result across register copies and reject unmodeled uses.
    auto operand=cpu->mCodeMem[uint16_t(pc+1)];
    auto sp=cpu->mSFR[REG_SP];
    if((opcode==0x22 || opcode==0x32 || opcode==0xd0) &&
       ((sp<32 && m.registerKnown[sp]!=255) ||
        (opcode!=0xd0 && uint8_t(sp-1)<32 && m.registerKnown[uint8_t(sp-1)]!=255)))
      throw VdHardwareBoundary("MCU unknown IRAM register alias consumed through stack/return address");
    auto bank=cpu->mSFR[REG_PSW]&0x18;
    auto index=unsigned(bank+(opcode&7));
    bool registerLoad=opcode>=0xe8 && opcode<=0xef;
    bool registerStore=opcode>=0xf8;
    bool registerImmediate=opcode>=0x78 && opcode<=0x7f;
    bool directRegisterLoad=opcode==0xe5 && operand<32;
    if(registerStore){m.registerKnown[index]=m.aKnown;m.registerOrigin[index]=m.aOrigin;}
    if(registerImmediate){m.registerKnown[index]=255;m.registerOrigin[index]=0;}
    if(opcode==0x75 && operand<32){m.registerKnown[operand]=255;m.registerOrigin[operand]=0;}
    if(opcode==0xf5 && operand<32){m.registerKnown[operand]=m.aKnown;m.registerOrigin[operand]=m.aOrigin;}
    auto usesRn=[&]{
      return (opcode>=0x08 && opcode<=0x0f) || (opcode>=0x18 && opcode<=0x1f) ||
        (opcode>=0x28 && opcode<=0x2f) || (opcode>=0x38 && opcode<=0x3f) ||
        (opcode>=0x48 && opcode<=0x4f) || (opcode>=0x58 && opcode<=0x5f) ||
        (opcode>=0x68 && opcode<=0x6f) || (opcode>=0x88 && opcode<=0x8f) ||
        (opcode>=0x98 && opcode<=0x9f) || (opcode>=0xb8 && opcode<=0xbf) ||
        (opcode>=0xc8 && opcode<=0xcf) || (opcode>=0xd8 && opcode<=0xdf);
    };
    if(usesRn() && m.registerKnown[index]!=255)
      throw VdHardwareBoundary("MCU unknown register bits affect operation/address/decision");
    // Conservative direct/indirect guards: transfers not yet tracked must
    // stop rather than laundering an invalid Rn through another IRAM alias.
    bool directSource=opcode==0x05 || opcode==0x15 || opcode==0x25 || opcode==0x35 ||
      opcode==0x45 || opcode==0x55 || opcode==0x65 || opcode==0x85 || opcode==0x95 ||
      opcode==0xb5 || opcode==0xc0 || opcode==0xc5 || opcode==0xd5 ||
      (opcode>=0xa8 && opcode<=0xaf);
    if(directSource && operand<32 && m.registerKnown[operand]!=255)
      throw VdHardwareBoundary("MCU unknown register bits consumed through direct IRAM alias");
    if(opcode==0x12 || (opcode&0x1f)==0x11 || opcode==0xc0){
      unsigned count=opcode==0xc0?1:2;
      for(unsigned i=1;i<=count;++i){auto address=uint8_t(sp+i);
        if(address<32){m.registerKnown[address]=255;m.registerOrigin[address]=0;}}
    }
    if(opcode==0xd0 && operand<32){m.registerKnown[operand]=255;m.registerOrigin[operand]=0;}
    if(opcode>=0xa8 && opcode<=0xaf){m.registerKnown[index]=255;m.registerOrigin[index]=0;}
    if((opcode>=0x88 && opcode<=0x8f) && operand<32){m.registerKnown[operand]=255;m.registerOrigin[operand]=0;}
    if(opcode==0x85){auto destination=cpu->mCodeMem[uint16_t(pc+2)];
      if(destination<32){m.registerKnown[destination]=255;m.registerOrigin[destination]=0;}}
    bool indirect=(opcode&0x0e)==6 || opcode==0xe2 || opcode==0xe3 || opcode==0xf2 || opcode==0xf3;
    if(indirect && m.registerKnown[bank+(opcode&1)]!=255)
      throw VdHardwareBoundary("MCU unknown register used as indirect address");
    std::optional<unsigned> indirectRegisterLoad;
    if((opcode&0x0e)==6){
      auto address=cpu->mLowerData[bank+(opcode&1)];
      bool writes=opcode==0x76 || opcode==0x77 || opcode==0xa6 || opcode==0xa7 || opcode==0xf6 || opcode==0xf7;
      if(address<32){
        if(opcode==0xe6 || opcode==0xe7)indirectRegisterLoad=address;
        else if(!writes && m.registerKnown[address]!=255)
          throw VdHardwareBoundary("MCU unknown register bits consumed through indirect IRAM alias");
        else if(writes){
          if(opcode==0xf6 || opcode==0xf7){m.registerKnown[address]=m.aKnown;m.registerOrigin[address]=m.aOrigin;}
          else {m.registerKnown[address]=255;m.registerOrigin[address]=0;}
        }
      }
    }
    if(m.aKnown!=255){
      bool independent=opcode==0 || opcode==0x22 || opcode==0x32 || opcode==0x02 ||
        opcode==0x12 || opcode==0x80 || opcode==0x90 || opcode==0xa3 || opcode==0xf0;
      if(opcode==0x54 || opcode==0x44 || opcode==0x64){
        auto bits=cpu->mCodeMem[uint16_t(pc+1)];
        if(opcode!=0x64)m.aKnown|=opcode==0x54?uint8_t(~bits):bits;
        m.parityUnknown=m.aKnown!=255;independent=true;
      }
      bool overwrite=opcode==0xe4 || opcode==0x74 || opcode==0xe6 || opcode==0xe7 ||
        registerLoad || opcode==0xe0 ||
        (opcode==0xe5 && cpu->mCodeMem[uint16_t(pc+1)]!=0xd0 && cpu->mCodeMem[uint16_t(pc+1)]!=0xe0);
      if(overwrite){m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;independent=true;}
      if(registerStore || registerImmediate || (opcode==0x75 && operand<32))independent=true;
      if(opcode==0x20 || opcode==0x30){
        auto bit=cpu->mCodeMem[uint16_t(pc+1)];
        independent=bit>=0xe0 && bit<=0xe7 && (m.aKnown&(1u<<(bit-0xe0)));
      }
      if(!independent)throw VdHardwareBoundary(std::string(m.name)+" unknown XDATA bits affect instruction/decision/address");
    }
    if(registerLoad || directRegisterLoad || indirectRegisterLoad){
      auto source=indirectRegisterLoad?*indirectRegisterLoad:registerLoad?index:unsigned(operand);
      m.aKnown=m.registerKnown[source];m.aOrigin=m.registerOrigin[source];
      m.parityUnknown=m.aKnown!=255;
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
void VdMcuCpu::resetPmCandidate(std::span<const uint8_t> code,std::function<bool(uint16_t)> sourceValid){
  if(code.size()!=65536 || sha256(code)!=
     "5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c")
    throw VdHardwareBoundary("original PM51 candidate size/hash mismatch");
  impl_->name="PM51 candidate";
  resetProgramStorage(code,sourceValid?std::move(sourceValid):[](uint16_t){return true;},code.size());
}
void VdMcuCpu::resetProgramStorage(std::span<const uint8_t> code,
    std::function<bool(uint16_t)> byteKnown,uint64_t writes){
  auto& m=*impl_;auto& cpu=m.context.cpu;
  if(!writes || code.size()!=65536)throw std::runtime_error("MCU startup without complete uploaded-program storage");
  // The upstream interpreter has a mutable pointer type but no instruction
  // writes CODE memory. reset(false) preserves the shared flash/program view.
  cpu.mCodeMem=const_cast<uint8_t*>(code.data());cpu.mCodeMemMaxIdx=65535;
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
  m.registerKnown.fill(255);m.registerOrigin.fill(0);
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
uint8_t VdMcuCpu::port2()const{return impl_->context.cpu.mSFR[REG_P2];}
uint8_t VdMcuCpu::accumulatorKnown()const{return impl_->aKnown;}
uint16_t VdMcuCpu::accumulatorOrigin()const{return impl_->aOrigin;}
std::vector<uint8_t> VdMcuCpu::stateBytes()const{
  const auto& m=*impl_;const auto& c=m.context.cpu;std::vector<uint8_t> b;
  auto append=[&](uint64_t value,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(uint8_t(value>>(8*i)));};
  b.insert(b.end(),std::begin(c.mLowerData),std::end(c.mLowerData));
  b.insert(b.end(),m.upper.begin(),m.upper.end());b.insert(b.end(),std::begin(c.mSFR),std::end(c.mSFR));
  append(c.mPC,2);append(c.mTickDelay,1);append(c.mInterruptActive,1);
  b.insert(b.end(),std::begin(c.int_a),std::end(c.int_a));b.insert(b.end(),std::begin(c.int_psw),std::end(c.int_psw));
  b.insert(b.end(),std::begin(c.int_sp),std::end(c.int_sp));
  b.insert(b.end(),std::begin(c.serial_out),std::end(c.serial_out));
  append(c.serial_out_idx,1);append(c.serial_out_remaining_bits,1);append(c.serial_interrupt_trigger,1);
  append(m.total,8);append(m.executed,8);append(m.ticks,8);append(m.interrupts,8);append(m.running,1);
  append(m.aKnown,1);append(m.aOrigin,2);append(m.parityUnknown,1);
  b.insert(b.end(),m.registerKnown.begin(),m.registerKnown.end());
  for(auto origin:m.registerOrigin)append(origin,2);
  return b;
}
}
