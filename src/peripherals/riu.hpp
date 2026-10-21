#pragma once
#include "snapshot/archive.hpp"
#include "peripherals/pm_ir_fifo.hpp"
#include "peripherals/vif_zero_input.hpp"
#include "peripherals/avd_mcu_bus.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include "peripherals/gpio_i2c.hpp"
#include "peripherals/rtc.hpp"
#include "peripherals/utmi.hpp"
#include "peripherals/usb_host.hpp"
#include "peripherals/spi_isp.hpp"
#include "peripherals/semaphore.hpp"
#include "video/gamma.hpp"
#include "video/scaler_timing.hpp"
#include "video/ge_queue.hpp"
#include "video/ge_rect_fill.hpp"
#include "video/ge_bitblt.hpp"
#include "video/gop_palette.hpp"
#include "video/gop_update.hpp"
#include "peripherals/tsp.hpp"
#include "cpu/openrisc.hpp"
#include "peripherals/pm_boot.hpp"
#include "peripherals/pm51.hpp"
#include "peripherals/pm_power_good.hpp"
#include "peripherals/sar.hpp"
#include "peripherals/ir_dma.hpp"
#include "inputs/scart.hpp"
#include "video/pq_sram.hpp"
#include "peripherals/vd_mcu_program.hpp"
#include "peripherals/avd_mcu_program.hpp"
#include "cpu/vd_mcu.hpp"
#include "audio/idma.hpp"
#include <set>
#include <bitset>
namespace manta {
struct RiuByte {
  uint8_t value = 0xa5, known = 0;
};
class Riu {
  friend void ioClass(snap::Ar&, Riu&);
  GopUpdate gopUpdate_;
  std::map<uint32_t,RiuByte> gopActive_;
  std::optional<uint64_t> gopSyncEdge(uint64_t tick)const;
  void publishGopAck();
  void latchGopRegisters(uint64_t tick,const char* cause);
  void advanceGopUpdate(uint64_t tick);
  int exploratoryFetchRevisionByte_=-1;
  VdMcuProgram vdMcuProgram_;
  AvdMcuProgram avdMcuProgram_;
  VdMcuCpu vdMcuCpu_;
  VdMcuCpu avdMcuCpu_;
  AvdMcuBus avdMcuBus_;
  uint64_t avdTick_=0;
  void advanceAvd(uint64_t tick);
  AudioIdma audioIdma_[2];
  bool avdMcuSuspended_=false;
  std::string avdMcuBoundaryReason_;
  RiuByte vdFiqMask_;
  std::array<RiuByte,2> vdVersionBytes_{};
  std::array<RiuByte,1024> vdPrivateRam_{};
  uint64_t vdTick_=0,vdMailboxReads_=0,vdMailboxWrites_=0;
  std::set<uint16_t> vdVisited_;
  std::map<uint16_t,VdByte> vdLastMailboxReads_;
  void advanceVd(uint64_t tick);
  std::optional<VdByte> vdRead(uint16_t address);
  bool vdWrite(uint16_t address,VdByte value);
  bool vdMcuSuspended_=false;
  std::string vdMcuBoundaryReason_;
  std::map<uint32_t, RiuByte> bytes_;
  uint64_t bdmaComplete_ = 0;
  bool bdmaPending_ = false;
  std::vector<uint8_t> bdmaCopyBytes_;
  uint32_t bdmaCopyDestination_=0;
  std::map<uint32_t, uint32_t> pendingRmw_;
  std::map<uint32_t, uint32_t> consumedRead_;
  std::map<uint32_t,uint32_t> preservedUnknownWrite_;
  std::optional<uint32_t> opaqueRead_;
  bool uartEnabled_ = false;
  GpioI2c gpio_;
  Rtc rtc_[2];
  UtmiCalibration utmi_[2];
  SpiIsp spi_;
  HardwareSemaphore semaphore_;
  GammaRam gamma_;
  ScalerTiming scaler_;
  GeQueue ge_;
  GopPalette palette_;
  TspQueue tsp_;
  std::optional<uint32_t> tspReadTarget_;
  uint64_t tspIndirectReads_=0,tspIndirectWrites_=0;
  TspDownload tspDownload_;
  uint32_t tspDownloadSource_=0;
  OpenRisc tspCpu_;
  bool exploratoryTspCpu_=false;
  PmBootReason pmBoot_;
  bool pmPowerReasonWritten_=false;
  Pm51 pm51_;
  PmPowerGood pmPowerGood_;
  std::span<const uint8_t> pmCode_;
  bool exploratoryPm51_=false;
  void advancePm(uint64_t tick);
  VdByte pmRead(uint16_t address);
  bool pmWrite(uint16_t address,VdByte value);
  SarAdc sar_;bool exploratorySarFullscale_=false;
  bool exploratoryScartIdGrounded_=false;
  uint64_t scartIdSamples_=0;
  ScartGroundedInput scart_;
  bool analogDisconnected_=false;
  bool exploratoryAvdNoHsync_=false;
  uint64_t avdHsyncReads_=0;
  bool exploratoryUsbNoDevice_=false;
  int diagnosticUsbUnpoweredDplus_=-1;
  int diagnosticVifCrLock_=-1;
  std::string guardedStretchDetail_;
  int exploratoryAvdHtotal_=-1;
  int exploratoryAvdNoise_=-1;
  struct HypothesisBits { uint32_t address; uint8_t mask, value; uint64_t reads; int scBank; bool rmw = false; bool init = false; bool ack = false; bool pending = false; };   // ack: peer-acknowledged mailbox byte (the next read after a guest write returns VALUE)   // rmw: guest-writable register, only MASK is hardware-owned; carried-back stores allowed
  std::vector<HypothesisBits> hypothesisBits_;   // EXPLORATORY: bit-scoped constants for hardware-owned status bytes
  int exploratoryAfecIndirect_=-1;   // -1 off, else initial 16-bit content of the indirectly addressed register 6A3C
  uint16_t afecIndirectValue_=0;
  uint64_t afecIndirectReads_=0,afecIndirectWrites_=0;
  uint64_t avdNoiseReads_=0;
  uint64_t avdHtotalReads_=0;
  uint64_t vifCrLockReads_=0;
  bool exploratoryAvdNoSyncEvents_=false;
  int diagnosticAvd11dbBit7_=-1;
  int exploratoryVifRevision_=-1;
  uint64_t vifRevisionReads_=0;
  VifZeroInput vifZeroInput_;
  uint64_t vifZeroInputReads_=0;
  void updateVifZeroInput(uint64_t tick);
  PqSram pqSram_;
  IrDma irDma_;bool exploratoryIrDmaReset_=false;
  bool exploratoryIrCapturePresent_=false;
  uint64_t irCaptureStatusReads_=0;
  PmIrFifo pmIrFifo_;
  bool candidatePmIrFifo_=false;
  uint64_t pmIrStatusReads_=0;
  bool exploratoryGopResetWindows_=false;
  std::array<bool,2> gopResetHeld_{};
  uint64_t gopResetPulses_=0;
  bool orFirstVisits_=false;
  uint64_t orBudget_=10000000,orOriginalWords_=0,orModifiedWords_=0;
  uint64_t orDivider_=1;
  std::set<uint32_t> orVisited_;
  std::bitset<4096> orVisitedQmem_;
  uint64_t tspCpuTick_=0,tspCpuReads_=0,tspCpuWrites_=0;
  struct TspMapping {uint32_t base=0,mask=0;bool known=false;};
  std::array<TspMapping,2> tspBurstMapping_{};
  bool tspBurstMappingActive_=false;
  void advanceTsp(uint64_t tick);
  OrWord tspCpuRead(uint32_t address,unsigned width,bool fetch);
  void tspCpuWrite(uint32_t address,unsigned width,OrWord value);
  uint32_t tspSelectedTarget() const;
  static uint32_t tspPayloadKey(uint32_t target,unsigned lane);
  std::optional<bool> lastOutputSync_;
  uint64_t outputSyncReads_=0, outputSyncTransitions_=0;
  // pcWrite/pcRestore identify the verified original paired-write instructions; the second
  // (HAL_AVD_COMB_Set3dCombSpeed, 80503828/8050382C) restores a low byte that is itself INVALID.
  struct CombRestore { uint32_t address; uint8_t bank, low; uint32_t unknown; bool written=false;
    uint32_t pcWrite=0x805f883c, pcRestore=0x805f887c; bool lowUnknown=false; };
  std::optional<CombRestore> combRestore_;
  uint64_t combRestoreCount_=0;
  RiuByte timingSnapshot_;
  bool timingSnapshotPending_ = false;
  unsigned miuSequence_[2]{};
  uint64_t miuReady_[2]{};
  bool miuArmed_[2]{};
  uint64_t bistReady_ = 0;
  uint32_t bistResult_ = 0;
  bool bistPending_ = false;
  uint64_t uartReady_ = 0;
  uint32_t get(uint32_t a, unsigned size) const;
  void put(uint32_t a, unsigned size, uint32_t v);
  uint32_t storageKey(uint32_t a) const;
  void updateOutputTiming(uint64_t tick);

public:
  bool candidateVdMcuProgram=false;
  bool candidateAvdMcuProgram=false;
  bool candidateAudioIdma=false;
  const AudioIdma& audioIdma(unsigned i)const{return audioIdma_[i];}
  bool exploratoryAvdMcuStartup=false,exploratoryAvdSuspendedBoundary=false;
  bool exploratoryAvdSharedRun=false,diagnosticAvdDeferredStartup=false;
  bool diagnosticAvdSymbolicCodeTables=false;
  bool exploratoryAvdHostResetEmpty=false;
  uint64_t avdHostRequestLatchResets=0;
  uint64_t avdDispatchDivider=32,avdInstructionBudget=1000000;
  const AvdMcuBus& avdMcuBus()const{return avdMcuBus_;}
  std::string avdSharedStateSha()const;
  const VdMcuCpu& avdMcuCpu()const{return avdMcuCpu_;}
  bool avdMcuSuspended()const{return avdMcuSuspended_;}
  const std::string& avdMcuBoundaryReason()const{return avdMcuBoundaryReason_;}
  bool exploratoryVdMcuStartup=false;
  bool deferVdSfr8eWrite=false;
  bool exploratoryVdSuspendedBoundary=false;
  bool deferVdResetKeyWrites=false;
  bool exploratoryVdSharedRun=false;
  uint64_t vdDispatchDivider=4,vdInstructionBudget=20000000;
  uint64_t vdMailboxReads()const{return vdMailboxReads_;}
  uint64_t vdMailboxWrites()const{return vdMailboxWrites_;}
  bool vdMcuSuspended()const{return vdMcuSuspended_;}
  const std::string& vdMcuBoundaryReason()const{return vdMcuBoundaryReason_;}
  const VdMcuProgram& vdMcuProgram()const{return vdMcuProgram_;}
  const AvdMcuProgram& avdMcuProgram()const{return avdMcuProgram_;}
  const VdMcuCpu& vdMcuCpu()const{return vdMcuCpu_;}
  bool candidateTsp=false;
  void configureExploratoryTspCpu();
  void configureExploratoryPmAcOn(){pmBoot_.acApplied();}
  void configureExploratoryPm51(std::span<const uint8_t> code,uint64_t divider=32,bool history=false,bool uart=false){
    if(code.size()!=65536 || !divider || divider>4096)
      throw std::runtime_error("PM CODE view/divider outside supported experiment");
    pmCode_=code;pm51_.divider=divider;pm51_.captureHistory=history;exploratoryPm51_=true;
    pm51_.configureCandidateUart(uart);
  }
  const Pm51& pm51()const{return pm51_;}
  void configureProbePmDeferredConfigurations(){
    if(pm51_.started())throw std::runtime_error("PM probe configuration changed after startup");
    pm51_.probeDeferredStartupConfigurations=true;
  }
  void configureExploratoryPmPowerGood(bool good){pmPowerGood_.input(good);}
  const PmPowerGood& pmPowerGood()const{return pmPowerGood_;}
  void configureExploratorySarFullscale(){exploratorySarFullscale_=true;sar_.input(0,3300);sar_.input(1,3300);}
  void configureExploratoryScartIdGrounded(){exploratoryScartIdGrounded_=true;sar_.input(2,0);sar_.input(3,0);}
  uint64_t scartIdSamples()const{return scartIdSamples_;}
  void configureExploratoryScartGrounded(){scart_.ground();}
  void configureExploratoryFetchRevisionByte(unsigned byte){
    if(byte>255)throw std::runtime_error("Fetch revision hypothesis byte must be 0..255");
    exploratoryFetchRevisionByte_=int(byte);
  }
  bool exploratoryCiDetectHigh=false;
  uint64_t ciDetectReads=0;
  void configureExploratoryCiDetectHigh(){
    exploratoryCiDetectHigh=true;bytes_[0x1f206891]={4,4};
  }
  void configureExploratoryPqSramIdle(){pqSram_.exploratoryIdle();}
  void configureExploratoryPqDisabledPorts(){
    auto &b=bytes_[0x81000000u+(0x23u<<10)+0x104];
    b.value&=uint8_t(~3u);b.known|=3;
  }
  const PqSram &pqSram()const{return pqSram_;}
  uint64_t scartReads()const{return scart_.reads();}
  void configureExploratoryAnalogDisconnected(bool disconnected=true){analogDisconnected_=disconnected;}
  // External absent-HSYNC experiment, NOT a reset/status-ready value. The
  // matched HAL and driver identify bit14; detector gating/latency unverified.
  void configureExploratoryAvdNoHsync(bool absent=true){exploratoryAvdNoHsync_=absent;}
  uint64_t avdHsyncReads()const{return avdHsyncReads_;}
  // EXPLORATORY no-sync hypothesis for the AVD event latch 11FC (host alias BF206BF8):
  // with no HSYNC/VSYNC the sync-derived event bits 0 and 2 (the only bits the
  // original AVD program consumes) stay clear; all other bits remain invalid.
  void configureExploratoryAvdNoSyncEvents(){exploratoryAvdNoSyncEvents_=true;avdMcuBus_.eventAckClearOnly=true;}
  // DIAGNOSTIC bracketing only (run both 0 and 1): defines AFEC 11DB bit 7, whose reset/producer is unknown.
  void configureDiagnosticAvd11dbBit7(unsigned bit){diagnosticAvd11dbBit7_=int(bit&1);}
  void configureDiagnosticAvdInvalidPublication(){avdMcuBus_.invalidDataPublication=true;}
  bool exploratoryAvdNoSyncEvents()const{return exploratoryAvdNoSyncEvents_;}
  void configureExploratoryVifRevision(unsigned byte){
    if(byte>255)throw std::runtime_error("VIF chip revision hypothesis must be a byte");
    exploratoryVifRevision_=int(byte);
  }
  uint64_t vifRevisionReads()const{return vifRevisionReads_;}
  void configureExploratoryVifZeroInput(uint64_t cycles){vifZeroInput_.configure(cycles);}
  const VifZeroInput& vifZeroInput()const{return vifZeroInput_;}
  uint64_t vifZeroInputReads()const{return vifZeroInputReads_;}
  void configureExploratoryScmiFormat(unsigned byte){
    if(byte>255)throw std::runtime_error("SCMI format fixture byte outside 0..255");
    // Original main/sub read/write targets 1204/1284; only low byte defined.
    // Reset value is an explicit hypothesis, not a silicon default or status.
    for(auto offset:{8u,0x108u})bytes_[0x81000000u+(0x12u<<10)+offset]={uint8_t(byte),255};
  }
  void configureExploratoryIrDmaReset(){exploratoryIrDmaReset_=true;}
  void configureExploratoryIrCapturePresent(){exploratoryIrCapturePresent_=true;}
  void configurePmIrFifo(bool coldEmpty=false){candidatePmIrFifo_=true;if(coldEmpty)pmIrFifo_.coldEmptyHypothesis();}
  void configurePmIrRptLatch(){if(!candidatePmIrFifo_)throw std::runtime_error("PM IR RPT latch hypothesis requires the PM IR FIFO");pmIrFifo_.enableRptLatchHypothesis();}
  const PmIrFifo& pmIrFifo()const{return pmIrFifo_;}
  uint64_t pmIrStatusReads()const{return pmIrStatusReads_;}
  bool receivePmIrDecoded(uint8_t key,bool repeat){
    if(!candidatePmIrFifo_)throw std::runtime_error("PM IR FIFO not enabled");
    if(!(knownBits(0x1f007b00,1)&1) || !(get(0x1f007b00,1)&1) ||
       (knownBits(0x1f007b45,1)&15)!=15 || (get(0x1f007b45,1)&15)!=15 ||
       (knownBits(0x1f007b4d,1)&0x30)!=0x30 || (get(0x1f007b4d,1)&0x30)!=0x30)
      throw std::runtime_error("PM IR packet while receiver/FIFO disabled or unknown");
    return pmIrFifo_.receive(key,repeat);
  }
  uint64_t irCaptureStatusReads()const{return irCaptureStatusReads_;}
  void captureIrRecord(std::span<const uint8_t> record){
    if(!exploratoryIrCapturePresent_ || !exploratoryIrDmaReset_)
      throw std::runtime_error("external IR capture requires explicit ring/activity experiments");
    irDma_.capture(record,dramWrite); // raw external record only; no host key/UI translation
  }
  uint64_t irDmaStarts()const{return irDma_.starts();}
  uint64_t irDmaCaptures()const{return irDma_.captures();}
  std::optional<unsigned> irDmaProducer()const{return irDma_.producer();}
  uint32_t irDmaAddress()const{return irDma_.address();}
  unsigned irDmaCapacity()const{return irDma_.capacity();}
  void configureExploratoryGopResetWindows(){exploratoryGopResetWindows_=true;}
  uint64_t gopResetPulses()const{return gopResetPulses_;}
  void configureOrTraceBudget(bool first,uint64_t budget,uint64_t divider){orFirstVisits_=first;orBudget_=budget;orDivider_=divider;}
  uint64_t orOriginalWords()const{return orOriginalWords_;}
  uint64_t orModifiedWords()const{return orModifiedWords_;}
  size_t orVisited()const{return orVisited_.size();}
  uint64_t sarConversions()const{return sar_.conversions();}
  std::string tspCoreStateSha()const;
  uint64_t tspCpuInstructions()const{return tspCpu_.instructions();}
  uint32_t tspCpuPc()const{return tspCpu_.pc();}
  bool tspStoredWordKnown(uint32_t address)const; // observer metadata, no read/counter/effect
  uint32_t tspStoredWordKnownMask(uint32_t address)const;
  uint64_t tspCpuReads()const{return tspCpuReads_;}
  uint64_t tspCpuWrites()const{return tspCpuWrites_;}
  bool tspCpuRunning()const{return tspCpu_.running();}
  void configureExploratoryTspResetLow() {tsp_.enableExploratoryResetLow();}
  bool tspQueueKnown() const {return tsp_.known();}
  bool tspResetHeld() const {return tsp_.held();}
  unsigned tspQueueCount() const {return tsp_.count();}
  uint64_t tspResetCount() const {return tsp_.resets();}
  uint64_t tspResetReleases() const {return tsp_.releases();}
  uint64_t tspSubmissions() const {return tsp_.submissions();}
  uint64_t tspIndirectReads() const {return tspIndirectReads_;}
  uint64_t tspIndirectWrites() const {return tspIndirectWrites_;}
  uint64_t tspDownloadRequests() const {return tspDownload_.requests();}
  bool tspDownloadPending() const {return tspDownload_.pending();}
  uint64_t tspDownloadCompletions() const {return tspDownload_.completions();}
  size_t tspDownloadBytes() const {return tspDownload_.bytes();}
  uint32_t tspDownloadSource() const {return tspDownloadSource_;}
  uint8_t tspQmemByte(size_t offset) const {return tspDownload_.byte(offset);}
  bool candidateGopPalette=false;
  uint64_t paletteCommits() const { return palette_.commits(); }
  unsigned paletteDefined(unsigned plane) const { return palette_.defined(plane); }
  std::optional<uint32_t> paletteColor(unsigned plane,unsigned index) const { return palette_.color(plane,index); }
  bool candidateGe=false;
  bool candidateGeRectFill=false;
  bool candidateGeBitBlt=false;
  uint64_t geBitBlts=0,geBitBltPixels=0;
  std::string geRegisterFile() const;
  bool geQueueIdle() const { return ge_.idle(); }
  std::string geReg0History() const;   // read-only dump of every known GE register (index=value)
  // Read-only GOP timeline (bounded): guest writes into the GOP aperture and active-latch events, anchored to the original PC.
  struct GopLogEntry { uint64_t tick; uint32_t pc; char kind; unsigned bank, offset, value, n; };
  std::vector<GopLogEntry> gopLog;
  // Last writes to GE register 00 (PC, value), kept only to explain refusals of fill/BitBlt requests.
  std::deque<std::pair<uint32_t,uint16_t>> geReg0Writes;
  uint64_t geRectangles=0,geRasterPixels=0;
  // EXPLORATORY (-1 off, 0 opaque-source, 1 alpha-bit): uniform-colour stretched BitBlt under a stated alpha hypothesis.
  int exploratoryGeUniformStretch=-1;
  bool exploratoryGeNearestStretch=false;   // EXPLORATORY: non-uniform stretched sources sampled nearest-neighbour
  uint64_t geNearestStretches=0;
  uint64_t geUniformStretches=0,geUniformStretchPixels=0;
  // EXPLORATORY: anti-aliased I2 glyph blit under the hypotheses H1..H5 documented in video/ge_i2_glyph.hpp.
  bool exploratoryGeI2Glyph=false;
  // EXPLORATORY: offscreen-to-GOP surface copy with blend coefficient ONE (see video/ge_blend_copy.hpp).
  bool exploratoryGeBlendCopy=false;
  uint16_t exploratoryGeFillIgnoredReg00=0;   // EXPLORATORY: reg00 bits assumed not to affect a solid fill
  // stored (guest-written) 16-bit RIU register pair at a UHC byte offset; 0xffff known mask required
  bool usbStoredKnown(uint32_t off) const { return knownBits(UsbHost::kBase+off*2,2)==0xffff && knownBits(UsbHost::kBase+(off+2)*2,2)==0xffff; }
  uint32_t usbStoredReg(uint32_t off) const { return get(UsbHost::kBase+off*2,2)|(get(UsbHost::kBase+(off+2)*2,2)<<16); }
  UsbHost usbHost;                     // not serialized (EXPLORATORY pendrive model)
  bool usbDeviceAttachedPort0=false;   // not serialized: re-applied from the options after a restore
  bool exploratoryGeLine=false;   // not serialized: re-applied from the options after a restore
  uint64_t geLines=0,geLinePixels=0;
  bool exploratoryGeGradientFill=false;   // EXPLORATORY: command 0x2030 gradient rectangle fill
  uint64_t geGradientFills=0;
  bool exploratoryGeI8Fill=false;   // EXPLORATORY: indexed (format 4) rectangle fill, index = low byte of colour
  uint64_t geI8Fills=0,geI8FillPixels=0;
  std::string copyRefusal_;
  bool exploratoryGeCopySnapshot=false;   // HYPOTHESIS B4: overlapping blend copies read all source rows before writing
  bool exploratoryGeHeight5Full=false;   // HYPOTHESIS B3': copies read/write every destination row despite register 6F = height-5
  uint64_t geBlendCopies=0,geBlendCopyPixels=0;
  std::vector<std::string> geBlendCopyLog;   // one JSON object per retired exploratory copy (bounded)
  uint64_t geI2Glyphs=0,geI2GlyphPixels=0;
  void configureExploratoryGeReset() {
    ge_.exploratoryReset();
    // Only original engine enable and virtual-queue selector bits. All other
    // controls remain invalid. This does not assert Titania4 silicon defaults.
    bytes_[0x1f205000]={0xa4,1};
    bytes_[0x1f205004]={0xa5,2};
  }
  uint64_t geSubmitted() const { return ge_.submitted(); }
  uint64_t geCompleted() const { return ge_.completed(); }
  uint64_t geTransferred() const { return ge_.transferred(); }
  bool candidateSemaphore=false;
  void configureExploratorySemaphoreIdle() { semaphore_.exploratoryIdle(); }
  uint64_t semaphoreAcquisitions() const { return semaphore_.acquisitions(); }
  uint64_t semaphoreReleases() const { return semaphore_.releases(); }
  uint64_t semaphoreContentions() const { return semaphore_.contentions(); }
  bool exploratoryCombRestore=false;
  // Explicit transaction experiment: no reset value is assigned. Unknown
  // high bits remain unknown; the temporary low byte is quarantined until
  // the original T4 workaround restores its saved, known low byte.
  bool beginCombRestore(uint32_t address,uint32_t unknown);
  // Second verified paired write whose low byte is INVALID (identity preserved by the restoring SB).
  bool beginCombRestoreInvalidLow(uint32_t address,uint32_t unknown,uint32_t pcWrite,uint32_t pcRestore);
  uint32_t preservedStoreKey(uint32_t address) const;
  uint64_t combRestoreCount() const { return combRestoreCount_; }
  bool combRestorePending() const { return combRestore_.has_value(); }
  bool exploratoryDlcTimeout=false;
  uint64_t exploratoryDisplayClock = 0;
  void configureExploratoryDisplayClock(uint64_t hz) { scaler_.clock(hz); exploratoryDisplayClock=hz; }
  std::optional<unsigned> scBank() const {
    auto it=bytes_.find(0x1f205e00);
    if(it==bytes_.end() || it->second.known!=255) return {};
    return it->second.value;
  }
  RiuByte scByte(unsigned bank, unsigned offset) const {
    if(bank>=256 || offset<4 || offset>=512) throw std::runtime_error("SC bank/register out of bounds");
    auto it=bytes_.find(0x81000000u+(bank<<10)+offset);
    return it==bytes_.end()?RiuByte{}:it->second;
  }
  std::optional<PanelTiming> panelTiming() const;
  uint64_t displayFrames(uint64_t tick) const { return scaler_.frames(tick); }
  bool exploratoryGopVsyncLatch=false;
  const GopUpdate& gopUpdate()const{return gopUpdate_;}
  size_t gopActiveBytes()const{return gopActive_.size();}
  RiuByte gopActiveByte(unsigned bank,unsigned offset)const;
  uint64_t outputSyncReads() const { return outputSyncReads_; }
  uint64_t outputSyncTransitions() const { return outputSyncTransitions_; }
  void configureExploratoryScBank(unsigned bank) {
    if (bank > 255) throw std::runtime_error("SC initial bank out of range");
    put(0x1f205e00, 1, bank);
  }
  void configureExploratoryGammaEnable(bool enable) {
    // Keep unrelated bank-10 control bits unknown; neither branch is claimed
    // to be the physical reset state. Run both configurations separately.
    bytes_[0x81000000u + (0x10u << 10) + 0x140] = {uint8_t(0xa4 | enable), 1};
  }
  void configureExploratorySubwindowEnable(bool enable) {
    // BK20_10 bit1 is subwindow enable in the matching MStar control path.
    // Its power-on state is unknown. This sets exactly one experimental bit.
    bytes_[0x81000000u+(0x20u<<10)+0x40]={uint8_t(0xa5|(enable?2:0)),2};
  }
  void configureExploratoryDlcPending(bool pending) {
    // Stock msDlcInitWithCurve checks a pre-existing capture-range update
    // request. Named reference msDlc_SetCaptureRange strobes this bit after
    // programming the range. Its reset value and bit-3 completion are unknown.
    bytes_[0x81000000u+(0x1au<<10)+0x10]={uint8_t(0xa1|(pending?4:0)),4};
  }
  void configureExploratoryCombBank(unsigned bank) {
    if(bank>255)throw std::runtime_error("COMB bank out of range");
    put(0x1f206c00,1,bank);
  }
  std::optional<unsigned> combBank() const {
    auto it=bytes_.find(0x1f206c00);
    if(it==bytes_.end() || it->second.known!=255)return {};
    return it->second.value;
  }
  // Banked GOP window access is corroborated by stock 8053A140/A210 and
  // Titania4 GFLIP. No reset selector, IRQ or write-ack value is invented.
  bool gopBankKnown() const;
  void configureExploratoryGopBank(unsigned bank) {
    if(bank>15)throw std::runtime_error("GOP initial bank out of range");
    bytes_[0x1f203ffc]={uint8_t(0xa0|bank),15}; // IRQ flags/ACK remain unknown
  }
  uint8_t gopBank() const;
  RiuByte gopByte(unsigned bank, unsigned offset) const;
  void saveTimingSnapshot() {
    auto it = bytes_.find(0x1f205e00);
    timingSnapshot_ = it == bytes_.end() ? RiuByte{} : it->second;
    timingSnapshotPending_ = true;
  }
  bool restoreTimingSnapshot() {
    if (!timingSnapshotPending_) return false;
    bytes_[0x1f205e00] = timingSnapshot_;
    timingSnapshotPending_ = false;
    return true;
  }
  void configureSpi(NorModel model, SpiUnknownInput input) { spi_.nor.model = model; spi_.nor.unknownInput = input; }
  bool spiDataUnknown(uint64_t tick) const { return spi_.dataReady(tick) && spi_.dataKnown() == 0; }
  unsigned undocumentedSpiSamples() const { return spi_.nor.undocumentedSamples; }
  void prepareRmw(uint32_t a, uint32_t definedBits) { pendingRmw_[a] = definedBits; }
  void preparePreservedWrite(uint32_t a,unsigned n,uint32_t unknown) {
    auto full=n==4?0xffffffffu:(1u<<(n*8))-1;
    pendingRmw_[a]=full&~unknown; preservedUnknownWrite_[a]=unknown;
  }
  void prepareMaskedRead(uint32_t a, uint32_t consumed) { consumedRead_[a] = consumed; }
  void prepareOpaqueRead(uint32_t a) { opaqueRead_=a; }
  uint32_t knownBits(uint32_t a, unsigned size) const;
  std::optional<bool> gopMiu1(unsigned bank)const{
    if(bank!=1 && bank!=4)return {};
    unsigned mask=bank==1?0x20:0x40;
    if(!(knownBits(0x1f2025e4,2)&mask))return {};
    return bool(get(0x1f2025e4,2)&mask);
  }
  uint32_t controlByteKey(uint32_t a) const;
  bool symbolicInit = false;
  bool candidateBdma = false;
  bool gpioI2c = false;
  bool candidateMiu = false;
  bool virtualRtc = false;
  bool candidateUtmi = false;
  bool candidateSpi = false;
  bool candidateChipId = false;
  bool candidateGamma = false;
  uint64_t gammaCommits() const { return gamma_.commits; }
  void resetMiuModel() {
    // Inferred from stock BIST0 preceding any selector write, followed by
    // explicit bit15=1 selection in BIST1. Only this bit is defined.
    bytes_[0x1f2025e1] = {0x25, 0x80};
  }
  void holdI2cSda(bool enabled) { gpio_.driveLow(0x1f203d05, 4, enabled); }
  void enableCandidateUart() {
    uartEnabled_ = true;
    put(0x1f201318, 1, 0);
  }
  std::string stopReason, uart;
  std::function<void(const std::string &, uint32_t, unsigned, uint32_t, const std::string &)> event;
  std::function<std::vector<uint8_t>(uint32_t, size_t)> flashRead;
  // Guest SPI NOR program/erase into the copy-on-write flash overlay (set by the machine).
  std::function<size_t(uint32_t, std::span<const uint8_t>)> flashProgram;
  std::function<void(uint32_t, uint32_t)> flashErase;
  const SpiIsp& spiIsp() const { return spi_; }
  void enableSpiNorWrite() { spi_.nor.writeSupport = true; }
  // HYPOTHESIS (opt-in): a powered USB PHY with no device attached sees SE0 on the host port, so the UTMI
  // line-state bits 7:6 of USBC+0x10 (D-/D+, per the cross-SoC Linux mstar_usbc.h) are 0. Applies only after the
  // guest itself powered the port-0 PHY (UTMI PWRCTRL 1F207500 bit 15 = 0). All other USBC bits stay invalid.
  void configureExploratoryUsbNoDevice() { exploratoryUsbNoDevice_ = true; }
  // Port 0: USBC 1F200E00 / UTMI 1F207500; port 1: USBC 1F200F00 / UTMI 1F207400 (original boot writes both).
  bool usbPhyPowered(unsigned port = 0) const {
    uint32_t pwr = port ? 0x1f207400u : 0x1f207500u;
    return (knownBits(pwr, 2) & 0x8000) && !(get(pwr, 2) & 0x8000);
  }
  // USBC+0x10 (UTMI signal) of a port whose line-state bits 7:6 the active USB hypothesis defines, or 0, and their value.
  // no-device hypothesis: a guest-powered PHY with no device sees SE0 (D-/D+ = 0).
  // bracketing diagnostic: a guest-POWERED-DOWN PHY (PWRCTRL bit 15 = 1 written by the guest) has no grounded line state;
  // run D+ = 0 and D+ = 1 and report only conclusions that hold for both.
  uint32_t usbLineStateRegister(uint32_t a, unsigned n, uint32_t* value = nullptr) const {
    for (unsigned port = 0; port < 2; ++port) {
      uint32_t reg = port ? 0x1f200f10u : 0x1f200e10u, pwr = port ? 0x1f207400u : 0x1f207500u;
      if (!(a <= reg && uint64_t(a) + n > reg)) continue;
      bool known = (knownBits(pwr, 2) & 0x8000) != 0;
      bool powered = known && !(get(pwr, 2) & 0x8000);
      if (usbDeviceAttachedPort0 && port == 0 && powered) { if (value) *value = 0x40u; return reg; }   // full-speed J state: D+ high
      if (exploratoryUsbNoDevice_ && powered) { if (value) *value = 0; return reg; }
      if (diagnosticUsbUnpoweredDplus_ >= 0 && known && !powered) { if (value) *value = diagnosticUsbUnpoweredDplus_ ? 0x40u : 0u; return reg; }
      return 0;
    }
    return 0;
  }
  // BRACKETING: VIF carrier-recovery lock status (0x1F224471 bit 0, reference CR_LOCK_STATUS). Bit 0 only; run 0 and 1.
  void configureDiagnosticVifCrLock(unsigned bit) { diagnosticVifCrLock_ = int(bit & 1); }
  // HYPOTHESIS: with no HSYNC the published AFEC HTotal bytes (11C7 -> BF206B8D low, 11C8 -> BF206B90 high) read a constant.
  void configureExploratoryAvdHtotal(unsigned value) { exploratoryAvdHtotal_ = int(value & 0xffff); }
  // HYPOTHESIS: the AFEC noise-magnitude byte (HAL_AVD_AFEC_GetNoiseMag, BF206A04) reads a constant; bracket it.
  void configureExploratoryAvdNoise(unsigned value) { exploratoryAvdNoise_ = int(value & 0xff); }
  // HYPOTHESIS (protocol decoded from the original routine 80503CE0, content unknown): the AFEC address pair 11A1:11A0 (BF206B41:BF206B40) latches a
  // register address; with address 0x6A3C and bit 7 of the low byte set, the data pair 119D:119E (BF206B39:BF206B3C, high:low) reads and writes the
  // content of that indirectly addressed 16-bit register instead of the plain bytes. The content is a bracketed constant until written.
  void configureExploratoryAfecIndirect(unsigned value) { exploratoryAfecIndirect_ = int(value & 0xffff); afecIndirectValue_ = uint16_t(value); }
  bool afecIndirectActive() const {
    // raw stored bytes only (knownBits/get consult this predicate, so they must not be used here)
    if (exploratoryAfecIndirect_ < 0) return false;
    auto lo = bytes_.find(storageKey(0x1f206b40)), hi = bytes_.find(storageKey(0x1f206b41));
    if (lo == bytes_.end() || hi == bytes_.end() || lo->second.known != 255 || hi->second.known != 255) return false;
    return hi->second.value == 0x6a && lo->second.value == (0x3c | 0x80);
  }
  uint64_t afecIndirectReads() const { return afecIndirectReads_; }
  uint64_t afecIndirectWrites() const { return afecIndirectWrites_; }
  uint16_t afecIndirectValue() const { return afecIndirectValue_; }
  // HYPOTHESIS: only the bits in `mask` of the byte at `address` read as `value`; every other bit keeps its provenance. Guest writes are rejected.
  // scBank >= 0 scopes an address inside the banked scaler aperture (BF205E04..) to that bank selector value (BF205E00).
  bool hypothesisApplies(const HypothesisBits& h) const {
    if (h.init || h.ack) return false;   // reset-value / mailbox-ack hypotheses use ordinary storage: seeded into storage once, then ordinary register behaviour
    if (h.scBank < 0) return true;
    auto it = bytes_.find(0x1f205e00);
    return it != bytes_.end() && it->second.known == 255 && it->second.value == unsigned(h.scBank);
  }
  void addHypothesisBits(uint32_t address, unsigned mask, unsigned value, int scBank = -1, bool rmw = false, bool init = false, bool ack = false) {
    if (!(address >= 0x1f000000u && address < 0x1f400000u) || !(mask & 255) || (value & ~mask & 255)) throw std::runtime_error("invalid bit hypothesis");
    for (auto& h : hypothesisBits_) if (h.address == address && h.scBank == scBank && (h.mask & mask)) throw std::runtime_error("duplicate bit hypothesis");   // disjoint masks of one byte may be bracketed separately
    if (scBank >= 0 && (address < 0x1f205e04u || address >= 0x1f206000u)) throw std::runtime_error("scaler bank scope needs an address inside BF205E04..");
    hypothesisBits_.push_back({address, uint8_t(mask), uint8_t(value), 0, scBank, rmw, init, ack, false});
    if (init && !(scBank >= 0 && address >= 0x1f205e04u && address < 0x1f206000u)) {   // EXPLORATORY reset value (bank-scoped scaler ones are seeded lazily at their first read): the MASK bits start known with VALUE; the guest's own writes replace them as for any register
      auto& b = bytes_[initKey(address, scBank)];
      b.value = uint8_t((b.value & ~mask) | value);
      b.known |= uint8_t(mask);
    }
  }
  const std::vector<HypothesisBits>& hypothesisBits() const { return hypothesisBits_; }
  // After a snapshot restore: reset-value (+init) hypotheses seed bytes whose masked bits are still unknown (a restored byte the guest already wrote is left alone).
  // Storage key for seeding a reset-value hypothesis; a scaler-bank-scoped one addresses its own bank without needing the (unknown) current selector.
  // Lazy seeding of a bank-scoped reset-value hypothesis: the first read of the byte while its bank is selected and the byte is still unknown.
  void seedLazyInit(uint32_t a, unsigned n) {
    auto sel = bytes_.find(0x1f205e00);
    if (sel == bytes_.end() || sel->second.known != 255) return;
    for (auto& h : hypothesisBits_) {
      if (!h.init || h.scBank != int(sel->second.value) || h.address < 0x1f205e04u || h.address >= 0x1f206000u) continue;
      if (!(a <= h.address && uint64_t(a) + n > h.address)) continue;
      auto& b = bytes_[initKey(h.address, h.scBank)];
      if ((b.known & h.mask) == h.mask) continue;
      b.value = uint8_t((b.value & ~h.mask) | h.value);
      b.known |= h.mask;
    }
  }
  uint32_t initKey(uint32_t address, int scBank) const {
    if (scBank >= 0 && address >= 0x1f205e04u && address < 0x1f206000u) return 0x81000000u + (uint32_t(scBank) << 10) + address - 0x1f205e00u;
    return storageKey(address);
  }
  void reapplyInitHypotheses() {
    for (auto& h : hypothesisBits_) {
      if (!h.init || (h.scBank >= 0 && h.address >= 0x1f205e04u && h.address < 0x1f206000u)) continue;
      auto& b = bytes_[initKey(h.address, h.scBank)];
      if ((b.known & h.mask) == h.mask) continue;
      b.value = uint8_t((b.value & ~h.mask) | h.value);
      b.known |= h.mask;
    }
  }
  uint64_t avdNoiseReads() const { return avdNoiseReads_; }
  uint64_t avdHtotalReads() const { return avdHtotalReads_; }
  const std::string& guardedStretchDetail() const { return guardedStretchDetail_; }
  uint64_t vifCrLockReads() const { return vifCrLockReads_; }
  void configureDiagnosticUsbUnpoweredDplus(unsigned dplus) { diagnosticUsbUnpoweredDplus_ = int(dplus & 1); }
  // DIAGNOSTIC: a stored register bit whose value is, by construction, the original unknown bit again.
  void forgetBits(uint32_t a, uint8_t mask) { bytes_[storageKey(a)].known &= uint8_t(~mask); }
  std::function<bool(uint32_t, size_t, uint16_t)> dramTest;
  std::function<std::vector<uint8_t>(uint32_t,size_t)> dramRead;
  std::function<void(uint32_t,std::span<const uint8_t>)> dramWrite;
  std::function<bool(uint32_t,size_t)> dramWritable;
  std::function<bool()> executionStopped;
  uint32_t read(uint32_t a, unsigned size, uint32_t pc, uint32_t mask, uint64_t tick);
  void write(uint32_t a, unsigned size, uint32_t value, uint32_t pc, uint32_t mask, uint64_t tick);
  static uint32_t crcMsb(const std::vector<uint8_t> &data, uint32_t polynomial, uint32_t seed);
};
} // namespace manta
