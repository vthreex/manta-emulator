// Bounded original PM51 execution, independent of main-machine PM readiness.
#include "cpu/vd_mcu.hpp"
#include "storage/sha256.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
int main(int argc,char** argv){try{
  const bool candidateSpi=argc==3 && std::string(argv[2])=="--candidate-spi-private-ram";
  if(argc!=2 && !(argc==3 && (candidateSpi || std::string(argv[2])=="--exploratory-private-ram")))
    throw std::runtime_error("supply original PM51 candidate [--exploratory-private-ram | --candidate-spi-private-ram]");
  const bool exploratory=argc==3;
  std::ifstream f(argv[1],std::ios::binary);
  std::vector<uint8_t> code(std::istreambuf_iterator<char>(f),{});
  manta::VdMcuCpu cpu;
  std::array<manta::VdByte,1024> ram{};
  uint64_t ramWrites=0,ramReads=0;unsigned deferredWrites=0,spiControlWrites=0;
  std::optional<uint8_t> romControl;
  std::vector<uint8_t> trace;
  auto traceAddress=[&](uint8_t kind,uint16_t address){
    trace.push_back(kind);trace.push_back(uint8_t(address));trace.push_back(uint8_t(address>>8));
  };
  std::set<uint16_t> visits;
  cpu.instruction=[&](uint16_t pc,uint8_t opcode,const std::string& text){
    if(opcode!=code[pc])throw std::runtime_error("PM opcode differs from original flash bytes");
    traceAddress(1,pc);trace.push_back(opcode);trace.push_back(cpu.stack());
    trace.push_back(cpu.accumulator());trace.push_back(cpu.status());
    if(visits.insert(pc).second)
      std::cout<<"PM_INSTRUCTION 0x"<<std::hex<<pc<<' '<<text<<'\n';
  };
  cpu.external=[&](uint16_t a,bool w,uint8_t value){
    traceAddress(4,a);trace.push_back(w);if(w)trace.push_back(value);
    std::cout<<"PM_XDATA "<<(w?"write":"read")<<" 0x"<<std::hex<<a;
    if(w)std::cout<<" value 0x"<<unsigned(value);else std::cout<<" unknown; no value returned";
    std::cout<<'\n';
  };
  if(exploratory){
    cpu.busWrite=[&](uint16_t a,manta::VdByte value){
      traceAddress(2,a);trace.push_back(value.value);trace.push_back(value.known);
      if(candidateSpi && a==0x1018 && value.known==255 && value.value==2){
        // Related Amber3/Kaiser headers call this ROM_CODE_CTRL/SPI_ENABLE.
        // The original PM reset write corroborates this candidate selection.
        // The configured 64-KiB SPI view remains the same original code storage;
        // SRAM/DRAM/cache/banking commands have no guessed implementation.
        romControl=value.value;++spiControlWrites;
        std::cout<<"PM_CANDIDATE_ROM_CONTROL SPI enabled by original write; configured flash window 0x10000..0x20000\n";
        return true;
      }
      if(!candidateSpi && cpu.pc()==0x60dd && a==0x1018 && value.known==255 && value.value==2 && !deferredWrites){
        ++deferredWrites;
        std::cout<<"PM_DEFERRED_UNKNOWN_WRITE PC 0x60dd XDATA 0x1018 value 0x2; effects unknown\n";
        return true;
      }
      if(a>=0x4000 && a<0x4400){ram[a-0x4000]=value;++ramWrites;return true;}
      return false;
    };
    cpu.busRead=[&](uint16_t a)->std::optional<manta::VdByte>{
      if(candidateSpi && a==0x1018 && romControl){
        traceAddress(3,a);trace.push_back(*romControl);trace.push_back(255);
        return manta::VdByte{*romControl,255,0};
      }
      if(a>=0x4000 && a<0x4400){
        auto value=ram[a-0x4000];
        if(value.known!=255)throw manta::VdHardwareBoundary("PM private RAM read before original initialization");
        traceAddress(3,a);trace.push_back(value.value);trace.push_back(value.known);
        ++ramReads;return value;
      }
      return {};
    };
  }
  cpu.resetPmCandidate(code);
  std::cout<<"PM_ORIGINAL_SHA256 "<<manta::sha256(code)<<'\n';
  std::cout<<"PM_MODE "<<(candidateSpi?"candidate SPI control, configured boot view and isolated RAM":exploratory?"exploratory isolated RAM and deferred first write":"bounded standard 8051 reset; no XDATA model")<<'\n';
  std::string boundary;
  try{for(uint64_t i=0;i<1000000;++i)cpu.step();}
  catch(const manta::VdHardwareBoundary& e){boundary=e.what();}
  if(boundary.empty())throw std::runtime_error("PM probe reached budget without an identified boundary");
  std::cout<<"PM_BOUNDARY "<<boundary<<'\n';
  std::cout<<"PM_ORIGINAL_INSTRUCTIONS "<<std::dec<<cpu.totalInstructions()<<" PC 0x"<<std::hex<<cpu.pc()<<'\n';
  std::cout<<"PM_PRIVATE_RAM_WRITES "<<std::dec<<ramWrites<<" READS "<<ramReads<<" DEFERRED_WRITES "<<deferredWrites<<'\n';
  std::cout<<"PM_SPI_CONTROL_WRITES "<<spiControlWrites<<" ORIGINAL_PROGRAM_UNCHANGED "
           <<(manta::sha256(code)=="5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c"?"true":"false")<<'\n';
  std::cout<<"PM_ALL_INSTRUCTION_BUS_TRACE_SHA256 "<<manta::sha256(trace)<<'\n';
  std::cout<<"PM_MAIN_MACHINE_ATTACHED false; no PM reply, marker, interrupt or ready signal supplied\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
