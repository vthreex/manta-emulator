#pragma once
#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "board/unknown_observations.hpp"
#include "board/preserved_bits.hpp"
#include "board/audio_no_ack.hpp"
#include "board/pm_activity_scenario.hpp"
#include "board/virtual_time.hpp"
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
namespace manta {
struct Options {
  int exploratoryScBank = -1;
  int exploratoryGammaEnable = -1;
  int exploratorySubwindowEnable = -1;
  int exploratoryDlcPending = -1;
  int exploratoryCombBank = -1;
  int exploratoryGopBank = -1;
  NorModel flashModel = NorModel::W25Q64BV;
  SpiUnknownInput spiUnknownInput = SpiUnknownInput::Stop;
  bool stage2 = false, initPrefix = false, symbolicInit = false, candidateBdma = false,
       candidateUart = false, gpioI2c = false, candidateMiu = false, traceInstructions = false,
       i2cSdaStuck = false, deferDiagnosticReads = false, virtualRtc = false, cp0Timer = false,
       candidateUtmi = false, candidateSpi = false, candidateChipId = false, candidateGamma = false;
  uint64_t limit = 1000000;
  uint64_t exploratoryDisplayClock = 0;
  bool exploratoryDlcTimeout=false;
  bool exploratoryCombRestore=false;
  bool candidateSemaphore=false, exploratorySemaphoreIdle=false;
  bool candidateGe=false, exploratoryGeReset=false;
  bool candidateGopPalette=false;
  bool candidateTsp=false, exploratoryTspResetLow=false;
  bool exploratoryTspCpu=false;
  bool deferPlatformIdentity=false;
  bool exploratoryPmAcOn=false;
  bool exploratoryPm51SpiStartup=false;
  bool exploratoryPmPowerGood=false;
  bool candidatePm51Uart=false;
  bool explorePmNonresumeBranch=false;
  uint64_t exploratoryCpi=1;
  bool coproProbes=false;
  uint64_t pm51Divider=32;
  bool exploratorySarFullscale=false,orFirstVisits=false;
  bool exploratoryIrDmaReset=false;
  bool exploratoryGopResetWindows=false;
  bool exploratoryScartGrounded=false;
  bool graphicsProbes=false;
  bool osdProbes=false;
  int exploratoryScmiFormat=-1;
  int exploratoryFetchRevisionByte=-1;
  bool exploratoryAnalogDisconnected=false;
  bool exploratoryPqSramIdle=false;
  bool exploratoryPqDisabledPorts=false;
  bool candidateVdMcuProgram=false;
  bool candidateAvdMcuProgram=false;
  bool candidateAudioIdma=false;
  bool exploratoryAudioNoAck=false;
  bool exploratoryAvdMcuStartup=false,exploratoryAvdSuspendedBoundary=false;
  bool exploratoryVdMcuStartup=false;
  bool deferVdSfr8eWrite=false;
  bool deferVdResetKeyWrites=false;
  bool exploratoryVdSharedRun=false;
  bool exploratoryVdSuspendedBoundary=false;
  bool exploratoryCiDetectHigh=false;
  uint64_t orBudget=10000000;
  uint64_t orDivider=1;
  uint32_t breakpoint = 0, pauseMmio = 0;
  std::filesystem::path trace, replay, summary, coverage;
  std::filesystem::path tspFirmwareDump;
  std::filesystem::path pm51History;
};
class Msd309Machine {
  std::vector<uint8_t> dramBacking_; // outlives CPU mappings, shared MIU aliases
  Engine cpu_;
  Flash flash_;
  Riu riu_;
  Options options_;
  VirtualTime virtualTime_;
  uint64_t coproLoadInspections_=0;
  std::string coproLoadedSha_;
  std::ofstream trace_;
  std::ifstream replay_;
  std::set<uint32_t> coverage_;
  std::unordered_map<uint32_t, uint64_t> pcHits_;
  struct Poll {
    uint32_t value = 0;
    uint64_t epoch = 0, count = 0;
  };
  std::unordered_map<uint64_t, Poll> polls_;
  uint64_t writeEpoch_ = 0, maxPoll_ = 0;
  std::deque<uint32_t> history_;
  struct Instruction {
    uint64_t tick;
    uint32_t pc, word, ra, sp;
  };
  std::deque<Instruction> recentInstructions_;
  std::deque<std::string> recentMmio_;
  UnknownObservations unknownObservations_;
  PreservedBits preservedBits_;
  AudioNoAck audioNoAck_;
  PmActivityScenario pmActivityScenario_;
  unsigned deferredReads_ = 0;
  uint64_t timerEntries_ = 0, gopEvents_ = 0, geEvents_ = 0, geBlockedEvents_=0;
  bool gopInitializerEntered_ = false;
  uint64_t gopInitializerEntries_ = 0;
  uint64_t gopWindowProgramEntries_=0,gopWrite32Entries_=0;
  uint64_t fetchPatchEnabled_=0,fetchPatchDisabled_=0;
  std::map<uint32_t,uint64_t> osdProbeEntries_;
  uint64_t scalerInitializerEntries_=0, scalerInitializerReturns_=0, dlcInitializerReturns_=0;
  bool spiJedecRead_ = false, spiDescriptorSelected_ = false;
  std::array<uint8_t,3> spiJedec_{};
  std::array<uint8_t,40> spiDescriptor_{};
  uint64_t dispatches_ = 0, mmio_ = 0, flashReads_ = 0, dramWrites_ = 0;
  bool stopped_ = false;
  uint32_t blockingPc_ = 0, blockingAddress_ = 0;
  std::string reason_;
  bool stage2Reached_ = false, decompressReached_ = false, runtimeReached_ = false;
  std::string guestDecompressedSha_;
  bool preparingPrefix_ = false;
  uint32_t proveImmediateRmw(uint32_t pc, uint32_t address, unsigned width);
  bool proveDiagnosticCache(uint32_t pc, uint32_t address, unsigned width);
  void stop(std::string why, uint32_t address = 0);
  void event(const std::string &kind, uint32_t address, unsigned width, uint32_t value,
             const std::string &detail);
  static void codeHook(uc_engine *, uint64_t, uint32_t, void *);
  static void memoryHook(uc_engine *, uc_mem_type, uint64_t, int, int64_t, void *);
  static bool invalidHook(uc_engine *, uc_mem_type, uint64_t, int, int64_t, void *);
  static void interruptHook(uc_engine *, uint32_t, void *);
  static uint64_t mmioRead(uc_engine *, uint64_t, unsigned, void *);
  static void mmioWrite(uc_engine *, uint64_t, unsigned, uint64_t, void *);

public:
  Msd309Machine(Flash flash, Options options);
  int run();
  std::string summary() const;
  const std::string &uart() const { return riu_.uart; }
};
} // namespace manta
