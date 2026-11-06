// Original auxiliary firmware diagnostic. It does not boot the MIPS TV image.
#include "peripherals/riu.hpp"
#include "storage/sha256.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
  const bool registers=argc==3 && std::string(argv[2])=="--register-bus";
  check(argc==2 || registers,"supply the original 22026-byte MCU payload [--register-bus]; this is an auxiliary-only diagnostic");
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
  auto transaction=[&](unsigned command,unsigned address,unsigned value,unsigned known=255){
    r.write(0x1f224c04,1,address>>8,0,255,++tick);
    r.write(0x1f224c01,1,address&255,0,255,++tick);
    if(known!=255)r.preparePreservedWrite(0x1f224c05,1,255&~known);
    r.write(0x1f224c05,1,value,0,known,++tick);r.write(0x1f224c00,1,command,0,255,++tick);
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
  if(registers){
    r.symbolicInit=true; // retain deliberately invalid host DATA in the isolated private-RAM copy check
    const auto before=r.vdMcuCpu().totalInstructions();
    r.exploratoryDvbtRegisters=true;
    check(r.knownBits(0x1f224400,1)==0,"enabling register bus must not create reset values");
    check(r.resumeDvbtRegisterAccess(tick),"pending original MOVX can resume when its bus is attached");
    r.write(0x1f206700,1,0,0,255,tick+=4000);
    check(r.vdMcuCpu().totalInstructions()>before && r.read(0x1f224c00,1,0,255,++tick)==255 && guestReplies==3,
      "original ISR alone acknowledges resumed software write");
    check(r.read(0x1f224400,1,0,255,++tick)==255,"original MCU write reaches the shared MIPS register byte");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x2200,0);
    check(r.read(0x1f224c05,1,0,255,++tick)==255 && guestReplies==4,"original read handler returns the actual stored register value");
    check(r.knownBits(0x1f224401,1)==0,"adjacent unwritten register stays unknown");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x2201,0);
    check(guestReplies==5 && r.knownBits(0x1f224c05,1)==0 && r.read(0x1f224c00,1,0,255,++tick)==255,"original RPC may transport an opaque byte but cannot promote any of its bits to known");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(2,0x83ac,0xa5,0);
    if(guestReplies!=6)std::cerr<<"private store: replies="<<guestReplies<<" pc="<<std::hex<<r.vdMcuCpu().pc()<<" boundary="<<r.vdMcuBoundaryReason()<<'\n';
    check(guestReplies==6,"original MCU may cache invalid data in its own private XRAM");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x83ac,0);
    if(guestReplies!=7 || r.knownBits(0x1f224c05,1)!=0)std::cerr<<"private round trip: replies="<<guestReplies<<" known="<<r.knownBits(0x1f224c05,1)<<" boundary="<<r.vdMcuBoundaryReason()<<'\n';
    check(guestReplies==7 && r.knownBits(0x1f224c05,1)==0,"private XRAM round trip cannot turn invalid register data into known bits");
    const auto deviceState=[](manta::Riu& bus){std::vector<uint8_t> bytes;manta::snap::Ar ar(bytes);manta::snap::io(ar,bus);return bytes;};
    auto saved=deviceState(r);auto restored=std::make_unique<manta::Riu>();restored->exploratoryDvbtRegisters=true;
    unsigned restoredReplies=0;
    restored->event=[&](const auto& kind,uint32_t address,unsigned,uint32_t value,const auto&){
      if(kind=="vd_mcu_instruction")check(address<code.size() && code[address]==value,"restored MCU executes an opcode outside the original image");
      if(kind=="vd_shared_mailbox_write" && address==0x1f224c00 && value==255){
        check(restored->vdMcuCpu().pc()==0x4b0c,"restored reply must come from the original ISR");++restoredReplies;}
    };
    manta::snap::Ar load(saved.data(),saved.size());manta::snap::io(load,*restored);
    check(load.remaining()==0 && deviceState(*restored)==saved,"restoring the native MCU must preserve its complete serialized device state");
    // Compare optimized first-visit formatting against the original full
    // formatting path on the same restored guest, including all device bytes.
    const_cast<manta::VdMcuCpu&>(restored->vdMcuCpu()).instructionTextAt={};
    auto restoredTick=tick;
    const auto exercise=[](manta::Riu& bus,uint64_t& t){
      for(auto command:{2u,1u}){
        bus.write(0x1f224c00,1,0,0,255,++t);
        bus.write(0x1f224c04,1,0x83,0,255,++t);bus.write(0x1f224c01,1,0xad,0,255,++t);
        bus.write(0x1f224c05,1,command==2?0x56:0,0,255,++t);bus.write(0x1f224c00,1,command,0,255,++t);
        bus.write(0x1f206905,1,2,0,255,++t);bus.write(0x1f206905,1,0,0,255,++t);
        bus.write(0x1f206700,1,0,0,255,t+=4000);
        check(bus.read(0x1f224c00,1,0,255,++t)==255,"native ISR must still answer after device-state restore");
      }
      check(bus.read(0x1f224c05,1,0,255,++t)==0x56,"restored native read must return the preceding original ISR write");
    };
    exercise(r,tick);exercise(*restored,restoredTick);
    check(guestReplies==9 && restoredReplies==2 && tick==restoredTick && deviceState(r)==deviceState(*restored),
      "continuous and restored original-MCU transactions must leave every serialized device byte identical");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(4,0x13,0x67);
    check(guestReplies==10,"original DSP-parameter command 4 must complete in the original ISR");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(3,0x13,0);
    check(guestReplies==11 && r.knownBits(0x1f224c05,1)==255 && r.read(0x1f224c05,1,0,255,++tick)==0x67,
      "original DSP-parameter command 3 must read the byte stored by command 4");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x8013,0);
    check(guestReplies==12 && r.read(0x1f224c05,1,0,255,++tick)==0x67,
      "original DSP parameter helper must map index 0x13 into private XRAM 0x8013");
    w(0,1);w(0,0);r.write(0x1f206700,1,0,0,255,tick+=100000);
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(3,0x13,0);
    check(guestReplies==13 && r.knownBits(0x1f224c05,1)==255 && r.read(0x1f224c05,1,0,255,++tick)==0x67,
      "CPU reset must not erase the DSP parameter previously written by the original ISR");
    r.write(0x1f224c00,1,0,0,255,++tick);transaction(1,0x7000,0);
    check(r.vdMcuSuspended() && guestReplies==13 && r.read(0x1f224c00,1,0,255,++tick)==1,"unmapped bus access cannot yield a completion");
    std::cout<<"{\"result\":\"PASS\",\"scope\":\"isolated original DVB-T MCU; register mapping only, no RF/lock/channel claim\",\"guest_generated_FF_replies\":13,\"original_DSP_parameter_commands_verified\":true,\"DSP_parameter_retained_across_CPU_reset\":true,\"unwritten_register_payload_stays_INVALID\":true,\"private_XRAM_payload_stays_INVALID\":true,\"restored_original_ISR_replies\":2,\"continuous_restored_device_bytes_identical\":true,\"unmapped_target_has_no_reply\":true,\"resumed_original_MOVX\":true}\n";
    return 0;
  }
  std::cout<<"{\"result\":\"PASS\",\"scope\":\"original auxiliary firmware diagnostic; no MIPS boot claimed\","
    <<"\"original_sha256\":\""<<manta::sha256(code)<<"\",\"verified_upload_bytes\":"<<code.size()
    <<",\"original_instructions\":"<<r.vdMcuCpu().totalInstructions()<<",\"first_visit_opcodes_verified\":"<<traced
    <<",\"original_irq_entries_before_failure\":"<<successfulIrqs<<",\"guest_generated_FF_replies\":"<<guestReplies
    <<",\"original_startup_idle_writes\":"<<guestIdleWrites<<",\"unknown_target_stopped_without_ack\":true,\"blocked_target\":\"0x2200\"}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
