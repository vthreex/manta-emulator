#pragma once
#include <array>
#include "snapshot/archive.hpp"
#include "video/gop_preview.hpp"
#include "input/remote_queue.hpp"
#include <memory>
#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "peripherals/copro.hpp"
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
  bool exploratoryGeCopySnapshot=false;   // HYPOTHESIS B4: overlapping GE blend copies are read-before-write snapshots
  bool exploratoryGeHeight5Full=false;   // HYPOTHESIS B3': GE copies cover every destination row (6F = height-5 is an encoding artefact)
  unsigned exploratoryGeFillIgnoredReg00=0;   // EXPLORATORY: GE reg00 bits (other than bit 0) assumed not to affect a solid fill
  bool exploratoryCopro=false;          // EXPLORATORY: run the original resource-61 multimedia coprocessor image (OpenRISC, peripherals/copro.hpp) once the guest releases it
  uint32_t exploratoryCoproSeqLo=0, exploratoryCoproSeqHi=0;   // diagnostic: ordered coprocessor access sequence for this RIU-window range
  uint32_t exploratoryCoproCapture=0;    // diagnostic: capture the value sequence written by the coprocessor to this RIU-window address
  uint32_t exploratoryCoproWatch=0;      // diagnostic: report writes to this coprocessor-DRAM word
  std::string exploratoryCoproDumpStart; // write DRAM and the mailbox bytes to FILE / FILE.mail when the coprocessor is released (offline harness input)
  int exploratoryCoproMipsIrq=-1;       // EXPLORATORY: hardware interrupt index (FIQ bank 1/3: 32..63, 96..127) raised toward the MIPS when the coprocessor fires its reply mailbox; -1 = none
  bool exploratoryGeYuvStretch=false;   // EXPLORATORY: nearest stretch for packed YUV422 (GE format 0xE -> 0xE); needs --exploratory-ge-uniform-stretch
  bool exploratoryBdmaCrcMiu=false;     // EXPLORATORY: BDMA CRC32 with a DRAM source (mode 0x330, inferred)
  uint64_t diagnosticRiuWriteLogFrom=UINT64_MAX;   // diagnostic: list guest-written RIU addresses since this cycle count
  std::string diagnosticDramDumpFile; uint32_t diagnosticDramDumpAddr=0, diagnosticDramDumpSize=0;   // diagnostic (observation only): write this DRAM range to FILE when the summary is produced
  std::vector<uint32_t> diagnosticRiuPeeks;   // diagnostic (observation only): report stored value/known bits of these RIU addresses in the summary
  bool exploratoryMvopPlane=false;      // EXPLORATORY: show the framebuffer the guest programs into the MVOP (RIU 0x1F202800 block) as a video plane behind the GOP layers
  std::string diagnosticUsbSaveImage;   // diagnostic (observation only): write the pendrive image including guest writes to this file when the summary is produced
  bool exploratoryRiuZeroUnknown=false;  // LABELLED HYPOTHESIS (diagnostic only): never-defined RIU registers read as 0 and are listed
  std::string exploratoryCoproJpdDump;  // diagnostic: write the JPD output picture (raw packed 4:2:2) to this file
  bool exploratoryCoproJpd=false;       // EXPLORATORY: model the coprocessor JPEG decoder block from its register programming (peripherals/jpd.hpp)
  bool exploratoryCoproZeroUnknown=false; // LABELLED HYPOTHESIS: coprocessor registers with unknown semantics read as 0 (listed in the summary)
  std::string exploratoryUsbPendrive;   // EXPLORATORY: attach a USB mass-storage device (image file) to port 0 (peripherals/usb_host.hpp)
  bool exploratoryGeLine=false;   // EXPLORATORY: GE command 0x10 axis-aligned one-pixel line into format C (video/ge_line.hpp)
  bool exploratoryGeGradientFill=false;   // EXPLORATORY: GE command 0x2030 gradient rectangle fill into format C
  bool exploratoryGeI8Fill=false;   // EXPLORATORY: format-4 rectangle fill, index = low byte of the colour registers
  bool exploratoryGeBlendCopy=false;   // EXPLORATORY: ARGB1555->C surface copy, blend coefficient ONE read as replace
  bool exploratoryGeI2Glyph=false;   // EXPLORATORY: anti-aliased I2 glyph blit hypotheses
  bool exploratoryGeNearestStretch=false;   // EXPLORATORY: also execute NON-uniform ARGB1555->C stretches with nearest sampling (needs opaque-source)
  int exploratoryGeUniformStretch=-1;   // EXPLORATORY: 0 opaque-source, 1 alpha-bit (uniform-colour stretched BitBlt only)
  int exploratoryAfecIndirect=-1;   // HYPOTHESIS: content of the indirectly addressed AFEC register 6A3C (bracket it)
  struct BitHypothesis { uint32_t address; unsigned mask, value; int scBank = -1; bool rmw = false; bool init = false; bool ack = false; };
  std::vector<BitHypothesis> bitHypotheses;   // HYPOTHESIS: --exploratory-mmio-bits ADDR:MASK:VALUE (bit-scoped constants)
  int exploratoryAvdNoise=-1;   // HYPOTHESIS: AFEC noise-magnitude byte BF206A04 reads this constant (requires absent-HSYNC); bracket it
  int exploratoryAvdHtotal=-1;   // HYPOTHESIS: published AFEC HTotal reads this constant (requires absent-HSYNC); bracket it
  int diagnosticVifCrLock=-1;   // BRACKETING: VIF CR_LOCK_STATUS bit0 at 0x1F224471 (run 0 and 1)
  int diagnosticUsbUnpoweredDplus=-1;   // BRACKETING: line-state D+ of a guest-powered-down USB PHY (run 0 and 1)
  bool exploratoryUsbNoDevice=false;   // HYPOTHESIS: powered USB PHY, no device attached (SE0 line state)
  bool candidateSpiNorWrite=false;   // opt-in guest SPI NOR program/erase/WRSR into the copy-on-write overlay
  bool noLoopSkip=false;                 // disable exact busy-wait loop skipping (A/B testing; results are identical either way)
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
  std::vector<std::pair<uint32_t,uint32_t>> watchData;   // read-only observation: guest physical (address,length) whose loads/stores are recorded per instruction
  std::vector<std::pair<uint32_t,uint32_t>> dumpRanges;   // read-only: guest physical (address,length) written to FILE_<addr>.bin after the run
  std::filesystem::path dumpPrefix;
  struct OffscreenSurface { uint32_t address = 0, width = 0, height = 0, pitch = 0; };
  std::vector<OffscreenSurface> offscreenInspector;   // DIAGNOSTIC read-only viewer of a guest surface that is NOT a GOP window (at most one)
  bool provenanceExtensions = false;   // opt-in: identity read-write-back RMW and INS bit-identity propagation (strict boot stays byte-identical when off)
  bool videoPlaneBlackKey = false;   // EXPLORATORY HYPOTHESIS: opaque RGB-black guest pixels are see-through to the video plane (colour key); requires videoPlane
  std::string videoPlane;   // EXPLORATORY presentation: "" (neutral checker), black, blue, setting (guest Blue Screen setting); never changes guest state
  std::filesystem::path gopFrameHistory;   // DIAGNOSTIC: directory receiving a BMP of the GOP preview whenever its content changes (bounded)
  uint64_t gopTimelineFrom=0;   // DIAGNOSTIC: first tick of the GOP write/latch timeline printed in the summary
  std::vector<uint32_t> historyPcs;   // DIAGNOSTIC: also sample the GOP history when one of these original PCs executes
  // Virtual remote: host functions are mapped to raw IR bytes by an explicit table and delivered to the emulated PM IR FIFO.
  std::shared_ptr<RemoteQueue> remoteQueue;      // live host key presses (function names), drained on the emulator thread
  IrKeyMap irKeyMap;                            // function -> raw IR byte (evidence-labelled)
  std::vector<ScriptedKey> irScript;            // deterministic replay: inject at/after a dispatch count
  bool exploratoryPmIrRptLatch=false;           // HYPOTHESIS: RPT latches the popped entry's repeat flag
  // Full-machine save state (see docs/save-state.md). saveState: write a snapshot at the first safe instruction boundary at or after dispatch
  // saveStateAt; loadState: construct the machine normally (same options), then resume from the snapshot instead of the reset vector.
  std::filesystem::path saveState, loadState;
  std::filesystem::path profile;   // DIAGNOSTIC: write an instruction-pointer sampling histogram of the emulation thread (Windows)
  bool stateDigest=false;        // add a per-section SHA-256 digest of the complete serialized machine state to the summary
  uint64_t saveStateAt=0;
  bool saveStateStop=false;      // stop the run right after the snapshot was written
  uint64_t saveStateMaxDefer=200000000;   // give up (fail closed) when no safe boundary occurs this many dispatches after saveStateAt
  std::string stateFingerprint, stateCanonical;   // canonical option lines (the fingerprint is their hash)
  bool loadAllowExtraOptions=false;   // accept a snapshot written under a SUBSET of the current options (extra hypotheses apply only after the load; reported)  // hash of the run configuration, set by main(); a snapshot only loads under the identical configuration
  std::vector<uint32_t> watchPcRegs;   // read-only probe: record registers/stack words at these original PCs
  std::filesystem::path trace, replay, summary, coverage, exportGopFrame;
  std::shared_ptr<GopPreviewStore> previewStore;   // DIAGNOSTIC GOP preview hand-off to the native window (filled on the emulator thread)
  std::filesystem::path tspFirmwareDump;
  std::filesystem::path pm51History;
};
class Msd309Machine {
  friend void ioClass(snap::Ar&, Msd309Machine&);
  struct Poll;
  struct Instruction;
  friend void ioClass(snap::Ar&, Poll&);
  friend void ioClass(snap::Ar&, Instruction&);
  std::vector<uint8_t> dramBacking_; // outlives CPU mappings, shared MIU aliases
  Engine cpu_;
  Flash flash_;
  Riu riu_;
  Options options_;
  VirtualTime virtualTime_;
  Copro copro_;
  void startCopro(uint32_t riuAddress);
  void attachCopro();
  void updateIrqLines();
  uint32_t irqLinesApplied_=0;
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
  mutable std::unordered_map<uint32_t, uint64_t> pcHits_;   // canonical counts; the hot path counts in pcHitsFast_ and flushStats() merges
  struct Poll {
    uint32_t value = 0;
    uint64_t epoch = 0, count = 0;
  };
  std::unordered_map<uint64_t, Poll> polls_;
  uint64_t writeEpoch_ = 0, maxPoll_ = 0;
  mutable std::deque<uint32_t> history_;
  struct Instruction {
    uint64_t tick;
    uint32_t pc, word, ra, sp;
  };
  mutable std::deque<Instruction> recentInstructions_;
  // ---- hot-path statistics (identical observable results; see flushStats/importStats) ----
  static constexpr uint32_t kFastBase = 0x803b0180u, kFastEnd = 0x806b8ce0u;
  mutable std::vector<uint64_t> pcHitsFast_;           // one counter per word of [kFastBase,kFastEnd)
  mutable std::array<uint32_t, 64> histRing_{};        // last 64 executed PCs
  mutable uint64_t histPushes_ = 0;                    // PCs pushed since the last import
  mutable std::vector<Instruction> recentRing_;        // last 4096 instructions in [kFastBase,kFastEnd)
  mutable uint64_t recentPushes_ = 0;
  std::vector<uint8_t> specialPc_;                     // bitmap over [kFastBase,kFastEnd): instruction addresses the full hook must see
  bool allSlow_ = false;                               // an option needs every instruction in the full hook
  uint64_t hookLimit_ = UINT64_MAX;                    // dispatch budget enforced by the hook (replaces Unicorn's per-instruction count hook)
  bool budgetStop_ = false;
  uint32_t shadowRa_ = 0, shadowSp_ = 0, prevWord_ = 0;   // RA/SP as seen before the current instruction
  bool shadowValid_ = false;
  uint64_t shadowGen_ = 0;
  void flushStats() const;                             // hot-path containers -> canonical containers
  void importStats();                                  // canonical containers -> hot-path containers (after a restore)
  void buildSpecialPcs();
  // ---- batched CP0 Count ticks (exactly equivalent to one tick call per instruction) ----
  uint64_t pendingTicks_ = 0, tickRemaining_ = 0;   // ticks not yet given to the engine; ticks until the next Compare crossing
  bool tickSynced_ = false, tickOff_ = false;       // tickRemaining_/tickOff_ valid; Cause.DC set
  void resyncTicks();
  void flushTicks() { if (pendingTicks_) { cpu_.tickCount(uint32_t(pendingTicks_)); pendingTicks_ = 0; } }
  void tickInstr(uint32_t w);
  // ---- exact busy-wait loop skipping (see loopSkipVisit) ----
  struct LoopCand {
    uint32_t pc = 0, period = 0, conf = 0, cooldown = 0, fails = 0;
    uint64_t lastDispatch = 0, gen = 0, flashReads = 0;
    bool haveRegs = false;
    std::array<uint32_t, 34> regs{};
  };
  std::array<LoopCand, 256> loopTab_{};
  uint64_t sideEffectGen_ = 0;   // bumped by everything that could make an otherwise register-identical iteration differ (stores, CP0/FP ops, events, special PCs)
  uint32_t prevPc_ = 0;
  uint64_t skippedInstructions_ = 0;
  bool loopSkipVisit(uint32_t a);
  void recordPc(uint32_t a);
  void pushRecent(uint32_t a, uint32_t w);
  static bool observeBlock(Msd309Machine &m, uint64_t a, uint32_t w);
  uint32_t lastHistoryPc() const { return histPushes_ ? histRing_[(histPushes_ - 1) & 63] : (history_.empty() ? 0 : history_.back()); }
  bool historyEmpty() const { return !histPushes_ && history_.empty(); }
  static void codeHookFull(uc_engine *, uint64_t, uint32_t, void *);
  mutable std::deque<std::string> recentMmio_;   // canonical "recent MMIO" lines; rebuilt from recentMmioBase_ + mmioRing_ by flushStats()
  std::deque<std::string> recentMmioBase_;        // lines imported from a snapshot (older than anything in mmioRing_)
  struct MmioRecord { uint64_t tick = 0; uint32_t pc = 0, address = 0, value = 0, ra = 0, sp = 0; unsigned width = 0; EvStore kind, detail; };
  mutable std::vector<MmioRecord> mmioRing_ = std::vector<MmioRecord>(256);   // last 256 recorded MMIO events, formatted lazily
  mutable uint64_t mmioPushes_ = 0;
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
  // Save-state support: shadow of the CP0 MMU registers written by MTC0 and the guest's TLBWI installs (Unicorn exposes no TLB read-back);
  // the installs are replayed on load before the CPU context is restored.
  struct TlbInstall { uint32_t index, pageMask, entryHi, entryLo0, entryLo1; };
  std::array<uint32_t,32> cp0Shadow_{};
  std::vector<TlbInstall> tlbInstalls_;
  bool tlbwrSeen_=false, replayingTlb_=false;
  std::string snapshotOptionDelta_;   // options present now but not when the snapshot was written (only with loadAllowExtraOptions)
  bool snapshotWritten_=false;
  uint64_t snapshotDeferrals_=0, snapshotFirstWanted_=0;
  uint32_t resumePc_=0;
  bool resumed_=false;
public:
  struct SnapSection { std::string name; std::vector<uint8_t> bytes; };
private:
  std::vector<SnapSection> captureSections(uint32_t pc);
  bool snapshotBoundaryOk(uint32_t pc);
  void writeSnapshot(uint32_t pc);
  void restoreSnapshot(const std::filesystem::path& path);
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
  bool rmwIdentity_ = false;   // set by proveImmediateRmw: the proven read-modify-write writes every sampled bit back to its own position (no bit defined)
  bool proveDiagnosticCache(uint32_t pc, uint32_t address, unsigned width);
  void stop(std::string why, uint32_t address = 0);
  void event(EvText kind, uint32_t address, unsigned width, uint32_t value, EvText detail);
  static void formatEventLine(std::string &out, uint64_t tick, uint32_t pc, std::string_view kind, uint32_t address, unsigned width, uint32_t value,
                              uint32_t ra, uint32_t sp, std::string_view detail, bool cycles, uint64_t cycleCount);
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
  struct DataAccess { uint64_t count = 0, firstDispatch = 0; uint32_t ra = 0, firstValue = 0, lastValue = 0, address = 0; };
  std::map<uint64_t, DataAccess> dataAccess_;   // (pc<<1 | isStore) -> counters; observation only
  std::vector<GopLayer> collectGopLayers() const;
  void publishPreview() const;
  struct IrLogEntry { uint64_t dispatch; std::string function; int raw; bool accepted; std::string note; };
  std::vector<IrLogEntry> irLog_;
  std::deque<std::pair<std::string,uint64_t>> irWaiting_;   // accepted host events not yet deliverable (receiver not enabled yet)
  void serviceVirtualRemote();
  void deliverIrFunction(const std::string& function, uint64_t requestedAt);
  mutable uint64_t historyChecksum_ = 0, historyFrames_ = 0;
  mutable std::string historyIndex_;
  const std::string &uart() const { return riu_.uart; }
};
} // namespace manta
