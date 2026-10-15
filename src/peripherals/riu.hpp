#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>
#include "peripherals/gpio_i2c.hpp"
#include "peripherals/rtc.hpp"
#include "peripherals/utmi.hpp"
#include "peripherals/spi_isp.hpp"
#include "peripherals/semaphore.hpp"
#include "video/gamma.hpp"
#include "video/scaler_timing.hpp"
#include "video/ge_queue.hpp"
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
  ScartGroundedInput scart_;
  bool analogDisconnected_=false;
  bool exploratoryAvdNoHsync_=false;
  uint64_t avdHsyncReads_=0;
  int exploratoryVifRevision_=-1;
  uint64_t vifRevisionReads_=0;
  PqSram pqSram_;
  IrDma irDma_;bool exploratoryIrDmaReset_=false;
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
  struct CombRestore { uint32_t address; uint8_t bank, low; uint32_t unknown; bool written=false; };
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
  void configureExploratoryVifRevision(unsigned byte){
    if(byte>255)throw std::runtime_error("VIF chip revision hypothesis must be a byte");
    exploratoryVifRevision_=int(byte);
  }
  uint64_t vifRevisionReads()const{return vifRevisionReads_;}
  void configureExploratoryScmiFormat(unsigned byte){
    if(byte>255)throw std::runtime_error("SCMI format fixture byte outside 0..255");
    // Original main/sub read/write targets 1204/1284; only low byte defined.
    // Reset value is an explicit hypothesis, not a silicon default or status.
    for(auto offset:{8u,0x108u})bytes_[0x81000000u+(0x12u<<10)+offset]={uint8_t(byte),255};
  }
  void configureExploratoryIrDmaReset(){exploratoryIrDmaReset_=true;}
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
  std::function<bool(uint32_t, size_t, uint16_t)> dramTest;
  std::function<std::vector<uint8_t>(uint32_t,size_t)> dramRead;
  std::function<void(uint32_t,std::span<const uint8_t>)> dramWrite;
  std::function<bool()> executionStopped;
  uint32_t read(uint32_t a, unsigned size, uint32_t pc, uint32_t mask, uint64_t tick);
  void write(uint32_t a, unsigned size, uint32_t value, uint32_t pc, uint32_t mask, uint64_t tick);
  static uint32_t crcMsb(const std::vector<uint8_t> &data, uint32_t polynomial, uint32_t seed);
};
} // namespace manta
