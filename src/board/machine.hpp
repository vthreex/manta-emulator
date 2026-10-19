#pragma once
#include "video/gop_preview.hpp"
#include <memory>
#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include "board/unknown_observations.hpp"
#include "board/preserved_bits.hpp"
#include "board/audio_no_ack.hpp"
#include "board/pm_activity_scenario.hpp"
#include "board/virtual_time.hpp"
#include "board/opaque_read_policy.hpp"
#include "board/zui_flow.hpp"
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
namespace manta {
struct Options {
  std::string opaqueReadPolicy;
  uint64_t opaquePolicyWaitMs=0;
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
  bool candidateGe=false, candidateGeRectFill=false, candidateGeBitBlt=false, exploratoryGeReset=false;
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
  bool noOrQmemTrace=false;
  bool orIdlePollChanges=false;
  bool orRegisterLogChanges=false;
  bool exploratoryGopVsyncLatch=false;
  uint64_t pm51Divider=32;
  bool exploratorySarFullscale=false,orFirstVisits=false;
  bool exploratoryIrDmaReset=false;
  bool exploratoryIrCapturePresent=false;
  bool candidatePmIrFifo=false,exploratoryPmIrResetEmpty=false;
  bool exploratoryGopResetWindows=false;
  bool exploratoryScartGrounded=false;
  bool exploratoryScartIdGrounded=false;
  bool graphicsProbes=false;
  bool osdProbes=false;
  bool zuiFlowProbes=false;
  int exploratoryScmiFormat=-1;
  int exploratoryFetchRevisionByte=-1;
  bool exploratoryAnalogDisconnected=false;
  bool exploratoryAvdNoHsync=false;
  int exploratoryVifRevision=-1;
  uint64_t exploratoryVifZeroInputSettlingCycles=0;
  bool exploratoryAvdNoSyncEvents=false;
  int diagnosticAvd11dbBit7=-1;
  bool diagnosticAvdInvalidPublication=false;
  bool diagnosticRestoreByIdentityBranch=false;   // DIAGNOSTIC: branch whose two arms both rewrite the tested bit to its tested value
  bool exploratoryGeBlendCopy=false;   // EXPLORATORY: ARGB1555->C surface copy, blend coefficient ONE read as replace
  bool exploratoryGeI2Glyph=false;   // EXPLORATORY: anti-aliased I2 glyph blit hypotheses
  int exploratoryGeUniformStretch=-1;   // EXPLORATORY: 0 opaque-source, 1 alpha-bit (uniform-colour stretched BitBlt only)
  int exploratoryAvdNoise=-1;   // HYPOTHESIS: AFEC noise-magnitude byte BF206A04 reads this constant (requires absent-HSYNC); bracket it
  int exploratoryAvdHtotal=-1;   // HYPOTHESIS: published AFEC HTotal reads this constant (requires absent-HSYNC); bracket it
  int diagnosticVifCrLock=-1;   // BRACKETING: VIF CR_LOCK_STATUS bit0 at 0x1F224471 (run 0 and 1)
  int diagnosticUsbUnpoweredDplus=-1;   // BRACKETING: line-state D+ of a guest-powered-down USB PHY (run 0 and 1)
  bool exploratoryUsbNoDevice=false;   // HYPOTHESIS: powered USB PHY, no device attached (SE0 line state)
  bool candidateSpiNorWrite=false;   // opt-in guest SPI NOR program/erase/WRSR into the copy-on-write overlay
  bool diagnosticHtotalBranchJoin=false; // DIAGNOSTIC: conservative join of the HTotal comparison outcomes
  bool exploratoryPqSramIdle=false;
  bool exploratoryPqDisabledPorts=false;
  bool candidateVdMcuProgram=false;
  bool candidateAvdMcuProgram=false;
  bool candidateAudioIdma=false;
  bool exploratoryAudioNoAck=false;
  bool exploratoryAvdMcuStartup=false,exploratoryAvdSuspendedBoundary=false;
  bool exploratoryAvdSharedRun=false,diagnosticAvdDeferredStartup=false;
  bool diagnosticAvdSymbolicCodeTables=false;
  bool exploratoryAvdHostResetEmpty=false;
  uint64_t avdDivider=32,avdBudget=1000000;
  bool exploratoryVdMcuStartup=false;
  bool deferVdSfr8eWrite=false;
  bool deferVdResetKeyWrites=false;
  bool exploratoryVdSharedRun=false;
  bool exploratoryVdSuspendedBoundary=false;
  bool exploratoryCiDetectHigh=false;
  uint64_t orBudget=10000000;
  uint64_t orDivider=1;
  uint32_t breakpoint = 0, pauseMmio = 0;
  std::vector<std::pair<uint32_t,uint32_t>> dumpRanges;   // read-only: guest physical (address,length) written to FILE_<addr>.bin after the run
  std::filesystem::path dumpPrefix;
  struct OffscreenSurface { uint32_t address = 0, width = 0, height = 0, pitch = 0; };
  std::vector<OffscreenSurface> offscreenInspector;   // DIAGNOSTIC read-only viewer of a guest surface that is NOT a GOP window (at most one)
  std::vector<uint32_t> watchPcRegs;   // read-only probe: record registers/stack words at these original PCs
  std::filesystem::path trace, replay, summary, coverage, exportGopFrame;
  std::shared_ptr<GopPreviewStore> previewStore;   // DIAGNOSTIC GOP preview hand-off to the native window (filled on the emulator thread)
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
  uint64_t suppressedOrQmemTrace_=0;
  uint64_t suppressedOrIdlePolls_=0;
  uint32_t lastOrIdlePollPc_=UINT32_MAX;
  uint64_t suppressedOrStoredReads_=0;
  std::map<uint64_t,std::pair<uint32_t,uint32_t>> lastOrStoredRead_;
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
  ZuiFlow zuiFlow_;
  uint64_t scalerInitializerEntries_=0, scalerInitializerReturns_=0, dlcInitializerReturns_=0;
  bool spiJedecRead_ = false, spiDescriptorSelected_ = false;
  std::array<uint8_t,3> spiJedec_{};
  std::array<uint8_t,40> spiDescriptor_{};
  uint64_t dispatches_ = 0, mmio_ = 0, flashReads_ = 0, dramWrites_ = 0;
  bool stopped_ = false;
  bool joinResume_ = false;
  bool restoreBranchSeen_ = false;
  uint32_t joinResumePc_ = 0;
  uint64_t htotalJoins_ = 0;
  bool applyHtotalBranchJoin();
  uint32_t blockingPc_ = 0, blockingAddress_ = 0;
  std::string reason_;
  bool stage2Reached_ = false, decompressReached_ = false, runtimeReached_ = false;
  std::string guestDecompressedSha_;
  bool preparingPrefix_ = false;
  uint64_t opaquePolicyReads_=0;
  std::string opaquePolicyRules_;
  bool observePolicyRead(uint32_t pc,uint32_t address,unsigned width);
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
  void exportGopFrames() const;
  void dumpGuestRanges() const;
  struct WatchHit { uint64_t dispatch; uint32_t reg[32]; uint32_t stack[8]; };
  std::map<uint32_t, std::pair<uint64_t, std::vector<WatchHit>>> watchHits_; // pc -> (count, first 6 + last 18 hits)
  std::vector<uint8_t> watchSet_;   // bitmap over code addresses (empty = off)
  std::vector<GopLayer> collectGopLayers() const;
  void publishPreview() const;
  const std::string &uart() const { return riu_.uart; }
};
} // namespace manta
