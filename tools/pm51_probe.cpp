// Bounded original PM51 execution, independent of main-machine PM readiness.
#include "cpu/vd_mcu.hpp"
#include "storage/sha256.hpp"
#include "peripherals/riu.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
int main(int argc,char** argv){try{
  const bool counterfactual=argc==4 && std::string(argv[2])=="--status-counterfactual";
  const bool history=argc==3 && std::string(argv[2])=="--shared-riu-symbolic-history";
  const bool deferred=argc==3 && std::string(argv[2])=="--probe-pending-startup-configurations";
  const bool uart=deferred || (argc==3 && std::string(argv[2])=="--shared-riu-power-good-uart-hypothesis");
  const bool powerGood=uart || (argc==3 && std::string(argv[2])=="--shared-riu-power-good-hypothesis");
  if(counterfactual || history || powerGood || (argc==3 && (std::string(argv[2])=="--shared-riu-symbolic" || std::string(argv[2])=="--shared-riu-status-fixture" || std::string(argv[2])=="--shared-riu-power-alias-fixture"))){
    const bool fixture=std::string(argv[2])=="--shared-riu-status-fixture";
    const bool powerFixture=std::string(argv[2])=="--shared-riu-power-alias-fixture";
    std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> code(std::istreambuf_iterator<char>(f),{});
    manta::Riu r;r.symbolicInit=true;
    r.flashRead=[&](uint32_t a,size_t n){
      if(a<0x10000 || uint64_t(a)+n>0x20000)throw std::runtime_error("PM probe SPI source out of range");
      return std::vector<uint8_t>(code.begin()+a-0x10000,code.begin()+a-0x10000+n);};
    r.event=[](const auto& k,auto a,auto n,auto v,const auto& d){
      std::cout<<"PM_CONNECTED_EVENT "<<k<<" address 0x"<<std::hex<<a<<" width "<<n<<" value 0x"<<v<<' '<<d<<'\n';};
    r.configureExploratoryPm51(code,32,history || counterfactual || powerGood,uart);
    if(deferred){r.configureProbePmDeferredConfigurations();
      std::cout<<"PM_PROBE_PENDING_CONFIGURATIONS three exact original stores may retire; effects remain unknown, not hardware emulation or verified TV boot\n";}
    if(powerGood){
      r.configureExploratoryPmPowerGood(true);
      std::cout<<"PM_POWER_GOOD_HYPOTHESIS aggregate external powered input from related CheckPowerGood loop; NOT validated MSD309 detector mapping, no marker/ready response\n";
    }
    if(counterfactual){
      auto value=std::string(argv[3]);
      if(value.size()!=1 || value[0]<'0' || value[0]>'7')
        throw std::runtime_error("counterfactual low status field must be exactly 0..7");
      unsigned field=unsigned(value[0]-'0');
      // Isolated input sensitivity experiment, not a peripheral model. Only
      // these low three input bits are supplied; the high byte stays invalid.
      // This option deliberately does not exist in the television executable.
      r.prepareRmw(0x1f001d84,7);r.write(0x1f001d84,1,0xa0|field,0,7,0);
      std::cout<<"PM_COUNTERFACTUAL_INPUT low bits="<<field
        <<"; SYNTHETIC input only, no physical producer/reset claim, not television boot\n";
    }
    if(powerFixture){
      std::cout<<"PM_POWER_ALIAS_FIXTURE bridge-only test; injected writes below are NOT original PM execution/responses\n";
      r.configureExploratoryPmAcOn();
      auto first=r.read(0x1f001cdd,1,0,0,0);auto alias=r.pm51().sharedRead(0x0e6f);
      if(first!=255 || alias.value!=255 || alias.known!=255)throw std::runtime_error("PM/MIPS external power cause views disagree");
      r.pm51().sharedWrite(0x0e6f,{1,255,0});
      if(r.read(0x1f001cdd,1,0,0,0)!=1 || r.pm51().sharedRead(0x0e6f).value!=1)
        throw std::runtime_error("shared power field write hidden behind external fixture");
      r.pm51().sharedWrite(0x0e6f,{0xa5,0,0x0e6f});
      if(r.knownBits(0x1f001cdd,1) || r.pm51().sharedRead(0x0e6f).known)
        throw std::runtime_error("external fixture promoted invalid shared power field");
      std::cout<<"PM_POWER_ALIAS_FIXTURE_PASS all views coherent, unknown write stays invalid; original instructions="<<r.pm51().cpu().totalInstructions()<<'\n';
      std::cout<<"PM_MAIN_MIPS_EXECUTED false; shared RIU probe only; no PM reply supplied\n";
      return 0;
    }
    if(fixture){
      // Bus-coupling/nonready fault fixture, never a stock reset or ready value.
      r.prepareRmw(0x1f001d84,7);r.write(0x1f001d84,1,0xa4,0,7,0);
      std::cout<<"PM_STATUS_FIXTURE lower bits=4; NOT a measured reset/ready response\n";
    }
    r.read(0x1f001cd1,1,0,0,500000);
    const auto& pm=r.pm51();
    std::cout<<"PM_CONNECTED_INSTRUCTIONS "<<std::dec<<pm.cpu().totalInstructions()<<" PC 0x"<<std::hex<<pm.cpu().pc()<<'\n';
    std::cout<<"PM_CONNECTED_READS "<<std::dec<<pm.riuReads<<" RAM_WRITES "<<pm.ramWrites<<" RAM_READS "<<pm.ramReads<<'\n';
    std::cout<<"PM_CONNECTED_BOUNDARY "<<pm.boundary()<<'\n';
    std::cout<<"PM_CONNECTED_UNKNOWN_ORIGIN 0x"<<std::hex<<pm.cpu().accumulatorOrigin()
      <<" ACC_KNOWN 0x"<<unsigned(pm.cpu().accumulatorKnown())<<'\n';
    std::cout<<"PM_CONNECTED_STATE_SHA256 "<<pm.stateSha()<<'\n';
    std::cout<<"PM_CONNECTED_SUSPENDED "<<(pm.suspended()?"true":"false")<<" MARKER_WRITES "<<pm.markerWrites
      <<" MAIN_BYTE_KNOWN "<<r.knownBits(0x1f001cd1,1)<<'\n';
    std::cout<<"PM_MAIN_MIPS_EXECUTED false; shared RIU probe only; no PM reply supplied\n";
    if(deferred)std::cout<<"PM_PENDING_CONFIGURATIONS "<<pm.deferredConfigurationCount()
      <<" SHARED_PAD_KNOWN "<<r.knownBits(0x1f001cd4,1)
      <<" SHARED_E26_KNOWN "<<r.knownBits(0x1f001c4c,2)<<'\n';
    if(history || counterfactual || powerGood)
      std::cout<<"PM_HISTORY_BEGIN\n"<<pm.historyJsonLines()<<"PM_HISTORY_END\n";
    return 0;
  }
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
