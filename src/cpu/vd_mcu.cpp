#include "cpu/vd_mcu.hpp"
#include "cpu/third_party/emu8051/emu8051.h"
#include "storage/sha256.hpp"
#include "cpu/mcu_value_mask.hpp"
#include <array>
#include <cstddef>
#include <cstring>
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
  bool selfLoop=false;   // the last executed instruction was SJMP $ and left every tracked bit unchanged (host-speed fast-forward, see skipIdleLoop)
  uint64_t total=0,interrupts=0;
  uint8_t aKnown=255;
  uint16_t aOrigin=0;
  std::array<uint8_t,256> registerKnown{};
  std::array<uint16_t,256> registerOrigin{};
  bool parityUnknown=false;
  bool carryKnown=true;
  uint8_t dplKnown=255,dphKnown=255,bKnown=255,arithmeticFlagsKnown=0x44;
  std::array<VdByte,128> uartRegisters{};
  bool uartTxPending=false;
  std::array<uint8_t,128> sfrShadow{};
  static bool uartRegister(uint8_t a){
    return a==0xd8 || a==0xaa || a==0xba || a==0x9d || a==0xbb || a==0x9b || a==0x9c || a==0x9a;
  }
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
    if(m.outer->candidateR8051Uart && uartRegister(address)){
      auto v=m.uartRegisters[address-128];auto op=cpu->mCodeMem[cpu->mPC];
      // An immediate read/modify/write preserves invalid bits locally; they
      // cannot affect a branch/address or leak into a foreign register.
      if((op==0x43 || op==0x53) && cpu->mCodeMem[uint16_t(cpu->mPC+1)]==address)return v.value;
      if(op==0xe5 && address!=0x9b && address!=0x9c){
        m.aKnown=v.known;m.aOrigin=uint16_t(0xff00u|address);m.parityUnknown=v.known!=255;return v.value;
      }
      if(v.known==255 && address!=0x9b && address!=0x9c)return v.value;
      throw VdHardwareBoundary("R8051 UART uninitialized/configuration or unmodeled live status consumed");
    }
    if(m.outer->sfrStorage){m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;return m.sfrShadow[address-128];}
    if(m.outer->specialRegister)m.outer->specialRegister(address,false,0);
    std::ostringstream s;s<<m.name<<" nonstandard SFR read unsupported at PC 0x"<<std::hex<<cpu->mPC<<", SFR 0x"<<unsigned(address);
    throw VdHardwareBoundary(s.str());
  }
  static void uart0Write(em8051* cpu,uint8_t address){
    auto& m=self(cpu);m.uartTxPending=!m.outer->sfrStorage;
    if(m.outer->sfrStorage)cpu->mSFR[REG_SCON]|=2;   // exploratory: transmit completes at once (TI)
    // The upstream Timer1 UART path is not the R8051 dedicated generator.
    // Queue the actual guest byte, but stop on its completion consumer until
    // that generator's clock relationship has been established.
    cpu->serial_out_remaining_bits=0;
    if(m.outer->specialRegister)m.outer->specialRegister(address,true,cpu->mSFR[REG_SBUF]);
  }
  static uint8_t uart0ControlRead(em8051* cpu,uint8_t){
    if(self(cpu).uartTxPending)
      throw VdHardwareBoundary("R8051 UART TX completion requires unimplemented dedicated baud clock");
    return cpu->mSFR[REG_SCON];
  }
  static void unknownSfrWrite(em8051* cpu,uint8_t address){
    auto& m=self(cpu);auto value=cpu->mSFR[address-128];
    auto opcode=cpu->mCodeMem[cpu->mPC];
    if(m.outer->unsupportedSfrWrite && (opcode==0x75 || opcode==0xf5) &&
       m.outer->unsupportedSfrWrite(address,{value,opcode==0x75?uint8_t(255):m.aKnown,
         opcode==0x75?uint16_t(0):m.aOrigin}))return;
    if(m.outer->sfrStorage && !(m.outer->candidateR8051Uart && uartRegister(address))){m.sfrShadow[address-128]=value;return;}
    if(m.outer->candidateR8051Uart && uartRegister(address)){
      auto op=cpu->mCodeMem[cpu->mPC];auto& v=m.uartRegisters[address-128];
      if(op==0x75){v={value,255,0};}
      else if((op==0x43 || op==0x53) && cpu->mCodeMem[uint16_t(cpu->mPC+1)]==address){
        auto bits=cpu->mCodeMem[uint16_t(cpu->mPC+2)];
        v.value=value;v.known|=op==0x43?bits:uint8_t(~bits);
      }else if(op>=0x88 && op<=0x8f && cpu->mCodeMem[uint16_t(cpu->mPC+1)]==address){
        // MOV SFR,Rn / MOV SFR,A copy the register's validity unchanged (no new information is created).
        auto index=unsigned((cpu->mSFR[REG_PSW]&0x18)+(op&7));
        v={value,m.registerKnown[index],m.registerOrigin[index]};
      }else if(op==0xf5 && cpu->mCodeMem[uint16_t(cpu->mPC+1)]==address){
        v={value,m.aKnown,m.aOrigin};
      }else throw VdHardwareBoundary("R8051 UART store form has no validity model");
      if(address==0xd8)v.known&=0x80; // only baud-generator selection established
      if(address==0xba || address==0xbb)v.known&=3; // ten-bit reload, reserved bits unknown
      if(address==0x9c)m.uartTxPending=true;
      if(m.outer->specialRegister)m.outer->specialRegister(address,true,value);
      return;
    }
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
      if((cpu->mSFR[REG_IE]&0x7c) && !m.outer->allStandardSources)
        throw VdHardwareBoundary("VD MCU interrupt source other than INT0/Timer0 unsupported");
      return;
    }
    if((cpu->mSFR[REG_IE]&0x80) && (cpu->mSFR[REG_IE]&0x7f))
      throw std::runtime_error("VD MCU interrupt-source enable requires verified MStar interrupt/clock wiring");
  }
  bool codeTableData(em8051* c,uint8_t op,uint8_t operand){
    bool readsPsw=operand==0xd0 && (op==0xe5 || op==0xc0 || op==0x85 ||
      (op>=0xa8 && op<=0xaf) || op==0x25 || op==0x35 || op==0x45 || op==0x55 ||
      op==0x65 || op==0x95 || op==0xb5 || op==0xc5);
    if(readsPsw && arithmeticFlagsKnown!=0x44)
      throw VdHardwareBoundary("MCU unknown arithmetic flags consumed through PSW");
    if(readsPsw && op!=0xc0 && (!carryKnown || parityUnknown))
      throw VdHardwareBoundary("MCU unknown PSW flags copied without a validity model");
    auto byte=[&](uint8_t address)->VdByte{
      if(address<128)return {c->mLowerData[address],registerKnown[address],registerOrigin[address]};
      auto known=address==0x82?dplKnown:address==0x83?dphKnown:address==0xf0?bKnown:address==0xe0?aKnown:255;
      return {c->mSFR[address-128],uint8_t(known),address==0xe0?aOrigin:uint16_t(0)};
    };
    auto acc=[&](VdByte v){aKnown=v.known;aOrigin=v.origin;parityUnknown=v.known!=255;};
    auto local=[](uint8_t address){return address<128 || address==0x82 || address==0x83 || address==0xe0 || address==0xf0;};
    auto put=[&](uint8_t address,VdByte v){
      if(address<128){registerKnown[address]=v.known;registerOrigin[address]=v.origin;}
      else if(address==0x82)dplKnown=v.known;
      else if(address==0x83)dphKnown=v.known;
      else if(address==0xf0)bKnown=v.known;
      else if(address==0xe0)acc(v);
    };
    auto bank=c->mSFR[REG_PSW]&0x18;
    if(op==0x90){dplKnown=dphKnown=255;return false;}
    if(op==0x75 && local(operand)){put(operand,{c->mCodeMem[uint16_t(c->mPC+2)],255,0});return true;}
    if(op==0xf5 && local(operand)){put(operand,{c->mSFR[REG_ACC],aKnown,aOrigin});return true;}
    if(op==0xe5 && local(operand)){acc(byte(operand));return true;}
    if(op>=0xa8 && op<=0xaf && local(operand)){put(uint8_t(bank+(op&7)),byte(operand));return true;}
    if(op>=0x88 && op<=0x8f && local(operand)){put(operand,byte(uint8_t(bank+(op&7))));return true;}
    if(op==0x85 && local(operand) && local(c->mCodeMem[uint16_t(c->mPC+2)])){
      put(c->mCodeMem[uint16_t(c->mPC+2)],byte(operand));return true;
    }
    bool add=(op==0x24 || op==0x25 || (op>=0x28 && op<=0x2f));
    bool addc=(op==0x34 || op==0x35 || (op>=0x38 && op<=0x3f));
    if(add || addc){
      VdByte rhs{operand,255,0};
      if(op==0x25 || op==0x35){if(!local(operand))return false;rhs=byte(operand);}
      else if((op&15)>=8)rhs=byte(uint8_t(bank+(op&7)));
      auto result=mcuAddMasks({c->mSFR[REG_ACC],aKnown},{rhs.value,rhs.known},
        addc && (c->mSFR[REG_PSW]&128),!addc || carryKnown);
      acc({result.result.value,result.result.known,0});
      carryKnown=(result.flagsKnown&128)!=0;arithmeticFlagsKnown=result.flagsKnown&0x44;
      return true;
    }
    if(op==0xa4){
      auto result=mcuMultiplyMasks({c->mSFR[REG_ACC],aKnown},{c->mSFR[REG_B],bKnown});
      acc({result.first.value,result.first.known,0});bKnown=result.second.known;carryKnown=true;
      // OV is high-product != 0. AC is architecturally undefined after MUL.
      arithmeticFlagsKnown=((bKnown==255 || (result.second.value&bKnown))?4:0);
      return true;
    }
    if(op==0x84 && bKnown!=255)throw VdHardwareBoundary("MCU unknown B consumed by DIV");
    if(op==0x83 || op==0x93){
      auto base=op==0x83?uint16_t(c->mPC+1):uint16_t(c->mSFR[REG_DPL]|(c->mSFR[REG_DPH]<<8));
      auto mask=op==0x83?uint16_t(65535):uint16_t(dplKnown|(dphKnown<<8));
      // Enumerate every address represented by the byte masks. Even one
      // unwritten possible byte rejects the lookup; no representative input
      // may hide an uninitialized CODE access. XDATA is never enumerated.
      McuMaskMerge merged;unsigned combinations=0;
      for(unsigned address=0;address<65536;++address)if(((address^base)&mask)==0)
        mcuPossibleBytes({c->mSFR[REG_ACC],aKnown},[&](unsigned offset){
          if(++combinations>65536)throw VdHardwareBoundary("MCU symbolic CODE lookup exceeds bounded address set");
          auto target=uint16_t(address+offset);
          if(!byteKnown(target))throw VdHardwareBoundary("VD MCU MOVC possible address reads unwritten code byte");
          merged.add(c->mCodeMem[target]);
        });
      acc({merged.byte().value,merged.byte().known,0});return true;
    }
    if(op==0xa3 && (dplKnown!=255 || dphKnown!=255))
      throw VdHardwareBoundary("MCU unknown DPTR increment requires a validity model");
    if((op==0xe0 || op==0xf0 || op==0x73) && (dplKnown!=255 || dphKnown!=255))
      throw VdHardwareBoundary("MCU unknown DPTR used as device address or instruction target");
    if((op==0xc0 || op==0xd0) && (operand==0x82 || operand==0x83 || operand==0xf0)){
      if(op==0xc0 && byte(operand).known!=255)
        throw VdHardwareBoundary("MCU unknown SFR stack save requires a validity model");
      if(op==0xd0 && registerKnown[c->mSFR[REG_SP]]==255)put(operand,{0,255,0});
    }
    // No untracked control use of data-SFR masks or arithmetic flags.
    bool bitRead=op==0x10 || op==0x20 || op==0x30 || op==0x72 || op==0x82 || op==0xa0 || op==0xa2 || op==0xb0 || op==0xb2;
    if(bitRead && (operand&0xf8)==0xf0 && !(bKnown&(1u<<(operand&7))))
      throw VdHardwareBoundary("MCU unknown B bit affects instruction/decision");
    if((op==0xe5 || op==0xc0) && operand==0xd0 && arithmeticFlagsKnown!=0x44)
      throw VdHardwareBoundary("MCU unknown arithmetic flags consumed through PSW");
    if(bitRead && (operand==0xd2 || operand==0xd6) && !(arithmeticFlagsKnown&(1u<<(operand&7))))
      throw VdHardwareBoundary("MCU unknown arithmetic flag affects instruction/decision");
    if((op==0x75 || op==0xf5) && operand==0xd0)arithmeticFlagsKnown=0x44;
    bool direct=op==0x05 || op==0x15 || op==0x25 || op==0x35 || op==0x42 || op==0x43 ||
      op==0x45 || op==0x52 || op==0x53 || op==0x55 || op==0x62 || op==0x63 || op==0x65 ||
      op==0x95 || op==0xa6 || op==0xa7 || op==0xb5 || op==0xc5 || op==0xd5;
    if(direct && (operand==0x82 || operand==0x83 || operand==0xf0) && byte(operand).known!=255)
      throw VdHardwareBoundary("MCU unknown data-SFR operation requires a validity model");
    if(op==0x85 && (operand==0x82 || operand==0x83 || operand==0xf0) && byte(operand).known!=255)
      throw VdHardwareBoundary("MCU unknown data-SFR copy to control register");
    if(op==0xd4 && !(arithmeticFlagsKnown&0x40))
      throw VdHardwareBoundary("MCU unknown auxiliary carry consumed by decimal adjust");
    return false;
  }
  static void boundary(em8051* cpu){
    auto& m=self(cpu);auto pc=cpu->mPC;
    if(!m.byteKnown(pc))throw VdHardwareBoundary("VD MCU instruction fetch from unwritten program memory");
    char text[128];
    // The disassembly text is only needed by the instruction trace callback; the instruction length comes from a table built once from the same
    // decoders (decode() formats text with sprintf for every instruction, which dominated the run time). Idle/power-down keep the original path.
    static uint8_t lengths[256];static bool lengthsReady=false;
    if(!lengthsReady){
      em8051 scratch=*cpu;unsigned char code[4]{};scratch.mCodeMem=code;scratch.mCodeMemMaxIdx=3;
      for(unsigned op=0;op<256;++op){code[0]=uint8_t(op);code[1]=code[2]=code[3]=0;lengths[op]=cpu->dec[op](&scratch,0,text);}
      lengthsReady=true;
    }
    uint8_t size;
    if((m.outer->instruction && m.outer->instructionText) || (cpu->mSFR[REG_PCON]&3))size=decode(cpu,pc,text);
    else{size=lengths[cpu->mCodeMem[pc&cpu->mCodeMemMaxIdx]];text[0]=0;}
    if(!size)throw VdHardwareBoundary("VD MCU invalid instruction decode");
    for(unsigned i=0;i<size;++i)if(!m.byteKnown(uint16_t(pc+i)))
      throw VdHardwareBoundary("VD MCU operand outside uploaded program");
    auto opcode=cpu->mCodeMem[pc];
    // Failed validation must not commit metadata for an unexecuted copy,
    // push or address operation. Architectural registers remain untouched.
    // Rn are banked IRAM bytes, not permanently known values. Preserve an
    // invalid MOVX result across register copies and reject unmodeled uses.
    auto operand=cpu->mCodeMem[uint16_t(pc+1)];
    auto sp=cpu->mSFR[REG_SP];
    if(m.outer->symbolicCodeTables && m.codeTableData(cpu,opcode,operand)){
      if(m.outer->instruction)m.outer->instruction(pc,opcode,text);
      return;
    }
    bool consumesCarry=opcode==0x40 || opcode==0x50 || opcode==0x92 || opcode==0xb3 || opcode==0xd4 ||
      opcode==0x72 || opcode==0x82 || opcode==0xa0 || opcode==0xb0 ||
      (opcode>=0x34 && opcode<=0x3f) || (opcode>=0x94 && opcode<=0x9f);
    if(!m.carryKnown && consumesCarry)
      throw VdHardwareBoundary("MCU unknown carry affects instruction/decision");
    if(opcode==0x22 || opcode==0x32){
      if(m.registerKnown[sp]!=255 || m.registerKnown[uint8_t(sp-1)]!=255)
        throw VdHardwareBoundary("MCU unknown IRAM register alias consumed through stack/return address");}
    auto bank=cpu->mSFR[REG_PSW]&0x18;
    auto index=unsigned(bank+(opcode&7));
    bool registerLoad=opcode>=0xe8 && opcode<=0xef;
    bool registerStore=opcode>=0xf8;
    bool registerImmediate=opcode>=0x78 && opcode<=0x7f;
    bool directRegisterLoad=opcode==0xe5 && operand<128;
    if(registerStore){m.registerKnown[index]=m.aKnown;m.registerOrigin[index]=m.aOrigin;}
    if(registerImmediate){m.registerKnown[index]=255;m.registerOrigin[index]=0;}
    if(opcode==0x75 && operand<128){m.registerKnown[operand]=255;m.registerOrigin[operand]=0;}
    if(opcode==0xf5 && operand<128){m.registerKnown[operand]=m.aKnown;m.registerOrigin[operand]=m.aOrigin;}
    bool directImmediateLogical=(opcode==0x43 || opcode==0x53 || opcode==0x63) && operand<128;
    if(directImmediateLogical){
      auto bits=cpu->mCodeMem[uint16_t(pc+2)];
      if(opcode!=0x63)m.registerKnown[operand]|=opcode==0x53?uint8_t(~bits):bits;
    }
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
      opcode==0xb5 || opcode==0xc5 || opcode==0xd5 ||
      (opcode>=0xa8 && opcode<=0xaf);
    if(directSource && operand<128 && m.registerKnown[operand]!=255)
      throw VdHardwareBoundary("MCU unknown register bits consumed through direct IRAM alias");
    bool bitRead=opcode==0x10 || opcode==0x20 || opcode==0x30 || opcode==0x72 ||
      opcode==0x82 || opcode==0xa0 || opcode==0xa2 || opcode==0xb0 || opcode==0xb2;
    if(operand<128){
      auto address=unsigned(0x20+(operand>>3));auto mask=uint8_t(1u<<(operand&7));
      if(bitRead && !(m.registerKnown[address]&mask))
        throw VdHardwareBoundary("MCU unknown IRAM bit affects instruction/decision");
      if(opcode==0xc2 || opcode==0xd2 || opcode==0x92){
        m.registerKnown[address]|=mask;
        if(m.registerKnown[address]==255)m.registerOrigin[address]=0;
      }
    }
    if(bitRead && operand==0xd0 && m.parityUnknown)
      throw VdHardwareBoundary("VD MCU unknown parity consumed through PSW");
    if(bitRead && operand==0xd7 && !m.carryKnown)
      throw VdHardwareBoundary("MCU unknown carry consumed through PSW bit");
    if(opcode==0xc0){
      uint8_t known=255;uint16_t origin=0;
      if(operand<128){known=m.registerKnown[operand];origin=m.registerOrigin[operand];}
      else if(operand==0xe0){known=m.aKnown;origin=m.aOrigin;}
      else if(operand==0xd0){
        known=uint8_t((m.parityUnknown?0xfe:255)&(m.carryKnown?255:0x7f));
        origin=m.parityUnknown && m.carryKnown?m.aOrigin:0;
      }
      auto address=uint8_t(sp+1);
      m.registerKnown[address]=known;m.registerOrigin[address]=origin;
    }
    if(opcode==0xd0){
      auto known=m.registerKnown[sp];auto origin=m.registerOrigin[sp];
      if(operand<128){m.registerKnown[operand]=known;m.registerOrigin[operand]=origin;}
      else if(operand==0xe0){m.aKnown=known;m.aOrigin=origin;m.parityUnknown=known!=255;}
      else if((operand==0xd0 && (known&0x7e)!=0x7e) || (operand!=0xd0 && known!=255))
        throw VdHardwareBoundary("MCU unknown stack byte affects SFR control/address");
      if(operand==0xd0)m.carryKnown=(known&0x80)!=0;
    }
    if(opcode==0x12 || (opcode&0x1f)==0x11){
      for(unsigned i=1;i<=2;++i){auto address=uint8_t(sp+i);
        m.registerKnown[address]=255;m.registerOrigin[address]=0;}
    }
    if(opcode>=0xa8 && opcode<=0xaf){m.registerKnown[index]=255;m.registerOrigin[index]=0;}
    if((opcode>=0x88 && opcode<=0x8f) && operand<128){m.registerKnown[operand]=255;m.registerOrigin[operand]=0;}
    if(opcode==0x85){auto destination=cpu->mCodeMem[uint16_t(pc+2)];
      if(destination<128){m.registerKnown[destination]=255;m.registerOrigin[destination]=0;}}
    bool indirect=(opcode&0x0e)==6 || opcode==0xe2 || opcode==0xe3 || opcode==0xf2 || opcode==0xf3;
    if(indirect && m.registerKnown[bank+(opcode&1)]!=255)
      throw VdHardwareBoundary("MCU unknown register used as indirect address");
    std::optional<unsigned> indirectRegisterLoad;
    if((opcode&0x0e)==6){
      auto address=cpu->mLowerData[bank+(opcode&1)];
      bool writes=opcode==0x76 || opcode==0x77 || opcode==0xa6 || opcode==0xa7 || opcode==0xf6 || opcode==0xf7;
      {
        if(opcode==0xe6 || opcode==0xe7)indirectRegisterLoad=address;
        else if(!writes && m.registerKnown[address]!=255)
          throw VdHardwareBoundary("MCU unknown register bits consumed through indirect IRAM alias");
        else if(writes){
          if(opcode==0xf6 || opcode==0xf7){m.registerKnown[address]=m.aKnown;m.registerOrigin[address]=m.aOrigin;}
          else {m.registerKnown[address]=255;m.registerOrigin[address]=0;}
        }
      }
    }
    // Carry and ACC bits are separate architectural values. Permuting an
    // invalid byte does not consume it, but must invalidate its former
    // same-register RMW identity. In particular original AVD 1EDA rotates
    // a masked configuration byte before selecting a code-table entry.
    bool partialPermutation=false;
    if((opcode==0x13 || opcode==0x33) && (m.aKnown!=255 || !m.carryKnown)){
      auto before=m.aKnown;auto carry=m.carryKnown;
      m.aKnown=opcode==0x13?uint8_t((before>>1)|(carry?0x80:0)):
        uint8_t((before<<1)|(carry?1:0));
      m.carryKnown=(before&(opcode==0x13?1:0x80))!=0;
      m.aOrigin=0;m.parityUnknown=m.aKnown!=255;partialPermutation=true;
    }else if(m.aKnown!=255 && (opcode==0x03 || opcode==0x23 || opcode==0xc4)){
      auto before=m.aKnown;
      m.aKnown=opcode==0x03?uint8_t((before>>1)|(before<<7)):
        opcode==0x23?uint8_t((before<<1)|(before>>7)):uint8_t((before<<4)|(before>>4));
      m.aOrigin=0;m.parityUnknown=m.aKnown!=255;partialPermutation=true;
    }
    if(opcode==0xc3 || opcode==0xd3 || ((opcode==0xc2 || opcode==0xd2) && operand==0xd7) ||
       opcode==0xa2 || (opcode>=0x24 && opcode<=0x2f) || (opcode>=0xb4 && opcode<=0xbf) ||
       (opcode>=0x34 && opcode<=0x3f) || (opcode>=0x94 && opcode<=0x9f) || opcode==0xa4 || opcode==0x84 ||
       ((opcode==0x75 || opcode==0xf5 || (opcode>=0x88 && opcode<=0x8f)) && operand==0xd0) ||
       (opcode==0x85 && cpu->mCodeMem[uint16_t(pc+2)]==0xd0))m.carryKnown=true;
    // INC A / DEC A on a partially known byte: the result bit is known exactly
    // when every assignment of the unknown bits agrees on it. Carry is unaffected;
    // parity follows result validity. This creates no information about the input.
    bool incDecA=false;
    if((opcode==0x04 || opcode==0x14) && m.aKnown!=255){
      uint8_t knownMask=m.aKnown,value=cpu->mSFR[REG_ACC]&knownMask,agreeOnes=0xff,agreeZeros=0xff;
      for(unsigned unknown=0;unknown<256;++unknown){
        if(unknown&knownMask)continue;
        uint8_t a=uint8_t(value|unknown),r=uint8_t(opcode==0x04?a+1:a-1);
        agreeOnes&=r;agreeZeros&=uint8_t(~r);
      }
      m.aKnown=uint8_t(agreeOnes|agreeZeros);m.aOrigin=0;m.parityUnknown=m.aKnown!=255;incDecA=true;
    }
    // CPL A complements each bit independently: the known mask is unchanged and
    // unknown bits stay unknown (only parity validity follows).
    bool cplA=false;
    if(opcode==0xf4 && m.aKnown!=255){m.aOrigin=0;m.parityUnknown=true;cplA=true;}
    if(m.aKnown!=255){
      bool independent=incDecA || cplA || (opcode&0x1f)==0x01 || opcode==0 || opcode==0x22 || opcode==0x32 || opcode==0x02 ||
        opcode==0x12 || (opcode&0x1f)==0x11 || opcode==0x80 || opcode==0x90 ||
        opcode==0xa3 || opcode==0xf0 || opcode==0xc0 || opcode==0xd0 || opcode==0xc3 || opcode==0xd3 ||
        opcode==0x40 || opcode==0x50 || partialPermutation || directImmediateLogical;
      if(opcode==0x54 || opcode==0x44 || opcode==0x64){
        auto bits=cpu->mCodeMem[uint16_t(pc+1)];
        if(opcode!=0x64)m.aKnown|=opcode==0x54?uint8_t(~bits):bits;
        m.parityUnknown=m.aKnown!=255;independent=true;
      }
      // AVD's original 0ECB ORL A,R5 merges a known low-six-bit field
      // into an invalid same-origin register read. A fully known Rn has
      // exactly the same bit-validity effect as an immediate operand.
      // The usesRn guard above still rejects partially unknown operands.
      if((opcode>=0x48 && opcode<=0x4f) || (opcode>=0x58 && opcode<=0x5f) ||
         (opcode>=0x68 && opcode<=0x6f)){
        auto bits=cpu->mLowerData[index];
        if(opcode<0x60)m.aKnown|=opcode<0x50?bits:uint8_t(~bits);
        m.parityUnknown=m.aKnown!=255;independent=true;
      }
      bool overwrite=opcode==0xe4 || opcode==0x74 || opcode==0xe6 || opcode==0xe7 ||
        registerLoad || opcode==0xe0 ||
        (opcode==0xe5 && cpu->mCodeMem[uint16_t(pc+1)]!=0xd0 && cpu->mCodeMem[uint16_t(pc+1)]!=0xe0);
      if(overwrite){m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;independent=true;}
      if(registerStore || registerImmediate || (opcode==0x75 && operand!=0xe0))independent=true;
      // Original AVD 076E copies the known register index from direct IRAM
      // into R3 while ACC still carries the masked value being stored.
      // The directSource guard above verifies the source independently.
      if(opcode>=0xa8 && opcode<=0xaf && operand<128)independent=true;
      if((opcode==0xc2 || opcode==0xd2) && (operand<0xe0 || operand>0xe7))independent=true;
      if(opcode==0x20 || opcode==0x30){
        auto bit=cpu->mCodeMem[uint16_t(pc+1)];
        independent=(bit<128) || (bit>=0xe0 && bit<=0xe7 && (m.aKnown&(1u<<(bit-0xe0))));
      }
      if(!independent)throw VdHardwareBoundary(std::string(m.name)+" unknown XDATA bits affect instruction/decision/address");
    }
    if(registerLoad || directRegisterLoad || indirectRegisterLoad){
      auto source=indirectRegisterLoad?*indirectRegisterLoad:registerLoad?index:unsigned(operand);
      m.aKnown=m.registerKnown[source];m.aOrigin=m.registerOrigin[source];
      m.parityUnknown=m.aKnown!=255;
    }
    if(m.parityUnknown && opcode==0xe5 && cpu->mCodeMem[uint16_t(pc+1)]==0xd0)
      throw VdHardwareBoundary("VD MCU unknown parity consumed through PSW");
    if(!m.carryKnown && opcode==0xe5 && cpu->mCodeMem[uint16_t(pc+1)]==0xd0)
      throw VdHardwareBoundary("MCU unknown carry consumed through PSW byte");
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
  if(code.size()!=65536 || (!unverifiedPmImage && sha256(code)!=
     "5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c"))
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
  if(candidateR8051Uart){cpu.sfrwrite[REG_SBUF]=Impl::uart0Write;cpu.sfrread[REG_SCON]=Impl::uart0ControlRead;}
  cpu.instruction_boundary=Impl::boundary;
  m.executed=m.ticks=0;m.deferredKeyCount=0;
  if(!m.byteKnown){m.registerKnown.fill(255);m.registerOrigin.fill(0);}
  m.byteKnown=std::move(byteKnown);m.running=true;m.aKnown=255;m.aOrigin=0;m.parityUnknown=false;m.carryKnown=true;
  m.dplKnown=m.dphKnown=m.bKnown=255;m.arithmeticFlagsKnown=0x44;
  m.uartRegisters.fill({});m.uartTxPending=false;
}
void VdMcuCpu::step(){
  auto& m=*impl_;
  if(!m.running)throw VdHardwareBoundary("VD MCU dispatch while stopped");
  auto known=m.registerKnown;auto origins=m.registerOrigin;
  auto accumulatorKnown=m.aKnown;auto accumulatorOrigin=m.aOrigin;auto parity=m.parityUnknown;auto carry=m.carryKnown;
  auto dpl=m.dplKnown,dph=m.dphKnown,b=m.bKnown,flags=m.arithmeticFlagsKnown;
  try{
    auto previousInterrupt=m.context.cpu.mInterruptActive;
    auto& c=m.context.cpu;auto pc0=c.mPC;
    if(tick(&c)){
      ++m.executed;++m.total;
      // A self-jump that changed no architectural or provenance state proves every further iteration identical.
      m.selfLoop=c.mPC==pc0 && c.mCodeMem[pc0]==0x80 && c.mCodeMem[uint16_t(pc0+1)]==0xfe &&
        known==m.registerKnown && origins==m.registerOrigin && accumulatorKnown==m.aKnown && accumulatorOrigin==m.aOrigin &&
        parity==m.parityUnknown && carry==m.carryKnown && dpl==m.dplKnown && dph==m.dphKnown && b==m.bKnown && flags==m.arithmeticFlagsKnown;
    }
    ++m.ticks;
    if(m.context.cpu.mInterruptActive & ~previousInterrupt){
      auto sp=m.context.cpu.mSFR[REG_SP];
      for(auto address:{sp,uint8_t(sp-1)}){m.registerKnown[address]=255;m.registerOrigin[address]=0;}
    }
  }
  catch(...){
    m.registerKnown=known;m.registerOrigin=origins;
    m.aKnown=accumulatorKnown;m.aOrigin=accumulatorOrigin;m.parityUnknown=parity;m.carryKnown=carry;
    m.dplKnown=dpl;m.dphKnown=dph;m.bKnown=b;m.arithmeticFlagsKnown=flags;
    m.running=false;throw;
  }
}
// Exact fast-forward of an idle `SJMP $`: it reads and writes nothing, so with both timers stopped and no interrupt source
// enabled the only effect of each iteration is one clock tick and one executed instruction.
uint64_t VdMcuCpu::skipIdleLoop(uint64_t maxTicks,uint64_t maxInstructions){
  auto& m=*impl_;auto& c=m.context.cpu;
  if(!m.running || !m.selfLoop || (instruction && instructionText) || (c.mSFR[REG_PCON]&3) || c.mTickDelay!=1)return 0;
  if(c.mCodeMem[c.mPC]!=0x80 || c.mCodeMem[uint16_t(c.mPC+1)]!=0xfe)return 0;
  auto tmod=c.mSFR[REG_TMOD];
  // Timer 1 (and timer 0 mode 3 with its second counter) would feed the serial port / TF1: not folded.
  if((c.mSFR[REG_TCON]&TCONMASK_TR1) || (tmod&(TMODMASK_M0_0|TMODMASK_M1_0))==(TMODMASK_M0_0|TMODMASK_M1_0))return 0;
  if((c.mSFR[REG_IE]&0x80) && (c.mSFR[REG_IE]&0x3f))return 0;
  uint64_t n=std::min(maxTicks,maxInstructions);   // SJMP takes one tick here: every tick is one executed instruction
  if(!n)return 0;
  // Timer 0 counts one per tick exactly as timer_tick does, so its evolution over n ticks is closed-form.
  if(!(tmod&TMODMASK_GATE_0) && !(tmod&TMODMASK_CT_0) && (c.mSFR[REG_TCON]&TCONMASK_TR0)){
    uint8_t& tl=c.mSFR[REG_TL0];uint8_t& th=c.mSFR[REG_TH0];
    bool overflow=false;
    switch(tmod&(TMODMASK_M0_0|TMODMASK_M1_0)){
    case 0:{   // 13-bit: TH0 and the low five bits of TL0
      uint64_t v=(uint64_t(th)<<5|(tl&0x1f))+n;overflow=v>=8192;v&=8191;
      th=uint8_t(v>>5);tl=uint8_t((tl&~0x1f)|(v&0x1f));break;}
    case TMODMASK_M0_0:{   // 16-bit
      uint64_t v=(uint64_t(th)<<8|tl)+n;overflow=v>=65536;v&=65535;
      th=uint8_t(v>>8);tl=uint8_t(v);break;}
    default:{   // 8-bit auto-reload from TH0
      uint64_t untilOverflow=256-tl;
      if(n<untilOverflow)tl=uint8_t(tl+n);
      else{overflow=true;tl=uint8_t(th+(n-untilOverflow)%(256-th));}
      break;}
    }
    if(overflow)c.mSFR[REG_TCON]|=TCONMASK_TF0;
  }
  m.ticks+=n;m.executed+=n;m.total+=n;
  return n;
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
// Diagnostic read-only view of the tracked low IRAM (value, known mask, origin XDATA address of an invalid byte); used by tools/avd_signal_probe.
std::tuple<uint8_t,uint8_t,uint16_t> VdMcuCpu::iramInfo(unsigned address)const{
  address&=127;auto& m=*impl_;return {m.context.cpu.mLowerData[address],m.registerKnown[address],m.registerOrigin[address]};
}
uint8_t VdMcuCpu::stack()const{return impl_->context.cpu.mSFR[REG_SP];}
uint8_t VdMcuCpu::accumulator()const{return impl_->context.cpu.mSFR[REG_ACC];}
uint8_t VdMcuCpu::status()const{return impl_->context.cpu.mSFR[REG_PSW];}
uint8_t VdMcuCpu::port2()const{return impl_->context.cpu.mSFR[REG_P2];}
uint16_t VdMcuCpu::dataPointer()const{
  return uint16_t(impl_->context.cpu.mSFR[REG_DPL] | (uint16_t(impl_->context.cpu.mSFR[REG_DPH])<<8));
}
uint8_t VdMcuCpu::accumulatorKnown()const{return impl_->aKnown;}
uint16_t VdMcuCpu::accumulatorOrigin()const{return impl_->aOrigin;}
bool VdMcuCpu::bound()const{return impl_->context.cpu.mCodeMem!=nullptr;}
void VdMcuCpu::rebindProgram(VdMcuProgram& program){
  impl_->name="VD MCU";
  resetProgramStorage(program.codeStorage(),[&program](uint16_t a){return program.byteKnown(a);},1);
}
void VdMcuCpu::rebindProgram(AvdMcuProgram& program){
  impl_->name="AVD MCU";
  resetProgramStorage(program.codeStorage(),[&program](uint16_t a){return program.byteKnown(a);},1);
}
void VdMcuCpu::rebindPm(std::span<const uint8_t> code,std::function<bool(uint16_t)> sourceValid){
  if(code.size()!=65536)throw std::runtime_error("PM51 snapshot rebind: wrong code view size");
  impl_->name="PM51 candidate";
  resetProgramStorage(code,sourceValid?std::move(sourceValid):[](uint16_t){return true;},code.size());
}
void VdMcuCpu::snapshot(snap::Ar& a,const std::function<void(VdMcuCpu&)>& bind){
  auto& m=*impl_;auto& c=m.context.cpu;
  a.tag("VdMcuCpu");
  snap::io(a,deferSfr8eWrite);snap::io(a,deferResetKeyWrites);snap::io(a,standardInterrupts);snap::io(a,candidateR8051Uart);snap::io(a,symbolicCodeTables);
  bool isBound=c.mCodeMem!=nullptr;
  snap::io(a,isBound);
  uint8_t nameId=!std::strcmp(m.name,"AVD MCU")?1:!std::strcmp(m.name,"PM51 candidate")?2:0;
  snap::io(a,nameId);
  if(a.loading()){
    if(nameId>2)throw snap::SnapshotError("MCU name id invalid");
    if(isBound){if(!bind)throw snap::SnapshotError("bound MCU snapshot without a rebind function");bind(*this);}
    m.name=nameId==1?"AVD MCU":nameId==2?"PM51 candidate":"VD MCU";
  }
  for(auto& v:c.mLowerData)snap::io(a,v);
  snap::io(a,m.upper);
  for(auto& v:c.mSFR)snap::io(a,v);
  snap::io(a,c.mPC);snap::io(a,c.mTickDelay);snap::io(a,c.mInterruptActive);
  snap::io(a,c.int_a[0]);snap::io(a,c.int_a[1]);snap::io(a,c.int_psw[0]);snap::io(a,c.int_psw[1]);snap::io(a,c.int_sp[0]);snap::io(a,c.int_sp[1]);
  for(auto& v:c.serial_out)snap::io(a,v);
  snap::io(a,c.serial_out_idx);snap::io(a,c.serial_out_remaining_bits);snap::io(a,c.serial_interrupt_trigger);
  snap::io(a,m.executed);snap::io(a,m.ticks);snap::io(a,m.deferredKeyCount);snap::io(a,m.running);snap::io(a,m.total);snap::io(a,m.interrupts);
  snap::io(a,m.aKnown);snap::io(a,m.aOrigin);snap::io(a,m.registerKnown);snap::io(a,m.registerOrigin);snap::io(a,m.parityUnknown);snap::io(a,m.carryKnown);
  snap::io(a,m.dplKnown);snap::io(a,m.dphKnown);snap::io(a,m.bKnown);snap::io(a,m.arithmeticFlagsKnown);
  for(auto& v:m.uartRegisters){snap::io(a,v.value);snap::io(a,v.known);snap::io(a,v.origin);}
  snap::io(a,m.uartTxPending);
}
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
  if(candidateR8051Uart){
    append(1,1);append(m.uartTxPending,1);
    for(const auto& v:m.uartRegisters){append(v.value,1);append(v.known,1);append(v.origin,2);}
  }
  // Existing all-known-carry fingerprints retain their exact byte layout.
  // An unknown carry is additional state and cannot hash as a known flag.
  if(!m.carryKnown){append(0x434b,2);append(0,1);}
  if(symbolicCodeTables){append(0x4354,2);append(m.dplKnown,1);append(m.dphKnown,1);append(m.bKnown,1);append(m.arithmeticFlagsKnown,1);}
  return b;
}
}
