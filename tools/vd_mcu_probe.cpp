// Original auxiliary firmware diagnostic. It does not boot the MIPS TV image.
#include "peripherals/riu.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
  check(argc==2,"supply the original 22026-byte MCU payload; this is an auxiliary-only diagnostic");
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> code(std::istreambuf_iterator<char>(file),{});
  check(code.size()==22026 && manta::sha256(code)=="2bdb82e863899ea863f63bb4596235615c8f5aa5f77bdc465bec2d465bde22a5","original MCU size/hash mismatch");
  manta::Riu r;r.candidateVdMcuProgram=true;r.exploratoryVdMcuStartup=true;r.exploratoryVdSharedRun=true;
  r.deferVdSfr8eWrite=true;r.deferVdResetKeyWrites=true;r.exploratoryVdSuspendedBoundary=true;
  unsigned traced=0,guestIdleWrites=0,guestReplies=0;
  r.event=[&](const auto& kind,uint32_t address,unsigned,uint32_t value,const auto&){
    if(kind=="vd_mcu_instruction"){
      check(address<code.size() && code[address]==value,"executed opcode differs from original MCU image");++traced;
    }
    if(kind=="vd_shared_mailbox_write" && address==0x1f224c38 && value==0){
      check(r.vdMcuCpu().pc()==0x4eaa,"shared idle byte must come from original startup MOVX");++guestIdleWrites;
    }
    if(kind=="vd_shared_mailbox_write" && address==0x1f224c00 && value==255){
      check(r.vdMcuCpu().pc()==0x4b0c,"completion must come from original IRQ handler MOVX");++guestReplies;
    }
  };
  uint64_t tick=0;auto w=[&](unsigned p,unsigned v){r.write(0x1f206900+p,1,v,0,255,++tick);};
  w(0,1);w(1,0);w(5,0x51);w(8,0);w(9,0);
  for(auto b:code)w(0x18,b);
  w(8,0);w(9,0);for(auto b:code)check(r.read(0x1f206920,1,0,255,++tick)==b,"actual program-port readback failed");
  w(5,0x50);w(5,0);w(1,1);w(0,0);
  r.write(0x1f206700,1,0,0,255,tick+=100000);
  check(r.vdMcuCpu().running() && guestIdleWrites==1 && r.knownBits(0x1f224c38,1)==255,
    "original MCU must initialize shared idle status and run its own main loop");
  auto transaction=[&](unsigned command,unsigned address,unsigned value){
    r.write(0x1f224c04,1,address>>8,0,255,++tick);
    r.write(0x1f224c01,1,address&255,0,255,++tick);
    r.write(0x1f224c05,1,value,0,255,++tick);r.write(0x1f224c00,1,command,0,255,++tick);
    w(5,2);w(5,0);r.write(0x1f206700,1,0,0,255,tick+=4000);
  };
  transaction(2,0x83ab,0x34);
  check(r.read(0x1f224c00,1,0,255,++tick)==255 && guestReplies==1,"original MCU write handler must generate completion");
  r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x83ab,0);
  check(r.read(0x1f224c00,1,0,255,++tick)==255 && r.read(0x1f224c05,1,0,255,++tick)==0x34 && guestReplies==2,
    "original MCU read handler must return the value actually stored by its write handler");
  auto successfulIrqs=r.vdMcuCpu().interruptEntries();
  r.write(0x1f224c00,1,0,0,255,++tick);transaction(2,0x2200,0xff);
  check(r.vdMcuSuspended() && !r.vdMcuCpu().running() && r.read(0x1f224c00,1,0,255,++tick)==2 && guestReplies==2,
    "unmapped hardware transaction cannot yield a fabricated completion");
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"original auxiliary firmware diagnostic; no MIPS boot claimed\","
    <<"\"original_sha256\":\""<<manta::sha256(code)<<"\",\"verified_upload_bytes\":"<<code.size()
    <<",\"original_instructions\":"<<r.vdMcuCpu().totalInstructions()<<",\"first_visit_opcodes_verified\":"<<traced
    <<",\"original_irq_entries_before_failure\":"<<successfulIrqs<<",\"guest_generated_FF_replies\":"<<guestReplies
    <<",\"original_startup_idle_writes\":"<<guestIdleWrites<<",\"unknown_target_stopped_without_ack\":true,\"blocked_target\":\"0x2200\"}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
