// Isolated original resource lookup and CP startup investigation. Never used
// as a substitute for the main machine's upload, reset release or mailbox.
#include "cpu/engine.hpp"
#include "cpu/openrisc.hpp"
#include "storage/flash.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
static void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static std::vector<uint8_t> load(const char* path){
  std::ifstream f(path,std::ios::binary);need(bool(f),"cannot read private original input");
  return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char** argv){try{
  need(argc>=5 && argc<=7,"supply stock.bin runtime.bin resource61.bin --entry-100-hypothesis [--assume-reset-r0-zero] [--defer-unmodeled-startup-writes]");
  need(std::string(argv[4])=="--entry-100-hypothesis","CP reset entry must remain an explicit unverified hypothesis");
  bool defer=false,r0zero=false;
  for(int i=5;i<argc;++i){
    auto flag=std::string(argv[i]);
    if(flag=="--defer-unmodeled-startup-writes")defer=true;
    else if(flag=="--assume-reset-r0-zero")r0zero=true;
    else throw std::runtime_error("unsupported probe option");
  }
  need(!defer || r0zero,"pending-effect probe requires separately named reset-r0 hypothesis");
  auto flash=manta::Flash::loadOriginal(argv[1]);auto runtime=load(argv[2]);auto program=load(argv[3]);
  need(runtime.size()==3181408,"original runtime extent mismatch");
  need(manta::sha256(std::span(runtime).subspan(0x3800))=="149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236","original runtime hash mismatch");
  need(std::equal(runtime.begin(),runtime.begin()+0x3800,flash.stock().begin()+0x20080),"runtime resource prefix differs from flash");
  need(program.size()==1253416 && manta::sha256(program)=="bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1","original resource61 hash mismatch");
  // Execute the complete original resource lookup function, not a host parser.
  manta::Engine m;m.map(0x003b0000,0x00310000);m.map(0x007f0000,0x10000);
  m.write(0x003b0180,runtime.data(),runtime.size());
  uint32_t request[4]={0x61,0,0,0};m.write(0x007f0100,request,sizeof(request));
  m.reg(UC_MIPS_REG_4,0x807f0100);m.reg(UC_MIPS_REG_SP,0x807fff00);m.reg(UC_MIPS_REG_RA,0x803b0000);
  uc_hook end;manta::check(uc_hook_add(m.handle,&end,UC_HOOK_CODE,reinterpret_cast<void*>(
    +[](uc_engine* cpu,uint64_t,uint32_t,void*){uc_emu_stop(cpu);}),nullptr,0x803b0000,0x803b0000));
  manta::check(m.run(0x804f0af4,10000));m.read(0x007f0100,request,sizeof(request));
  need(m.reg(UC_MIPS_REG_PC)==0x803b0000 && m.reg(UC_MIPS_REG_2)==1,"original MIPS resource lookup did not return successfully");
  need(request[1]==0x242910 && request[2]==0x769b8 && request[3]==4,"original guest did not select expected CP resource");
  // Exercise original MHAL_MBX_Fire against an explicit empty software bus
  // fixture. These bytes are NOT asserted to be real hardware reset state.
  m.map(0x006f6000,0x1000);uint32_t riuBase=0xbf200000;m.write(0x006f6964,&riuBase,4);
  std::array<uint8_t,0x1000> mailbox{};
  auto read=+[](uc_engine*,uint64_t a,unsigned n,void* p)->uint64_t{
    auto& b=*static_cast<std::array<uint8_t,0x1000>*>(p);uint64_t v=0;
    for(unsigned i=0;i<n;++i)v|=uint64_t(b.at(a+i))<<(i*8);return v;
  };
  auto write=+[](uc_engine*,uint64_t a,unsigned n,uint64_t v,void* p){
    auto& b=*static_cast<std::array<uint8_t,0x1000>*>(p);
    for(unsigned i=0;i<n;++i)b.at(a+i)=uint8_t(v>>(i*8));
  };
  manta::check(uc_mmio_map(m.handle,0x1f206000,0x1000,read,&mailbox,write,&mailbox));
  auto fire=[&](uint8_t index){
    std::array<uint8_t,32> message{};message[0]=1;message[9]=0x2b;message[10]=index;
    m.write(0x007f0100,message.data(),message.size());m.reg(UC_MIPS_REG_4,0x807f0100);
    m.reg(UC_MIPS_REG_5,0);m.reg(UC_MIPS_REG_RA,0x803b0000);m.reg(UC_MIPS_REG_SP,0x807fff00);
    manta::check(m.run(0x805db6e8,10000));need(m.reg(UC_MIPS_REG_PC)==0x803b0000,"original mailbox routine did not return");
    return m.reg(UC_MIPS_REG_2);
  };
  need(fire(0)==0 && mailbox[0x700]==1 && mailbox[0x701]==0x2b && mailbox[0x704]==0 && mailbox[0x705]==0,
    "original frame is class2B/index0/count0, not JPEG class12");
  need(fire(0x12)==9 && mailbox[0x704]==0,"original sender must reject occupied FIRE without fabricated completion");
  mailbox.fill(0);need(fire(0x12)==0 && mailbox[0x701]==0x2b && mailbox[0x704]==0x12 && mailbox[0x705]==0,
    "original cleanup frame must retain class2B/index12/count0 ordering");
  manta::OpenRisc cpu;std::ostringstream events;bool first=true;unsigned pending=0;uint32_t blocked=0;
  auto record=[&](const char* kind,uint32_t a,unsigned n,uint32_t v){
    if(!first)events<<',';first=false;
    events<<"{\"kind\":\""<<kind<<"\",\"pc\":"<<cpu.pc()<<",\"address\":"<<a<<",\"width\":"<<n<<",\"value\":"<<v<<'}';
  };
  cpu.read=[&](uint32_t a,unsigned n,bool fetch)->manta::OrWord{
    if(uint64_t(a)+n<=program.size()){
      uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(program[a+i])<<(8*i);
      return {v,0xffffffffu,{}};
    }
    blocked=a;record(fetch?"unmapped_instruction":"unknown_read",a,n,0);
    throw std::runtime_error("unverified CP address map; no register/reset value supplied");
  };
  cpu.write=[&](uint32_t a,unsigned n,manta::OrWord v){
    need(v.known==0xffffffffu,"unknown CP write data cannot become known");
    blocked=a;record(defer?"pending_unimplemented_write":"blocked_write",a,n,v.value);
    if(defer && (a==0xa0201fd4 || a==0xa0201fdc) && n==4){++pending;return;}
    throw std::runtime_error("CP peripheral side effect remains unsupported; no effect applied");
  };
  cpu.trace=[&](uint32_t a,uint32_t word){record("instruction",a,4,word);};
  cpu.reset(0x100,0x8001,r0zero?std::optional<uint32_t>(0):std::nullopt);std::string reason="instruction budget";
  try{for(unsigned i=0;i<10000;++i)cpu.step();}catch(const std::runtime_error& e){reason=e.what();}
  std::cout<<"{\"scope\":\"isolated CP reset-entry hypothesis; NOT integrated TV boot or a ready response\","
    <<"\"original_MIPS_resource_lookup\":true,\"resource_id\":97,\"flash_source\":"<<request[1]
    <<",\"original_MIPS_mailbox_framing_verified\":true,\"mailbox_class\":43,\"cleanup_command_index\":18"
    <<",\"mailbox_parameter_count\":0,\"original_busy_rejection\":9"
    <<",\"compressed_extent\":"<<request[2]<<",\"resource_type\":"<<request[3]
    <<",\"payload_sha256\":\""<<manta::sha256(program)<<"\",\"payload_bytes\":"<<program.size()
    <<",\"entry_hypothesis\":256,\"reset_r0_zero_hypothesis\":"<<(r0zero?"true":"false")
    <<",\"original_CP_instructions\":"<<cpu.instructions()<<",\"stop_pc\":"<<cpu.pc()
    <<",\"blocked_address\":"<<blocked<<",\"pending_unimplemented_writes\":"<<pending
    <<",\"peripheral_effects_applied\":0,\"ready_replies_generated\":0,\"reason\":\""<<reason
    <<"\",\"events\":["<<events.str()<<"]}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
