#include "cpu/openrisc.hpp"
#include <bit>
#include <limits>
#include <sstream>
namespace manta {
std::vector<uint8_t> OpenRisc::stateBytes()const{
  std::vector<uint8_t> bytes;auto word=[&](uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(v>>(i*8)));};
  word(pc_);word(ppc_);word(sr_);word(branch_.has_value());word(branch_.value_or(0));word(running_);
  word(uint32_t(instructions_));word(uint32_t(instructions_>>32));word(uint32_t(exceptions_));word(uint32_t(exceptions_>>32));
  for(auto r:r_){word(r.value);word(r.known);word(r.origin.has_value());word(r.origin.value_or(0));}
  word(unsigned(spr_.size()));for(auto [key,value]:spr_){word(key);word(value);}return bytes;
}
uint32_t OpenRisc::require(OrWord v){
  if(v.known!=0xffffffffu)throw std::runtime_error("OpenRISC consumes unknown register/peripheral bits");
  return v.value;
}
OrWord OpenRisc::logic(OrWord a,OrWord b,unsigned op){
  OrWord v;
  if(op==3){v.value=a.value&b.value;v.known=(a.known&b.known)|(a.known&~a.value)|(b.known&~b.value);}
  else if(op==4){v.value=a.value|b.value;v.known=(a.known&b.known)|(a.known&a.value)|(b.known&b.value);}
  else {v.value=a.value^b.value;v.known=a.known&b.known;}
  if(v.known!=0xffffffffu){
    if(a.known!=0xffffffffu && b.known!=0xffffffffu && a.origin!=b.origin)
      throw std::runtime_error("OpenRISC combines unknown bits from different origins");
    v.origin=a.known!=0xffffffffu?a.origin:b.origin;
  }
  return v;
}
void OpenRisc::reset(uint32_t pc,uint32_t sr,std::optional<uint32_t> configuredResetR0){
  if(pc&3)throw std::runtime_error("OpenRISC reset PC misaligned");
  r_={};spr_.clear();branch_.reset();pc_=pc;ppc_=pc;sr_=sr|0x8000;running_=true;
  // Platform reset configuration is distinct from an instruction result. The
  // original TSP caller leaves this unset: its uninitialized GPRs stay invalid.
  if(configuredResetR0)r_[0]={*configuredResetR0,0xffffffffu,{}};
}
void OpenRisc::exception(unsigned vector,uint32_t address){
  spr_[32]=branch_?pc_-4:pc_;spr_[48]=address;spr_[64]=sr_|(branch_?0x2000:0);
  sr_=(sr_|1)&~(2u|4u|0x20u|0x40u);branch_.reset();pc_=(sr_&0x4000?0xf0000000u:0)+(vector<<8);++exceptions_;
}
uint32_t OpenRisc::arithmetic(uint32_t a,uint32_t b,bool sub,bool carry){
  uint64_t wide=uint64_t(a)+b+(carry?((sr_>>10)&1):0);
  uint32_t result=sub?a-b:uint32_t(wide);
  bool cy=sub?a<b:(wide>>32)!=0;
  bool ov=sub?(((a^b)&(a^result))>>31):((~(a^b)&(a^result))>>31);
  sr_=(sr_&~0xc00u)|(cy?0x400:0)|(ov?0x800:0);
  if((sr_&0x1000)&&(cy||ov))throw std::runtime_error("OpenRISC enabled arithmetic overflow exception unsupported");
  return result;
}
void OpenRisc::step(){
  if(!running_)return;
  if(!read || !write)throw std::runtime_error("OpenRISC bus not connected");
  if(pc_&3){exception(6,pc_);return;}
  uint32_t w=require(read(pc_,4,true));if(trace)trace(pc_,w);
  auto op=w>>26,d=(w>>21)&31,a=(w>>16)&31,b=(w>>11)&31;
  int32_t imm=int16_t(w);uint32_t storeImm=uint32_t(int32_t(int16_t(((w>>10)&0xf800)|(w&0x7ff))));
  auto pending=branch_;bool wasDelay=pending.has_value();branch_.reset();uint32_t next=pc_+4;
  auto full=[](uint32_t v){return OrWord{v,0xffffffffu,{}};};
  auto ra=[&](){return require(r_[a]);};auto rb=[&](){return require(r_[b]);};
  auto branch=[&](uint32_t target){
    if(wasDelay)throw std::runtime_error("OpenRISC branch in delay slot unsupported/undefined");
    branch_=target;
  };
  auto compare=[&](uint32_t lhs,uint32_t rhs){
    bool f=false;
    switch(d){case 0:f=lhs==rhs;break;case 1:f=lhs!=rhs;break;case 2:f=lhs>rhs;break;case 3:f=lhs>=rhs;break;
    case 4:f=lhs<rhs;break;case 5:f=lhs<=rhs;break;case 10:f=int32_t(lhs)>int32_t(rhs);break;
    case 11:f=int32_t(lhs)>=int32_t(rhs);break;case 12:f=int32_t(lhs)<int32_t(rhs);break;case 13:f=int32_t(lhs)<=int32_t(rhs);break;
    default:throw std::runtime_error("OpenRISC comparison encoding unsupported");}
    sr_=(sr_&~0x200u)|(f?0x200:0);
  };
  switch(op){
  case 0:case 1:case 3:case 4:{
    int32_t disp=int32_t(w<<6)>>6;
    if(op==1)r_[9]=full(pc_+8);
    bool take=op==0||op==1||(op==3?!(sr_&0x200):bool(sr_&0x200));
    branch(take?pc_+uint32_t(disp*int64_t(4)):pc_+8);break;
  }
  case 5:if((w&0xff000000)!=0x15000000)throw std::runtime_error("OpenRISC invalid NOP encoding");break;
  case 6:if(w&0x10000)throw std::runtime_error("OpenRISC MACRC unsupported");r_[d]=full((w&65535)<<16);break;
  case 8:branch_=pending;exception((w&0x1000000)?14:12,0);++instructions_;return;
  case 9:if(!spr_.contains(32)||!spr_.contains(64))throw std::runtime_error("OpenRISC RFE without exception state");
    next=spr_[32];sr_=spr_[64]|0x8000;branch_.reset();pending.reset();break;
  case 0x11:case 0x12:{auto target=rb();if(op==0x12)r_[9]=full(pc_+8);branch(target);break;}
  case 0x21:case 0x22:case 0x23:case 0x24:case 0x25:case 0x26:{
    uint32_t address=ra()+uint32_t(imm);unsigned width=op<=0x22?4:op<=0x24?1:2;
    if(address&(width-1)){branch_=pending;exception(6,address);++instructions_;return;}
    auto v=read(address,width,false);uint32_t mask=width==4?0xffffffffu:(1u<<(width*8))-1;
    v.value&=mask;v.known|=~mask;
    if(op==0x24||op==0x26){require(v);v=full(width==1?uint32_t(int32_t(int8_t(v.value))):uint32_t(int32_t(int16_t(v.value))));}
    r_[d]=v;break;
  }
  case 0x27:r_[d]=full(arithmetic(ra(),uint32_t(imm),false,false));break;
  case 0x28:r_[d]=full(arithmetic(ra(),uint32_t(imm),false,true));break;
  case 0x29:r_[d]=logic(r_[a],full(w&65535),3);break;
  case 0x2a:r_[d]=logic(r_[a],full(w&65535),4);break;
  case 0x2b:r_[d]=logic(r_[a],full(uint32_t(imm)),5);break;
  case 0x2c:{int64_t product=int64_t(int32_t(ra()))*imm;uint32_t value=uint32_t(product);
    bool ov=product!=int64_t(int32_t(value));sr_=(sr_&~0x800u)|(ov?0x800:0);
    if(ov&&(sr_&0x1000))throw std::runtime_error("OpenRISC multiplication overflow exception unsupported");
    r_[d]=full(value);break;}
  case 0x2d:{unsigned id=ra()|(w&65535);if(id==17)r_[d]=full(sr_);else if(id==16)r_[d]=full(pc_);
    else if(id==18)r_[d]=full(ppc_);else if(spr_.contains(id))r_[d]=full(spr_[id]);
    else throw std::runtime_error("OpenRISC unknown/unimplemented SPR read");
    break;}
  case 0x2e:{uint32_t x=ra();unsigned shift=w&31;
    switch((w>>6)&3){case 0:x<<=shift;break;case 1:x>>=shift;break;case 2:x=uint32_t(int32_t(x)>>shift);break;case 3:x=std::rotr(x,int(shift));break;}
    r_[d]=full(x);break;}
  case 0x2f:compare(ra(),uint32_t(imm));break;
  case 0x30:{unsigned id=ra()|(((w>>10)&0xf800)|(w&0x7ff));uint32_t value=rb();
    if(id==17){if(value&(0x20|0x40))throw std::runtime_error("OpenRISC MMU enable unsupported");sr_=value|0x8000;}
    else if(id==32||id==48||id==64)spr_[id]=value;
    else throw std::runtime_error("OpenRISC unknown/unimplemented SPR write");
    break;}
  case 0x35:case 0x36:case 0x37:{unsigned width=op==0x35?4:op==0x36?1:2;uint32_t address=ra()+storeImm;
    if(address&(width-1)){branch_=pending;exception(6,address);++instructions_;return;}
    write(address,width,r_[b]);break;}
  case 0x38:{unsigned fn=w&0x7ff;uint32_t value=0;
    if(fn>=3&&fn<=5){r_[d]=logic(r_[a],r_[b],fn);break;}
    switch(fn){case 0:value=arithmetic(ra(),rb(),false,false);break;case 1:value=arithmetic(ra(),rb(),false,true);break;
    case 2:value=arithmetic(ra(),rb(),true,false);break;case 8:value=ra()<<(rb()&31);break;
    case 0x48:value=ra()>>(rb()&31);break;case 0x88:value=uint32_t(int32_t(ra())>>(rb()&31));break;
    case 0xc8:value=std::rotr(ra(),int(rb()&31));break;case 0xe:value=sr_&0x200?ra():rb();break;
    case 0xf:value=ra()?std::countr_zero(ra())+1:0;break;case 0x10f:value=ra()?32-std::countl_zero(ra()):0;break;
    case 0x306:{int64_t product=int64_t(int32_t(ra()))*int32_t(rb());value=uint32_t(product);
      bool ov=product!=int64_t(int32_t(value));sr_=(sr_&~0x800u)|(ov?0x800:0);
      if(ov&&(sr_&0x1000))throw std::runtime_error("OpenRISC multiplication overflow exception unsupported");
      break;}
    default:throw std::runtime_error("OpenRISC ALU function unsupported");}r_[d]=full(value);break;}
  case 0x39:compare(ra(),rb());break;
  default:{std::ostringstream s;s<<"OpenRISC unsupported instruction 0x"<<std::hex<<w<<" at PC 0x"<<pc_;throw std::runtime_error(s.str());}
  }
  ppc_=pc_;pc_=pending.value_or(next);++instructions_;
}
}
