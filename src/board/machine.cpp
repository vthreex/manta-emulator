#include "board/machine.hpp"
#include "profile/sampler.hpp"
#include "board/mmio_read_mask.hpp"
#include "board/htotal_join.hpp"
#include "storage/sha256.hpp"
#include "video/gop.hpp"
#include "video/gop_export.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
namespace manta {
static void appendHex(std::string &out, uint64_t n) {   // "0x" + lowercase hex without padding (same text as the former ostream formatting)
  char buf[18];
  int i = 18;
  do { buf[--i] = "0123456789abcdef"[n & 15]; n >>= 4; } while (n);
  out += "0x";
  out.append(buf + i, size_t(18 - i));
}
static std::string jsonText(const std::string& in) {
  std::string out;
  for (unsigned char c : in) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c == 10) out += "\\n";
    else if (c < 32 || c > 126) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
    else out += char(c);
  }
  return out;
}
static std::string hex(uint64_t n) {
  std::string s;
  s.reserve(18);
  appendHex(s, n);
  return s;
}
Msd309Machine::Msd309Machine(Flash f, Options o) : flash_(std::move(f)), options_(o), virtualTime_(o.exploratoryCpi) {
  if((!o.opaqueReadPolicy.empty() && (!o.symbolicInit || !o.deferDiagnosticReads)) ||
     (o.opaqueReadPolicy.empty() && o.opaquePolicyWaitMs) || o.opaquePolicyWaitMs>300000)
    throw std::runtime_error("opaque observation policy requires guarded mode and at most300000ms debugger wait; unavailable in strict mode");
  if(o.exploratoryCpi!=1 && (!o.cp0Timer || !o.symbolicInit || !o.deferDiagnosticReads || o.stage2 || o.initPrefix))
    throw std::runtime_error("Uncalibrated CPI experiment requires guarded reset execution and CP0 timers; unavailable in strict/direct-boot modes");
  if(o.exploratoryGopVsyncLatch && (!o.symbolicInit || !o.deferDiagnosticReads || !o.exploratoryDisplayClock))
    throw std::runtime_error("GOP sync-latch hypothesis requires guarded execution and explicit output clock; no strict reset/ACK default");
  riu_.exploratoryGopVsyncLatch=o.exploratoryGopVsyncLatch;
  if (o.cp0Timer)
    cpu_.tickCount(0); // reject unavailable engine extension at launch
  riu_.symbolicInit = o.symbolicInit;
  riu_.candidateBdma = o.candidateBdma;
  riu_.gpioI2c = o.gpioI2c;
  riu_.candidateMiu = o.candidateMiu;
  riu_.virtualRtc = o.virtualRtc;
  riu_.candidateUtmi = o.candidateUtmi;
  riu_.candidateSpi = o.candidateSpi;
  riu_.candidateChipId = o.candidateChipId;
  riu_.candidateGamma = o.candidateGamma;
  riu_.candidateSemaphore=o.candidateSemaphore;
  riu_.candidateGe=o.candidateGe;
  if(o.candidateGeRectFill && !o.candidateGe)throw std::runtime_error("GE rectangle model requires candidate GE queues");
  riu_.candidateGeRectFill=o.candidateGeRectFill;
  if(o.candidateGeBitBlt && !o.candidateGe)throw std::runtime_error("GE BitBlt model requires candidate GE queues");
  if(o.diagnosticHtotalBranchJoin && (!o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("HTotal branch-join diagnostic requires guarded symbolic exploration");
  riu_.candidateGeBitBlt=o.candidateGeBitBlt;
  riu_.candidateGopPalette=o.candidateGopPalette;
  riu_.candidateTsp=o.candidateTsp;
  riu_.candidateVdMcuProgram=o.candidateVdMcuProgram;
  if(o.deferVdSfr8eWrite && !o.exploratoryVdMcuStartup)
    throw std::runtime_error("VD SFR write-effect deferral requires the explicit bounded startup experiment");
  riu_.deferVdSfr8eWrite=o.deferVdSfr8eWrite;
  riu_.candidateAvdMcuProgram=o.candidateAvdMcuProgram;
  riu_.candidateAudioIdma=o.candidateAudioIdma;
  if(o.explorePmNonresumeBranch && (!o.symbolicInit || !o.deferDiagnosticReads || !o.exploratoryPm51SpiStartup))
    throw std::runtime_error("PM symbolic branch requires guarded original execution and connected PM; not a power/reset model");
  if(o.exploratoryAudioNoAck && (!o.candidateAudioIdma || !o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("No-ACK predicate scenario requires original IDMA and guarded unknown provenance; no valid DSP response is supplied");
  if(o.exploratoryAvdMcuStartup && (!o.candidateAvdMcuProgram || !o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("AVD original startup experiment requires the original programming port and guarded exploration");
  if(o.exploratoryAvdSuspendedBoundary && !o.exploratoryAvdMcuStartup)
    throw std::runtime_error("AVD boundary suspension requires original auxiliary instruction execution");
  riu_.exploratoryAvdMcuStartup=o.exploratoryAvdMcuStartup;
  riu_.exploratoryAvdSuspendedBoundary=o.exploratoryAvdSuspendedBoundary;
  if(o.exploratoryAvdSharedRun && (!o.exploratoryAvdMcuStartup || !o.symbolicInit || !o.deferDiagnosticReads ||
      !o.avdDivider || o.avdDivider>4096 || !o.avdBudget || o.avdBudget>100000000))
    throw std::runtime_error("AVD shared execution requires guarded uploaded startup, explicit bounded scheduling and unknown-value guards");
  if(o.diagnosticAvdDeferredStartup && !o.exploratoryAvdSharedRun)
    throw std::runtime_error("AVD unresolved startup-write retention requires the separate shared-run diagnostic; no effects/reset values inferred");
  if(o.exploratoryAvdHostResetEmpty && (!o.exploratoryAvdSharedRun || !o.exploratoryAvdSuspendedBoundary))
    throw std::runtime_error("AVD request-latch reset hypothesis requires guarded original shared execution and explicit unresolved-CPU suspension; not verified reset behavior");
  riu_.exploratoryAvdSharedRun=o.exploratoryAvdSharedRun;
  if(o.diagnosticAvdSymbolicCodeTables && !o.exploratoryAvdSharedRun)
    throw std::runtime_error("AVD symbolic CODE data tracking requires guarded shared execution; no hardware selector is provided");
  riu_.diagnosticAvdSymbolicCodeTables=o.diagnosticAvdSymbolicCodeTables;
  riu_.diagnosticAvdDeferredStartup=o.diagnosticAvdDeferredStartup;
  riu_.exploratoryAvdHostResetEmpty=o.exploratoryAvdHostResetEmpty;
  riu_.avdDispatchDivider=o.avdDivider;riu_.avdInstructionBudget=o.avdBudget;
  if(o.deferVdResetKeyWrites && (!o.deferVdSfr8eWrite || !o.exploratoryVdMcuStartup))
    throw std::runtime_error("VD reset-key write deferral requires explicit SFR deferral and original startup experiment");
  riu_.deferVdResetKeyWrites=o.deferVdResetKeyWrites;
  if(o.exploratoryVdSharedRun && !o.exploratoryVdMcuStartup)
    throw std::runtime_error("Shared MCU experiment requires original program upload/startup");
  riu_.exploratoryVdSharedRun=o.exploratoryVdSharedRun;
  if(o.exploratoryVdSuspendedBoundary && (!o.exploratoryVdMcuStartup || !o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("Suspended VD boundary experiment requires original startup and guarded exploration");
  riu_.exploratoryVdSuspendedBoundary=o.exploratoryVdSuspendedBoundary;
  if(o.exploratoryVdMcuStartup){
    if(!o.candidateVdMcuProgram || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("VD MCU standard-reset startup experiment requires candidate program port and guarded exploration");
    riu_.exploratoryVdMcuStartup=true;
  }
  if(o.exploratoryFetchRevisionByte>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("Fetch revision hypothesis requires guarded exploration");
    riu_.configureExploratoryFetchRevisionByte(unsigned(o.exploratoryFetchRevisionByte));
  }
  if(o.exploratoryCiDetectHigh){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("CI detect fixture requires guarded exploration");
    riu_.configureExploratoryCiDetectHigh();
  }
  riu_.executionStopped=[this]{return stopped_;};
  if(o.exploratoryPqSramIdle){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("PQ SRAM idle fixture requires guarded exploration");
    riu_.configureExploratoryPqSramIdle();
  }
  if(o.exploratoryPqDisabledPorts){
    if(!o.exploratoryPqSramIdle)throw std::runtime_error("PQ disabled-port reset fixture requires explicit idle experiment");
    riu_.configureExploratoryPqDisabledPorts();
  }
  if(o.exploratoryAnalogDisconnected){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("Disconnected analog input fixture requires guarded exploration");
    riu_.configureExploratoryAnalogDisconnected();
  }
  if(o.exploratoryAvdNoHsync){
    if(!o.symbolicInit || !o.deferDiagnosticReads || !o.exploratoryAnalogDisconnected)
      throw std::runtime_error("AVD absent-HSYNC hypothesis requires guarded execution and explicit disconnected analog input");
    riu_.configureExploratoryAvdNoHsync();
  }
  if(o.exploratoryAvdNoSyncEvents){
    if(!o.exploratoryAvdNoHsync || !o.exploratoryAvdSharedRun)
      throw std::runtime_error("AVD no-sync event hypothesis requires the absent-HSYNC hypothesis and the shared AVD run");
    riu_.configureExploratoryAvdNoSyncEvents();
  }
  if(o.diagnosticRestoreByIdentityBranch && (!o.symbolicInit || !o.deferDiagnosticReads || !o.candidateSpiNorWrite))
    throw std::runtime_error("restore-by-identity branch diagnostic requires guarded exploration and the SPI NOR write model");
  if(o.exploratoryAfecIndirect>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("AFEC indirect-register hypothesis requires guarded exploration");
    riu_.configureExploratoryAfecIndirect(unsigned(o.exploratoryAfecIndirect));
  }
  for(const auto& h:o.bitHypotheses){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("bit hypotheses: requires guarded exploration");
    riu_.addHypothesisBits(h.address,h.mask,h.value,h.scBank,h.rmw,h.init,h.ack);
  }
  if(o.provenanceExtensions){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("provenance extensions require guarded exploration");
    preservedBits_.enableInsPropagation();
  }
  if(o.exploratoryAvdNoise>=0){
    if(!o.exploratoryAvdNoHsync || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("AVD noise constant hypothesis requires the absent-HSYNC hypothesis and guarded exploration");
    riu_.configureExploratoryAvdNoise(unsigned(o.exploratoryAvdNoise));
  }
  if(o.exploratoryAvdHtotal>=0){
    if(!o.exploratoryAvdNoHsync || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("AVD HTotal constant hypothesis requires the absent-HSYNC hypothesis and guarded exploration");
    riu_.configureExploratoryAvdHtotal(unsigned(o.exploratoryAvdHtotal));
  }
  if(!o.watchPcRegs.empty()){
    watchSet_.assign((0x400000+7)/8,0);
    for(auto pc:o.watchPcRegs){auto i=(pc>>2)&0x3fffff;watchSet_[i>>3]|=uint8_t(1u<<(i&7));}
  }
  if(o.exploratoryGeCopySnapshot){
    if(!o.exploratoryGeBlendCopy)throw std::runtime_error("copy-snapshot hypothesis requires an exploratory blend-copy model");
    riu_.exploratoryGeCopySnapshot=true;
  }
  if(o.exploratoryGeHeight5Full){
    if(!o.exploratoryGeBlendCopy && !o.exploratoryGeI2Glyph)throw std::runtime_error("height-5 full-copy hypothesis requires an exploratory blend-copy or I2 glyph model");
    riu_.exploratoryGeHeight5Full=true;
  }
  if(o.exploratoryGeI8Fill){
    if(!o.candidateGeRectFill)throw std::runtime_error("I8 fill hypothesis requires the candidate GE rectangle model");
    riu_.exploratoryGeI8Fill=true;
  }
  if(o.exploratoryCopro){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("coprocessor execution requires guarded exploration");
  }
  if(!o.exploratoryUsbPendrive.empty()){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("USB pendrive attach requires guarded exploration");
    riu_.usbDeviceAttachedPort0=true;
  }
  if(o.exploratoryGeLine){
    if(!o.candidateGeRectFill)throw std::runtime_error("GE line hypothesis requires the candidate GE rectangle model");
    riu_.exploratoryGeLine=true;
  }
  if(o.exploratoryGeGradientFill){
    if(!o.candidateGeRectFill)throw std::runtime_error("GE gradient hypothesis requires the candidate GE rectangle model");
    riu_.exploratoryGeGradientFill=true;
  }
  if(o.exploratoryGeFillIgnoredReg00){
    if(!o.candidateGeRectFill)throw std::runtime_error("GE reg00 fill hypothesis requires the candidate GE rectangle model");
    riu_.exploratoryGeFillIgnoredReg00=uint16_t(o.exploratoryGeFillIgnoredReg00);
  }
  if(o.exploratoryGeBlendCopy){
    if(!o.candidateGeBitBlt)throw std::runtime_error("blend-copy hypothesis requires the candidate GE BitBlt model");
    riu_.exploratoryGeBlendCopy=true;
  }
  if(o.exploratoryGeI2Glyph){
    if(!o.candidateGeBitBlt)throw std::runtime_error("I2 glyph hypothesis requires the candidate GE BitBlt model");
    riu_.exploratoryGeI2Glyph=true;
  }
  if(o.exploratoryGeNearestStretch){
    if(o.exploratoryGeUniformStretch!=0)throw std::runtime_error("nearest-stretch hypothesis requires --exploratory-ge-uniform-stretch opaque-source");
    riu_.exploratoryGeNearestStretch=true;
  }
  if(o.exploratoryGeUniformStretch>=0){
    if(!o.candidateGeBitBlt)throw std::runtime_error("uniform-stretch hypothesis requires the candidate GE BitBlt model");
    riu_.exploratoryGeUniformStretch=o.exploratoryGeUniformStretch;
  }
  if(o.diagnosticVifCrLock>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("VIF carrier-lock bracketing requires guarded exploration");
    riu_.configureDiagnosticVifCrLock(unsigned(o.diagnosticVifCrLock));
  }
  if(o.diagnosticUsbUnpoweredDplus>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("USB powered-down line-state bracketing requires guarded exploration");
    riu_.configureDiagnosticUsbUnpoweredDplus(unsigned(o.diagnosticUsbUnpoweredDplus));
  }
  if(o.exploratoryUsbNoDevice){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("USB no-device hypothesis requires guarded exploration");
    riu_.configureExploratoryUsbNoDevice();
  }
  if(o.candidateSpiNorWrite){
    if(!o.candidateSpi)throw std::runtime_error("SPI NOR write model requires the candidate SPI model");
    riu_.enableSpiNorWrite();
  }
  if(o.diagnosticAvdInvalidPublication){
    if(!o.exploratoryAvdSharedRun || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("AVD invalid-data publication diagnostic requires the guarded shared AVD run");
    riu_.configureDiagnosticAvdInvalidPublication();
  }
  if(o.diagnosticAvd11dbBit7>=0){
    if(!o.exploratoryAvdSharedRun || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("11DB bit 7 bracketing diagnostic requires the guarded shared AVD run");
    riu_.configureDiagnosticAvd11dbBit7(unsigned(o.diagnosticAvd11dbBit7));
  }
  if(o.exploratoryVifRevision>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("VIF chip revision hypothesis requires guarded exploration; actual board stepping unknown");
    riu_.configureExploratoryVifRevision(unsigned(o.exploratoryVifRevision));
  }
  if(o.exploratoryVifZeroInputSettlingCycles){
    if(!o.symbolicInit || !o.deferDiagnosticReads || !o.exploratoryAnalogDisconnected)
      throw std::runtime_error("VIF ideal zero-energy hypothesis requires guarded exploration and disconnected analog input; real antenna noise and gain convergence unverified");
    riu_.configureExploratoryVifZeroInput(o.exploratoryVifZeroInputSettlingCycles);
  }
  if(o.exploratoryScmiFormat>=0){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("SCMI format reset fixture requires guarded exploration");
    riu_.configureExploratoryScmiFormat(unsigned(o.exploratoryScmiFormat));
  }
  if(o.exploratoryScartGrounded){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("SCART grounded input experiment requires guarded exploration");
    riu_.configureExploratoryScartGrounded();
  }
  if(o.exploratoryGopResetWindows){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("GOP reset/window coupling experiment requires guarded exploration");
    riu_.configureExploratoryGopResetWindows();
  }
  if(o.exploratoryIrDmaReset){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("IR DMA reset/index fixture requires explicitly guarded exploration");
    riu_.configureExploratoryIrDmaReset();
  }
  if(o.exploratoryIrCapturePresent){
    if(!o.symbolicInit || !o.deferDiagnosticReads || !o.exploratoryIrDmaReset)
      throw std::runtime_error("IR capture activity hypothesis requires guarded execution and original DMA descriptor/reset experiment");
    riu_.configureExploratoryIrCapturePresent();
  }
  if(o.exploratoryPmIrResetEmpty && (!o.candidatePmIrFifo || !o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("PM IR cold-empty hypothesis requires candidate FIFO and guarded exploration; reset occupancy unverified");
  if(o.candidatePmIrFifo)riu_.configurePmIrFifo(o.exploratoryPmIrResetEmpty);
  if(o.exploratoryPmIrRptLatch)riu_.configurePmIrRptLatch();
  riu_.configureOrTraceBudget(o.orFirstVisits,o.orBudget,o.orDivider);
  if(o.exploratorySarFullscale){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("SAR input/reference fixture requires explicitly guarded exploration");
    riu_.configureExploratorySarFullscale();
  }
  if(o.exploratoryScartIdGrounded){
    if(!o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("Grounded SCART ID voltage hypothesis requires guarded exploration; actual board wiring unknown");
    riu_.configureExploratoryScartIdGrounded();
  }
  if(o.exploratoryPmAcOn){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("PM external AC-on boundary requires explicitly guarded exploration");
    riu_.configureExploratoryPmAcOn();
  }
  if(o.deferPlatformIdentity && (!o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("Platform identity deferral requires guarded symbolic execution");
  if(o.exploratoryTspCpu){
    if(!o.candidateTsp || !o.exploratoryTspResetLow || !o.deferDiagnosticReads)
      throw std::runtime_error("OpenRISC reset/mapping experiment requires guarded TSP reset experiment");
    riu_.configureExploratoryTspCpu();
  }
  if(!o.tspFirmwareDump.empty() && !o.candidateTsp)
    throw std::runtime_error("TSP firmware dump requires candidate TSP instrumentation");
  if(o.exploratoryTspResetLow) {
    if(!o.candidateTsp || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("TSP reset-polarity experiment requires candidate TSP and guarded execution");
    riu_.configureExploratoryTspResetLow();
  }
  if(o.exploratoryGeReset) {
    if(!o.candidateGe || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("GE reset experiment requires candidate GE and guarded execution");
    riu_.configureExploratoryGeReset();
  }
  if(o.exploratorySemaphoreIdle) {
    if(!o.candidateSemaphore || !o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("Semaphore idle experiment requires candidate hardware and guarded execution");
    riu_.configureExploratorySemaphoreIdle();
  }
  riu_.configureSpi(o.flashModel, o.spiUnknownInput);
  if(o.exploratoryCombRestore && (!o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("COMB paired-write experiment requires guarded execution");
  riu_.exploratoryCombRestore=o.exploratoryCombRestore;
  if(o.exploratoryGopBank>=0) {
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("GOP initial bank experiment requires guarded execution");
    riu_.configureExploratoryGopBank(unsigned(o.exploratoryGopBank));
  }
  if(o.exploratoryDlcTimeout && (!o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("DLC timeout experiment requires guarded execution");
  riu_.exploratoryDlcTimeout=o.exploratoryDlcTimeout;
  if(o.exploratoryCombBank>=0) {
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("COMB bank experiment requires guarded execution");
    riu_.configureExploratoryCombBank(unsigned(o.exploratoryCombBank));
  }
  if(o.exploratoryDisplayClock) {
    if(!o.symbolicInit || !o.deferDiagnosticReads)
      throw std::runtime_error("Exploratory display clock requires guarded diagnostic execution");
    riu_.configureExploratoryDisplayClock(o.exploratoryDisplayClock);
  }
  if(o.exploratorySubwindowEnable>=0) {
    if(!o.symbolicInit || !o.deferDiagnosticReads) throw std::runtime_error("Subwindow initial-state experiment requires guarded execution");
    riu_.configureExploratorySubwindowEnable(o.exploratorySubwindowEnable!=0);
  }
  if(o.exploratoryDlcPending>=0) {
    if(!o.symbolicInit || !o.deferDiagnosticReads) throw std::runtime_error("DLC request initial-state experiment requires guarded execution");
    riu_.configureExploratoryDlcPending(o.exploratoryDlcPending!=0);
  }
  if (o.exploratoryScBank >= 0) {
    if (!o.deferDiagnosticReads || !o.symbolicInit)
      throw std::runtime_error("Exploratory SC bank requires guarded diagnostic execution");
    riu_.configureExploratoryScBank(unsigned(o.exploratoryScBank));
  }
  if (o.exploratoryGammaEnable >= 0) {
    if (!o.deferDiagnosticReads || !o.symbolicInit || !o.candidateGamma)
      throw std::runtime_error("Gamma enable-state experiments require guarded execution and --candidate-gamma");
    riu_.configureExploratoryGammaEnable(o.exploratoryGammaEnable != 0);
  }
  if (o.spiUnknownInput != SpiUnknownInput::Stop && (!o.candidateSpi || !o.deferDiagnosticReads))
    throw std::runtime_error("SPI unknown-input experiments require --candidate-spi --defer-diagnostic-reads");
  if (o.candidateMiu)
    riu_.resetMiuModel();
  if (o.i2cSdaStuck) {
    if (!o.gpioI2c)
      throw std::runtime_error("I2C fault fixture requires --gpio-i2c");
    riu_.holdI2cSda(true);
  }
  riu_.dramTest = [this](uint32_t a, size_t n, uint16_t pattern) {
    std::vector<uint8_t> expected(65536), actual(65536);
    for (size_t i = 0; i < expected.size(); ++i)
      expected[i] = uint8_t(pattern >> (8 * (i & 1)));
    for (size_t i = 0; i < n; i += expected.size()) {
      auto size = std::min(expected.size(), n - i);
      cpu_.write(a + uint32_t(i), expected.data(), size);
      cpu_.read(a + uint32_t(i), actual.data(), size);
      if (!std::equal(expected.begin(), expected.begin() + size, actual.begin()))
        return false;
    }
    return true;
  };
  if (o.candidateUart)
    riu_.enableCandidateUart();
  riu_.flashRead = [this](uint32_t a, size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i)
      b[i] = flash_.read(a + uint32_t(i));
    return b;
  };
  // Guest SPI NOR program/erase: modifies only the in-memory copy-on-write overlay (the stock image and the
  // input file are never written) and refreshes the CPU-visible flash windows so later guest reads agree.
  auto mirrorFlash = [this](uint32_t a, size_t n) {
    std::vector<uint8_t> bytes(n);
    for (size_t i = 0; i < n; ++i) bytes[i] = flash_.read(a + uint32_t(i));
    if (a < 0x400000) cpu_.write(0x1fc00000 + a, bytes.data(), std::min<size_t>(n, 0x400000 - a));
    if (options_.candidateSpi) cpu_.write(0x14000000 + a, bytes.data(), n);
  };
  riu_.flashProgram = [this, mirrorFlash](uint32_t a, std::span<const uint8_t> bytes) {
    auto violations = flash_.programAnd(a, bytes);
    mirrorFlash(a, bytes.size());
    event("spi_nor_program", a, unsigned(bytes.size()), unsigned(violations),
          "guest SPI NOR page program into the copy-on-write overlay; bits can only be cleared; stock image untouched");
    return violations;
  };
  riu_.flashErase = [this, mirrorFlash](uint32_t a, uint32_t size) {
    flash_.eraseRange(a, size);
    mirrorFlash(a, size);
    event("spi_nor_erase", a, size, 0, "guest SPI NOR erase into the copy-on-write overlay; neighbouring sectors unaffected");
  };
  riu_.dramRead=[this](uint32_t address,size_t size) {
    if(!options_.candidateMiu || size==0 || size>0x4000 || uint64_t(address)+size>dramBacking_.size())
      throw std::runtime_error("TSP DMA guest DRAM source outside mapped bounds");
    for(size_t i=0;i<size;++i)
      if(preservedBits_.memoryUnknown(address+uint32_t(i)) || unknownObservations_.bytes().contains(address+uint32_t(i)))
        throw std::runtime_error("TSP DMA source contains invalid hardware-origin bits");
    std::vector<uint8_t> bytes(size);cpu_.read(address,bytes.data(),size);return bytes;
  };
  riu_.event = [this](EvText k, uint32_t a, unsigned n, uint32_t v, EvText d) { event(k, a, n, v, d); };
  riu_.dramWrite=[this](uint32_t address,std::span<const uint8_t> bytes) {
    if(!options_.candidateMiu || bytes.empty() || uint64_t(address)+bytes.size()>0x08000000 ||
       uint64_t(address)+bytes.size()>dramBacking_.size())
      throw std::runtime_error("BDMA MIU0 destination outside mapped bounds");
    cpu_.write(address,bytes.data(),bytes.size());
    preservedBits_.overwriteKnownMemory(address,bytes.size());
    unknownObservations_.overwriteKnownMemory(address,bytes.size());
  };
  riu_.dramWritable=[this](uint32_t address,size_t size){
    return options_.candidateMiu && size>0 && uint64_t(address)+size<=0x08000000 &&
      uint64_t(address)+size<=dramBacking_.size();
  };
  if(o.exploratoryCoproMipsIrq>=0){
    if(!o.exploratoryCopro || !o.cp0Timer)throw std::runtime_error("--exploratory-copro-mips-irq requires --exploratory-copro and --cp0-timer");
    const int hw=o.exploratoryCoproMipsIrq;
    if(!((hw>=32&&hw<64)||(hw>=96&&hw<128)))throw std::runtime_error("--exploratory-copro-mips-irq supports FIQ banks 32..63 and 96..127 only");
    riu_.fiqModel=true;
    riu_.fiqChanged=[this]{updateIrqLines();};
    copro_.onReply=[this,hw]{riu_.fiqRaise(unsigned(hw));};
  }
  riu_.zeroUnknownReads=o.exploratoryRiuZeroUnknown;
  riu_.writeLogFrom=o.diagnosticRiuWriteLogFrom;
  riu_.exploratoryGeYuvStretch=o.exploratoryGeYuvStretch;
  if(o.exploratoryCopro){
    copro_.zeroUnknownReads=o.exploratoryCoproZeroUnknown;
    copro_.mailRead=[this](uint32_t a,uint8_t& v){return riu_.coproPeekByte(a,v);};
    copro_.mailWrite=[this](uint32_t a,uint8_t v){riu_.coproPokeByte(a,v);};
    copro_.ramWritten=[this](uint32_t a,size_t n){preservedBits_.overwriteKnownMemory(a,n);unknownObservations_.overwriteKnownMemory(a,n);};
    copro_.event=[this](const std::string& k,uint32_t a,unsigned n,uint32_t v,const std::string& d){event(k,a,n,v,d);};
    riu_.coproNotify=[this](uint32_t a,unsigned n,uint32_t v){
      if(a<=0x1f201fe0u && uint64_t(a)+n>0x1f201fe0u){
        uint32_t ctl=(v>>(8*(0x1f201fe0u-a)))&255;
        if(ctl==3)startCopro(a);else copro_.hold(ctl);
      }else copro_.mipsWrote(a,n,v);};
  }
  if(!o.exploratoryUsbPendrive.empty()){
    std::ifstream image(o.exploratoryUsbPendrive,std::ios::binary);
    if(!image)throw std::runtime_error("cannot open USB pendrive image "+o.exploratoryUsbPendrive);
    riu_.usbHost.attach(std::vector<uint8_t>((std::istreambuf_iterator<char>(image)),std::istreambuf_iterator<char>()));
    auto& bus=riu_.usbHost.bus;
    bus.read=[this](uint32_t a,size_t n){
      a&=0x1fffffff;   // the driver programs KSEG0/KSEG1 aliases; the MIU decodes the low 29 address bits
      try{return riu_.dramRead(a,n);}catch(const std::exception& e){throw std::runtime_error(std::string("USB host DMA read of ")+hex(a)+" ("+std::to_string(n)+" bytes): "+e.what());}};
    bus.write=[this](uint32_t a,std::span<const uint8_t> b){riu_.dramWrite(a&0x1fffffff,b);};
    bus.event=[this](const std::string& k,uint32_t a,uint32_t n,const std::string& d){event(k,a,n,0,d);};
    bus.phyPowered=[this](unsigned port){return riu_.usbPhyPowered(port);};
    bus.storedKnown=[this](uint32_t off){return riu_.usbStoredKnown(off);};
    bus.storedReg=[this](uint32_t off){
      if(!riu_.usbStoredKnown(off))throw std::runtime_error("USB host register "+std::to_string(off)+" read by the controller model before the guest defined it");
      return riu_.usbStoredReg(off);};
  }
  if(!o.pm51History.empty() && !o.exploratoryPm51SpiStartup)
    throw std::runtime_error("PM history requires original PM SPI startup experiment");
  if((o.exploratoryPmPowerGood || o.candidatePm51Uart) && !o.exploratoryPm51SpiStartup)
    throw std::runtime_error("PM power-good input hypothesis requires original PM startup experiment");
  if(o.exploratoryPmPowerGood)riu_.configureExploratoryPmPowerGood(true);
  if(o.exploratoryPm51SpiStartup){
    if(!o.symbolicInit || !o.deferDiagnosticReads || o.stage2 || o.initPrefix)
      throw std::runtime_error("PM SPI startup requires guarded original cold boot; reset/clock wiring remains exploratory");
    riu_.configureExploratoryPm51(std::span<const uint8_t>(flash_.stock()).subspan(0x10000,65536),o.pm51Divider,!o.pm51History.empty(),o.candidatePm51Uart);
  }
  if (!o.trace.empty()) {
    trace_.open(o.trace);
    if (!trace_)
      throw std::runtime_error("Cannot open trace");
  }
  if (!o.replay.empty()) {
    replay_.open(o.replay);
    if (!replay_)
      throw std::runtime_error("Cannot open replay");
  }
  if (o.candidateMiu) {
    dramBacking_.resize(0x10000000);
    cpu_.mapShared(0, dramBacking_.size(), dramBacking_.data());
    cpu_.setBacking(dramBacking_.data(), dramBacking_.size());
    // Firmware MsOS_PA2Uncached (805CB940) translates logical MIU1
    // 08000000..0FFFFFFF to D0000000..D7FFFFFF. Guest MMU translates this
    // to bus 40000000..47FFFFFF. It aliases the existing MIU1 backing.
    cpu_.mapShared(0x40000000, 0x08000000, dramBacking_.data() + 0x08000000);
  } else cpu_.map(0, 0x08000000);
  cpu_.map(0x1fc00000, 0x400000, UC_PROT_READ | UC_PROT_EXEC);
  auto aperture = riu_.flashRead(0, 0x400000);
  cpu_.write(0x1fc00000, aperture.data(), aperture.size());
  if (o.candidateSpi) {
    // Stock 8053DAD0 reads B4000000 + flash offset. The equivalent MStar
    // driver names this XIU flash0; map exactly chip capacity, no metadata.
    cpu_.map(0x14000000, 0x800000, UC_PROT_READ);
    auto xiu = riu_.flashRead(0, 0x800000);
    cpu_.write(0x14000000, xiu.data(), xiu.size());
  }
  check(uc_mmio_map(cpu_.handle, 0x1f000000, 0x400000, mmioRead, this, mmioWrite, this));
  if (o.stage2)
    cpu_.write(0x01000000, flash_.stock().data() + 0x2c60, 0x5960);
  uc_hook h;
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_CODE, reinterpret_cast<void *>(codeHook), this, 1, 0));
  // Unicorn 2.1.4 memory-write hooks leak MIPS delay state when a store is
  // the delay slot of JR and its target is another branch. See engine
  // regression. MMIO remains fully observed through mmio_map; count DRAM stores
  // at dispatch. Generic read hooks have the same problem for loads in delay
  // slots. Use instruction observations for coverage/access counts; retain
  // invalid hooks.
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_MEM_UNMAPPED | UC_HOOK_MEM_PROT,
                    reinterpret_cast<void *>(invalidHook), this, 1, 0));
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_INTR, reinterpret_cast<void *>(interruptHook), this, 1,
                    0));
  buildSpecialPcs();
  importStats();
}
void Msd309Machine::stop(std::string why, uint32_t a) {
  if (stopped_)
    return;
  stopped_ = true;
  blockingPc_ = cpu_.reg(UC_MIPS_REG_PC);
  blockingAddress_ = a;
  reason_ = std::move(why);
  uc_emu_stop(cpu_.handle);
}
void Msd309Machine::formatEventLine(std::string &line, uint64_t tick, uint32_t pc, std::string_view k, uint32_t a, unsigned n, uint32_t v,
                                    uint32_t ra, uint32_t sp, std::string_view detail, bool cycles, uint64_t cycleCount) {
  line.reserve(160 + k.size() + detail.size());
  line += "{\"tick\":"; line += std::to_string(tick);
  line += ",\"pc\":\""; appendHex(line, pc);
  line += "\",\"kind\":\""; line += k;
  line += "\",\"address\":\""; appendHex(line, a);
  line += "\",\"width\":"; line += std::to_string(n);
  line += ",\"value\":\""; appendHex(line, v);
  line += "\",\"ra\":\""; appendHex(line, ra);
  line += "\",\"sp\":\""; appendHex(line, sp);
  line += "\",\"detail\":\""; line += detail; line += "\"";
  if (cycles) { line += ",\"virtual_cpu_cycles\":"; line += std::to_string(cycleCount); }
  line += '}';
}
// The guest wrote 3 to the reset-control byte 1F201FE0 (the original enable sequence; the meaning of the value is inferred from ordering only).
void Msd309Machine::updateIrqLines() {
  const uint32_t lines = riu_.fiqLine() ? 2u : 0u;   // FIQ -> Cause.IP3 (the guest's IP3 slot at 806F70B0 is the FIQ dispatcher path)
  if (lines == irqLinesApplied_) return;
  irqLinesApplied_ = lines;
  ++riu_.fiqLineChanges;
  cpu_.setIrqLines(lines);
  event("mips_irq_lines", lines, 4, lines, "EXPLORATORY: external interrupt line state toward the MIPS core (IP2=bit0, IP3=bit1)");
}
// Run-time wiring of the coprocessor model that is not snapshot state (used when it starts and after a snapshot restore).
void Msd309Machine::attachCopro() {
  const size_t ramSize = std::min<size_t>(dramBacking_.size(), 0x08000000);
  copro_.jpdEnabled = options_.exploratoryCoproJpd;
  copro_.jpd.dumpPath = options_.exploratoryCoproJpdDump;
  copro_.jpd.ram = dramBacking_.data();
  copro_.jpd.ramSize = ramSize;
  copro_.jpd.ramWritten = [this](uint32_t a, size_t n){ preservedBits_.overwriteKnownMemory(a, n); unknownObservations_.overwriteKnownMemory(a, n); };
  copro_.watchAddr = options_.exploratoryCoproWatch;
  copro_.captureAddr = options_.exploratoryCoproCapture;
  copro_.seqLo = options_.exploratoryCoproSeqLo;
  copro_.seqHi = options_.exploratoryCoproSeqHi;
  copro_.ram = dramBacking_.data();
  copro_.ramSize = ramSize;
}
void Msd309Machine::startCopro(uint32_t riuAddress) {
  const size_t ramSize = std::min<size_t>(dramBacking_.size(), 0x08000000);
  uint32_t first = 0;
  std::memcpy(&first, dramBacking_.data() + 0x100, 4);
  if (ramSize < 0x1000 || first == 0) {
    event("copro_start_refused", riuAddress, 1, 3, "reset-control write seen but no code at the OpenRISC reset vector 0x100 in DRAM; coprocessor not started");
    return;
  }
  if (!options_.exploratoryCoproDumpStart.empty()) {
    std::ofstream d(options_.exploratoryCoproDumpStart, std::ios::binary);
    d.write(reinterpret_cast<const char*>(dramBacking_.data()), std::streamsize(ramSize));
    std::ofstream m(options_.exploratoryCoproDumpStart + ".mail");
    for (uint32_t o = 0x6700; o < 0x6800; ++o) { uint8_t v = 0; if (riu_.coproPeekByte(0x1f200000u + o, v)) m << std::hex << o << " " << unsigned(v) << char(10); }
  }
  attachCopro();
  copro_.start(0x100);
  event("copro_started", riuAddress, 1, 3, "EXPLORATORY: original DRAM image executes on the OpenRISC core from the architectural reset vector 0x100; stepped 16384 instructions per 16384 MIPS dispatches");
}
void Msd309Machine::event(EvText kt, uint32_t a, unsigned n, uint32_t v, EvText dt) {
  ++sideEffectGen_;
  const std::string_view k = kt.view();
  std::string knowledgeDetail;
  if(options_.orRegisterLogChanges && k=="or1k_internal_read" && n==4){
    uint64_t key=(uint64_t(riu_.tspCpuPc())<<32)|a;
    auto mask=riu_.tspStoredWordKnownMask(a);
    auto sample=std::pair{v,mask};auto it=lastOrStoredRead_.find(key);
    if(it!=lastOrStoredRead_.end() && it->second==sample && !(options_.pauseMmio && options_.pauseMmio==a)){
      ++suppressedOrStoredReads_;return;
    }
    lastOrStoredRead_[key]=sample;
    knowledgeDetail=std::string(dt.view())+"; known-mask="+hex(mask)+"; identical value/mask repeats summarized; invalid bits stay invalid";
    dt=EvText(knowledgeDetail);
  }
  if(options_.orIdlePollChanges){
    if((k=="write" && a>=0x1f202b00 && a<=0x1f202b05) ||
       (k=="or1k_register_write" && a==0x210330))lastOrIdlePollPc_=UINT32_MAX;
    if(k=="or1k_register_read" && a==0x210330 && n==4){
      if(v==0 && riu_.knownBits(0x1f202b00,2)==0xffff && riu_.knownBits(0x1f202b04,2)==0xffff){
        auto pc=riu_.tspCpuPc();
        if(lastOrIdlePollPc_==pc && !(options_.pauseMmio && options_.pauseMmio==a)){
          ++suppressedOrIdlePolls_;return;
        }
        lastOrIdlePollPc_=pc;
      }else lastOrIdlePollPc_=UINT32_MAX;
    }
  }
  // Observation filter only: Riu has already performed the actual original
  // auxiliary CPU store. Mailboxes/MMIO/CPU execution and pause points stay on.
  if(options_.noOrQmemTrace && k=="or1k_qmem_write" &&
     !(options_.pauseMmio && options_.pauseMmio==a)){
    ++suppressedOrQmemTrace_;return;
  }
  const bool isRead = k == "read", isWrite = k == "write";
  const bool recorded = isRead || isWrite || k == "blocked_read" || k == "deferred_read";
  const bool needLine = trace_.is_open() || replay_.is_open();
  uint32_t pc = 0, ra = 0, sp = 0;
  if (recorded || needLine) { pc = cpu_.reg(UC_MIPS_REG_PC); ra = cpu_.reg(UC_MIPS_REG_RA); sp = cpu_.reg(UC_MIPS_REG_SP); }
  if ((isRead || isWrite) && a >= 0x1f203e00 && a < 0x1f204000)
    ++gopEvents_;
  if ((isRead || isWrite) && a >= 0x1f205000 && a < 0x1f205200)
    ++geEvents_;
  if ((k == "blocked_read" || k == "blocked_write") && a >= 0x1f205000 && a < 0x1f205200)
    ++geBlockedEvents_;
  if (recorded) {
    auto &r = mmioRing_[mmioPushes_ & 255];
    r.tick = dispatches_; r.pc = pc; r.address = a; r.width = n; r.value = v; r.ra = ra; r.sp = sp;
    r.kind.set(kt); r.detail.set(dt);
    ++mmioPushes_;
  }
  if (needLine) {
    std::string line;
    formatEventLine(line, dispatches_, pc, k, a, n, v, ra, sp, dt.view(), virtualTime_.cpi() != 1, virtualTime_.cycles(dispatches_));
    if (trace_.is_open()){
      trace_ << line << '\n';
      if(!trace_)stop("trace write failure; execution evidence truncated",a);
    }
    if (replay_.is_open()) {
      std::string expected;
      if (!std::getline(replay_, expected) || expected != line)
        stop("deterministic replay mismatch", a);
    }
  }
  if (isRead || isWrite || k == "blocked_read")
    ++mmio_;
  if (isWrite)
    ++writeEpoch_;
  if (isRead) {
    auto key = (uint64_t(pc) << 32) | a;
    auto &poll = polls_[key];
    if (poll.epoch != writeEpoch_ || poll.value != v)
      poll.count = 0;
    poll.epoch = writeEpoch_;
    poll.value = v;
    ++poll.count;
    maxPoll_ = std::max(maxPoll_, poll.count);
    if (poll.count >= 1024 && (poll.count & (poll.count - 1)) == 0)
      event("poll_warning", a, n, v,
            "unchanged peripheral reads without intervening guest MMIO write: " +
                std::to_string(poll.count));
  }
  if (options_.pauseMmio && a == options_.pauseMmio)
    stop("MMIO breakpoint", a);
}
bool Msd309Machine::observeBlock(Msd309Machine &m, uint64_t a, uint32_t w) {
  if (m.options_.diagnosticHtotalBranchJoin && a == HtotalBranchJoin::branch && !m.stopped_ &&
      (m.unknownObservations_.registerUnknown(7) || m.preservedBits_.mask(7)) &&
      !m.unknownObservations_.registerUnknown(5) && !m.preservedBits_.mask(5) &&
      m.applyHtotalBranchJoin())
    return true;
  if (m.options_.deferDiagnosticReads) {
    auto why = m.unknownObservations_.observe(
        uint32_t(a), w, [&](unsigned i) { return m.cpu_.reg(UC_MIPS_REG_0 + int(i)); });
    if (!why.empty()) {
      m.event("unknown_observation_use", uint32_t(a), 4, w, why);
      auto op = w >> 26;
      auto address = (m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 21) & 31)) + uint32_t(int32_t(int16_t(w)))) & 0x1fffffff;
      m.stop(why, op >= 32 && op <= 38 ? address : 0);
      return true;
    }
    if(a==0x80503828 && w==0xa4a46cbc && m.options_.exploratoryCombRestore) {
      // HAL_AVD_COMB_Set3dCombSpeed: SH duplicates the nibble, SB restores the original low byte.
      // Both instructions and the whole 0x58-byte function are verified against the original image.
      std::vector<uint8_t> code(0x58);
      m.cpu_.read(0x005037e0, code.data(), code.size());
      uint32_t next = 0;
      m.cpu_.read(0x0050382c, &next, 4);
      if (sha256(code) == "98d17c412845002a953b5e10e3c10f67670f739d504546c6f1f0c25d90404500" && next == 0xa0a66cbc) {
        auto target = (m.cpu_.reg(UC_MIPS_REG_5) + 0x6cbc) & 0x1fffffff;
        m.riu_.beginCombRestoreInvalidLow(target, m.preservedBits_.mask(4) & 0xffff, 0x80503828, 0x8050382c);
      }
    }
    if(a==0x805f883c && w==0xa4820000 && m.cpu_.reg(UC_MIPS_REG_31)==0x803c69f8) {
      auto target=m.cpu_.reg(UC_MIPS_REG_4)&0x1fffffff;
      m.riu_.beginCombRestore(target,m.preservedBits_.mask(2)&0xffff);
    }
    // DIAGNOSTIC restore-by-identity branch in the original SerFlash write routine: 8053CB7C..CB80 tests the saved
    // copy of ISP register BF0011C8 bit 2; both arms (8053CB94..CB9C clears it, 8053CBE8..CBF4 sets it) rewrite that bit
    // to exactly the value tested. The path consistent with the representative value is executed and the stored bit is
    // relabelled unknown with its original identity at the join 8053CBA4.
    bool restoreBranch = false;
    if (m.options_.diagnosticRestoreByIdentityBranch && a == 0x8053cb80 && w == 0x14400017) {
      std::vector<uint8_t> code(0x454);
      m.cpu_.read(0x0053c914, code.data(), code.size());
      restoreBranch = sha256(code) == "f196f19f68c656f8c079385b51e73ee725324ff4b6f2b012fa25c61bad11afad" &&
                      m.preservedBits_.exactFieldOrigin(2, m.riu_.preservedStoreKey(0x1f0011c8), 4);
      m.event(restoreBranch ? "diagnostic_restore_by_identity_branch" : "diagnostic_restore_by_identity_branch_refused", uint32_t(a), 4, w,
              "DIAGNOSTIC ONLY: both arms rewrite ISP 1C8 bit2 to the tested value; representative path executed, bit relabelled unknown at the join");
    }
    if (m.options_.diagnosticRestoreByIdentityBranch && a == 0x8053cba4 && m.restoreBranchSeen_) {
      m.riu_.forgetBits(0x1f0011c8, 4);
      m.restoreBranchSeen_ = false;
    }
    if (restoreBranch) m.restoreBranchSeen_ = true;
    const bool scenarioXor=m.options_.explorePmNonresumeBranch &&
      m.pmActivityScenario_.xorPredicate(uint32_t(a),w,m.cpu_.reg(UC_MIPS_REG_2),
        m.preservedBits_.exactFieldOrigin(2,0x1f001cd1,15));
    if(scenarioXor)m.preservedBits_.scenarioXorImmediate(2,8);
    auto preservedWhy=(scenarioXor||restoreBranch)?std::string{}:m.preservedBits_.observe(w,[&](unsigned r){return m.cpu_.reg(UC_MIPS_REG_0+int(r));},
      [&](uint32_t address){return m.riu_.preservedStoreKey(address);},
      [&](uint32_t address,unsigned width,uint32_t unknown){
        m.riu_.preparePreservedWrite(address,width,unknown);
        m.event("preserved_control_write",address,width,unknown,"original unknown bits return to the same banked byte/bit identities; no decision or new hardware consumer authorized");
      });
    if(!preservedWhy.empty()) {
      if(m.options_.explorePmNonresumeBranch && m.pmActivityScenario_.resolvePredicate(uint32_t(a),w,
         m.cpu_.reg(UC_MIPS_REG_2),m.preservedBits_.exactFieldOrigin(2,0x1f001cd1,15,8))){
        // Resolve only this comparison's result provenance. The engine still
        // executes the original SLTIU, whose concrete representative yields
        // zero consistently with the explicit !=8 assumption. No CPU write.
        m.preservedBits_.seed(2,4,0xffffffffu,[](unsigned){return 0u;});
        m.event("diagnostic_pm_nonresume_predicate",uint32_t(a),4,w,
          "SYMBOLIC PATH ASSUMPTION ONLY: this activity observation !=8; hardware byte remains invalid; not verified cold boot; no register/PC/instruction modified");
        preservedWhy.clear();
      }
      if(m.options_.exploratoryAudioNoAck &&
         m.audioNoAck_.constrain(uint32_t(a),w,m.cpu_.reg(UC_MIPS_REG_2),m.cpu_.reg(UC_MIPS_REG_18),
           m.preservedBits_.mask(18),m.preservedBits_.exactByteOrigin(2,a==0x804f7b10?0x1f225cb8:0x1f225ab4),m.cpu_.reg(UC_MIPS_REG_16))){
        m.event("exploratory_audio_no_ack_predicate",uint32_t(a),4,w,
          "HYPOTHESIS only: original comparison constrained ack!=E3; observed placeholder is INVALID, reply bits remain unknown; no register/PC/firmware modification");
        preservedWhy.clear();
      }
    }
    if(!preservedWhy.empty()) {
      auto op=w>>26;
      auto target=(m.cpu_.reg(UC_MIPS_REG_0+int((w>>21)&31))+uint32_t(int32_t(int16_t(w))))&0x1fffffff;
      m.stop(preservedWhy,op>=32 && op<=43?target:0);return true;
    }
  }
  if(m.options_.explorePmNonresumeBranch && m.pmActivityScenario_.observeBranch(uint32_t(a),w,
      m.cpu_.reg(UC_MIPS_REG_2),m.preservedBits_.mask(2)))
    m.event("diagnostic_pm_nonresume_branch",uint32_t(a),4,w,
      "original BEQ follows assumed non-resume predicate to 80469CB8; no hardware marker produced; diagnostic application path only");
  return false;
}
void Msd309Machine::codeHookFull(uc_engine *, uint64_t a, uint32_t, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  if (m.replayingTlb_) return;   // save-state TLB replay executes synthetic MIPS code: no board side effects
  if (m.dispatches_ >= m.hookLimit_) { m.budgetStop_ = true; uc_emu_stop(m.cpu_.handle); return; }
  ++m.sideEffectGen_;            // the full hook may emit events/probes: never part of a skippable loop
  m.prevPc_ = uint32_t(a);
  // Save state: capture BEFORE this instruction is dispatched so a resumed run re-enters exactly here.
  if (!m.options_.saveState.empty() && !m.snapshotWritten_ && m.dispatches_ >= m.options_.saveStateAt) {
    if (!m.snapshotFirstWanted_) m.snapshotFirstWanted_ = m.dispatches_ + 1;
    if (m.snapshotBoundaryOk(uint32_t(a))) {
      try { m.writeSnapshot(uint32_t(a)); }
      catch (const std::exception& e) { m.snapshotWritten_ = true; m.stop(std::string("save-state failed: ") + e.what()); return; }
      m.snapshotWritten_ = true;
      if (m.options_.saveStateStop) { m.stop("save-state written; run stopped"); return; }
    } else if (++m.snapshotDeferrals_ > m.options_.saveStateMaxDefer) {
      m.stop("save-state: no safe instruction boundary within the deferral limit");
      return;
    }
  }
  ++m.dispatches_;
  if(m.options_.exploratoryCopro && (m.dispatches_ & 0x3fff)==0)m.copro_.run(0x4000);
  if((!m.options_.irScript.empty() || m.options_.remoteQueue || !m.irWaiting_.empty()) && (m.dispatches_ & 0x3fff)==0)m.serviceVirtualRemote();
  if(!m.options_.historyPcs.empty() && a>=0x80000000u && a<0x81000000u)
    for(auto pc:m.options_.historyPcs)if(pc==a){m.publishPreview();break;}
  if(!m.watchSet_.empty() && a>=0x80000000u && a<0x81000000u && ((a>>2)&0x3fffff)<m.watchSet_.size()*8 && (m.watchSet_[((a>>2)&0x3fffff)>>3]>>(((a>>2)&0x3fffff)&7)&1)){
    // Observation only: no guest state is touched.
    auto& entry=m.watchHits_[uint32_t(a)];
    ++entry.first;
    Msd309Machine::WatchHit hit{};hit.dispatch=m.dispatches_;
    for(unsigned i=0;i<32;++i)hit.reg[i]=m.cpu_.reg(UC_MIPS_REG_0+int(i));
    m.cpu_.read(hit.reg[29]&0x1fffffff,hit.stack,sizeof hit.stack);
    if(entry.second.size()>=24)entry.second.erase(entry.second.begin()+6);
    entry.second.push_back(hit);
  }
  if(m.options_.previewStore && (m.dispatches_ & 0xfffff)==0)m.publishPreview();
  else if(!m.options_.gopFrameHistory.empty() && (m.dispatches_ & 0x3fff)==0)m.publishPreview();   // finer sampling for the history timeline
  if(m.options_.zuiFlowProbes && ZuiFlow::watches(uint32_t(a))){
    std::vector<uint8_t> initAndDispatch(0x1cc),queueCode(0x140);
    m.cpu_.read(0x004e0d48,initAndDispatch.data(),initAndDispatch.size());
    m.cpu_.read(0x0049f040,queueCode.data(),queueCode.size());
    bool signature=sha256(initAndDispatch)=="9da8c036aca630ca87967416a7c79bb6bf2a891d5f570de86be61fabaaba3f92" &&
      sha256(queueCode)=="4ce696096aded2ef3fbfea2ab738d4aa276251132213ce7ddd5aebeaa4aa47b1";
    auto read=[&](uint32_t address,unsigned width)->std::optional<uint32_t>{
      auto ramSize=m.dramBacking_.empty()?0x08000000u:uint32_t(m.dramBacking_.size());
      if(!width || width>4 || uint64_t(address)+width>ramSize)return {};
      for(unsigned i=0;i<width;++i)
        if(m.preservedBits_.memoryUnknown(address+i) || m.unknownObservations_.bytes().contains(address+i))return {};
      uint8_t bytes[4]{};m.cpu_.read(address,bytes,width);uint32_t value=0;
      for(unsigned i=0;i<width;++i)value|=uint32_t(bytes[i])<<(8*i);
      return value;
    };
    if(m.zuiFlow_.record(uint32_t(a),m.dispatches_,read,signature)){
      const auto& observation=m.zuiFlow_.snapshots.back();
      auto show=[](auto v){return v?std::to_string(*v):std::string("INVALID");};
      m.event("guest_zui_flow_observation",uint32_t(a),0,0,
        "READ-ONLY original code signatures verified; state="+show(observation.state)+
        "; active_osd="+show(observation.active)+"; queue_head="+show(observation.head)+
        "; queue_tail="+show(observation.tail)+"; raw_occupied_slots="+std::to_string(observation.occupiedSlots)+
        "; no guest writes, events or CPU control changes");
    }
  }
  if(m.options_.exploratoryVifZeroInputSettlingCycles && m.options_.osdProbes &&
      (a==0x8050962c || a==0x804f2998 || a==0x80509d54 || a==0x80509d68)){
    ++m.osdProbeEntries_[uint32_t(a)];
    m.event("guest_vif_control_flow",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_RA),
      "original VIF handler entry, measurement predicate, gain-limit path or caller return; no PC override");
  }
  if(m.options_.exploratoryPm51SpiStartup && (a==0x80404bec || a==0x8046a190 || a==0x80469e38))
    m.event("guest_pm_application_entry",uint32_t(a),0,m.riu_.knownBits(0x1f001cd1,1),
      "original control flow: activity reader/caller or ZUI call site; status validity reported without selecting a branch");
  if(m.options_.osdProbes && (a==0x804e0d48 || a==0x804e0dc4 || a==0x80536274 ||
      a==0x80522d5c || a==0x80520a84 || a==0x8040440c)){
    ++m.osdProbeEntries_[uint32_t(a)];
    m.event("guest_osd_routine_entry",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_4),
      "original routine entry; a1="+hex(m.cpu_.reg(UC_MIPS_REG_5))+", a2="+hex(m.cpu_.reg(UC_MIPS_REG_6))+
      ", a3="+hex(m.cpu_.reg(UC_MIPS_REG_7))+"; 804E0D48=ZUI state initialization, 804E0DC4=state dispatch, 80536274=GOP CreateWinImpl2, 80520A84=font callback renderer; no host invocation");
  }
  if(m.options_.exploratoryAudioNoAck && (a==0x804f7a60 || a==0x804f7a84 || a==0x804f7b58 || a==0x804f7b8c)){
    if(a==0x804f7a60 || a==0x804f7b58){++m.audioNoAck_.timeouts;
      m.event("guest_audio_ack_timeout",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_16),"original counter reached failure path after original retries/delays; no host timeout or acknowledgment");}
    else {++m.audioNoAck_.returns;
      m.event("guest_audio_ack_failure_return",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_2),"original failure path returns to its own caller; PC and return value not set by host");}
  }
  if(m.options_.exploratoryFetchRevisionByte>=0 && a==0x805f4b18){
    auto enabled=m.cpu_.reg(UC_MIPS_REG_2);
    if(enabled)++m.fetchPatchEnabled_;else ++m.fetchPatchDisabled_;
    m.event("guest_fetch_patch_decision",uint32_t(a),0,enabled,
      "original byte predicate returned; pre-patch fetch parameter s0="+hex(m.cpu_.reg(UC_MIPS_REG_16)));
  }
  if(m.options_.exploratoryFetchRevisionByte>=0 && a==0x805f4b44)
    m.event("guest_fetch_parameter",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_16),
      "original caller reached selected branch join; no PC change or parameter override");
  if (m.options_.cp0Timer) {
    uint32_t wt = 0;
    const uint32_t pp = uint32_t(a) & 0x1fffffff;
    if (uint64_t(pp) + 4 <= m.dramBacking_.size()) std::memcpy(&wt, m.dramBacking_.data() + pp, 4); else m.cpu_.read(pp, &wt, 4);
    m.tickInstr(wt);
  }
  if (m.options_.cp0Timer && a == 0x803b0180) {
    ++m.timerEntries_;
    m.event("exception_vector", uint32_t(a), 0, 0,
            "original guest exception vector entered; QEMU performs architectural exception entry");
  }
  if (a == 0x80539ce4) {
    m.gopInitializerEntered_ = true;
    ++m.gopInitializerEntries_;
    m.event("gop_initializer", uint32_t(a), 0, 0,
            "stock MDrv_GOP_Init entry identified by original diagnostic string and disassembly");
  }
  if(m.options_.graphicsProbes && (a==0x80538944 || a==0x8053a43c)){
    if(a==0x80538944)++m.gopWindowProgramEntries_;else ++m.gopWrite32Entries_;
    m.event(a==0x80538944?"guest_gop_window_program":"guest_gop_write32",uint32_t(a),4,m.cpu_.reg(UC_MIPS_REG_6),
      "original routine entry; raw a0="+hex(m.cpu_.reg(UC_MIPS_REG_4))+", a1="+hex(m.cpu_.reg(UC_MIPS_REG_5))+"; arguments alone do not prove enabled framebuffer or pixels");
  }
  if(m.options_.exploratoryPqSramIdle && (a==0x803c8250 || a==0x803c8424)){
    auto type=m.cpu_.reg(UC_MIPS_REG_4)&255,source=m.cpu_.reg(UC_MIPS_REG_5);
    auto size=a==0x803c8250?640u:320u,physical=PreservedBits::backingAddress(source);
    bool valid=!m.preservedBits_.mask(4) && !m.preservedBits_.mask(5) &&
      uint64_t(physical)+size<=m.dramBacking_.size();
    for(unsigned b=0;valid && b<size;++b)
      if(m.preservedBits_.memoryUnknown(physical+b) || m.unknownObservations_.bytes().contains(physical+b))valid=false;
    if(valid){
      std::vector<uint8_t> bytes(size);m.cpu_.read(physical,bytes.data(),size);
      m.event("guest_pq_table_upload",source,size,type,"original Y/C SRAM writer entry; immutable guest-source payload SHA256="+sha256(bytes));
    }
  }
  if(m.options_.candidateTsp && a==0x805d819c) {
    auto source=m.cpu_.reg(UC_MIPS_REG_4),size=m.cpu_.reg(UC_MIPS_REG_5);
    auto physical=PreservedBits::backingAddress(source);
    bool valid=!m.preservedBits_.mask(4) && !m.preservedBits_.mask(5) && size>0 && size<=0x200000 &&
      uint64_t(physical)+size<=m.dramBacking_.size();
    for(unsigned i=0;valid && i<size;++i)
      if(m.preservedBits_.memoryUnknown(physical+i) || m.unknownObservations_.bytes().contains(physical+i))valid=false;
    if(valid) {
      std::vector<uint8_t> image(size);m.cpu_.read(physical,image.data(),size);
      m.event("tsp_original_firmware_buffer",source,size,0,"original HAL CPU_SetBase arguments; RAM sha256="+sha256(image)+"; download unit/IRAM mapping and ISA not inferred from this capture");
      if(!m.options_.tspFirmwareDump.empty()) {
        std::ofstream out(m.options_.tspFirmwareDump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()),image.size());
        if(!out)throw std::runtime_error("cannot write private TSP firmware buffer dump");
      }
    } else m.event("tsp_original_firmware_buffer",source,size,0,"unverified/invalid source arguments; no host firmware bytes read or output");
  }
  if(m.options_.coproProbes && a==0x80545a40 && m.cpu_.reg(UC_MIPS_REG_RA)==0x803edf14){
    auto physical=PreservedBits::backingAddress(m.cpu_.reg(UC_MIPS_REG_4));
    constexpr unsigned size=1253416;
    bool valid=!m.preservedBits_.mask(4) && uint64_t(physical)+size<=m.dramBacking_.size();
    for(unsigned i=0;valid && i<size;++i)
      if(m.preservedBits_.memoryUnknown(physical+i) || m.unknownObservations_.bytes().contains(physical+i))valid=false;
    if(valid){
      std::vector<uint8_t> image(size);m.cpu_.read(physical,image.data(),size);auto digest=sha256(image);
      // S0 is already a destination-end pointer here, not the resource ID.
      // Recognize only the actual resource61 bytes, not an assumed ABI value.
      if(digest=="bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1"){
        ++m.coproLoadInspections_;m.coproLoadedSha_=digest;
        m.event("guest_copro_loader_output",physical,size,0,"original COPRO Init_End caller; matching resource61 guest RAM SHA256="+digest+
          "; processor entry/reset/bus mapping remain unverified; no acknowledgment supplied");
      }else m.event("guest_copro_other_loader_output",physical,size,0,"other/unidentified loader content; RAM SHA256="+digest+"; no resource61 match claimed");
    }else m.event("guest_copro_loader_output",physical,size,0,"invalid original loader output; no code/hash supplied");
  }
  if(a==0x805f8300) {
    ++m.scalerInitializerEntries_;
    m.event("guest_scaler_initializer",uint32_t(a),0,0,"original scaler initialization entry");
  }
  if(a==0x805f833c) {
    ++m.scalerInitializerReturns_;
    m.event("guest_scaler_init_return",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_2),"original return value: one successful, zero failed");
  }
  if(a==0x805e3e58) {
    ++m.dlcInitializerReturns_;
    m.event("guest_dlc_init_return",uint32_t(a),0,0,"original curve initializer return; capture completion is not implied");
  }
  if(m.options_.exploratoryDisplayClock && a==0x805f2518)
    m.event("guest_output_vsync_wait_return",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_17),
      "original wait returns remaining pulse count; zero means completed, nonzero means original timer timeout");
  // Repeated polling still executes every instruction and increments its hit
  // count. Only first visits need the ordered-set lookup/insertion.
  m.recordPc(uint32_t(a));
  if (a == 0x81000000)
    m.stage2Reached_ = true;
  if (a == 0x81000480 || a == 0x81004f08)
    m.decompressReached_ = true;
  if (a == 0x803b0600)
    m.runtimeReached_ = true;
  uint32_t w = 0;
  const uint32_t observedPhysical=uint32_t(a)&0x1fffffff;
  if(uint64_t(observedPhysical)+4<=m.dramBacking_.size()){
    // This is the same shared backing passed to uc_mem_map_ptr, not a code
    // cache. Observe every current guest byte, including self-modification.
    // The existing CPU still fetches/executes the instruction independently.
    const auto* bytes=m.dramBacking_.data()+observedPhysical;
    w=uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16)|(uint32_t(bytes[3])<<24);
  }else m.cpu_.read(observedPhysical,&w,4);
  if ((w >> 26) == 16 && ((w >> 21) & 31) == 4 &&
      (((w >> 11) & 31) <= 6 || ((w >> 11) & 31) == 10)) {
    if ((w & 7) == 0) m.cp0Shadow_[(w >> 11) & 31] = m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31));
    m.event("cp0_mmu_write", (w >> 11) & 31, 4, m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31)),
            "original guest MTC0 MMU register; select " + std::to_string(w & 7));
  }
  if (w == 0x42000002) m.tlbInstalls_.push_back({m.cp0Shadow_[0], m.cp0Shadow_[5], m.cp0Shadow_[10], m.cp0Shadow_[2], m.cp0Shadow_[3]});
  if (w == 0x42000006) m.tlbwrSeen_ = true;
  if (w == 0x42000002 || w == 0x42000006)
    m.event("guest_tlb_write", uint32_t(a), 4, w, "original architectural TLB entry installation");
  if (a == 0x8053dbe0) {
    m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_SP) + 0x14) & 0x1fffffff, m.spiJedec_.data(), 3);
    m.spiJedecRead_ = m.cpu_.reg(UC_MIPS_REG_2) == 1;
    m.event("spi_jedec_return", uint32_t(a), 3,
            (uint32_t(m.spiJedec_[0]) << 16) | (uint32_t(m.spiJedec_[1]) << 8) | m.spiJedec_[2],
            "unchanged guest JEDEC read returned; bytes cached in original stack buffer");
  }
  if (a == 0x8053dd14) {
    m.cpu_.read(m.cpu_.reg(UC_MIPS_REG_16) & 0x1fffffff, m.spiDescriptor_.data(), 40);
    m.spiDescriptorSelected_ = true;
    m.event("spi_descriptor_selected", m.cpu_.reg(UC_MIPS_REG_16), 40,
            m.spiDescriptor_[0] | (uint32_t(m.spiDescriptor_[1]) << 8),
            "original guest copied JEDEC-matching descriptor; CF variant check has not consumed its samples yet");
  }
  // Runtime history is bounded; retain thousands of instructions without a
  // 90-million-line trace. RA/SP are call context observations, not an unwind.
  if (a >= kFastBase && a < kFastEnd) m.pushRecent(uint32_t(a), w); else m.shadowValid_ = false;
  if (observeBlock(m, a, w)) return;
  auto op = w >> 26;
  if ((op >= 32 && op <= 38) || (op >= 40 && op <= 43) || (op == 46)) {
    auto rs = (w >> 21) & 31;
    auto address =
        (m.cpu_.reg(UC_MIPS_REG_0 + int(rs)) + uint32_t(int32_t(int16_t(w & 65535)))) & 0x1fffffff;
    if (op < 40 && ((address >= 0x1fc00000 && address < 0x20000000) ||
                    (m.options_.candidateSpi && address >= 0x14000000 && address < 0x14800000)))
      ++m.flashReads_;
    if (op >= 40 && address < 0x08000000)
      ++m.dramWrites_;
    if (!m.options_.watchData.empty())
      for (auto& [wa, wl] : m.options_.watchData)
        if (address < uint64_t(wa) + wl && uint64_t(address) + ((op==32||op==36||op==40)?1u:(op==33||op==37||op==41)?2u:4u) > wa) {
          auto& e = m.dataAccess_[(uint64_t(a) << 1) | (op >= 40 ? 1u : 0u)];
          auto value = m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31));
          if (!e.count) { e.firstDispatch = m.dispatches_; e.ra = m.cpu_.reg(UC_MIPS_REG_RA); e.firstValue = value; e.address = address; }
          e.lastValue = value; ++e.count;
          break;
        }
  }
  if (m.options_.traceInstructions)
    m.event("instruction", uint32_t(a), 4, w,
            "guest instruction dispatch; engine executes unchanged bytes");
  if (a == 0x81000000 || a == 0x81000480 || a == 0x81004f08 || a == 0x803b0600)
    m.event("checkpoint", uint32_t(a), 0, 0, "guest execution entered milestone address");
  if (a == 0x803b0600) {
    std::vector<uint8_t> b(3167072);
    m.cpu_.read(0x003b3980, b.data(), b.size());
    m.guestDecompressedSha_ = sha256(b);
    m.event("guest_inflate_hash", 0x003b3980, unsigned(b.size()), 0,
            "sha256=" + m.guestDecompressedSha_);
    if (m.guestDecompressedSha_ !=
        "149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236")
      m.stop("guest decompression hash mismatch");
  }
  if (m.preparingPrefix_ && a == 0xbfc01a74)
    m.stop("diagnostic prefix boundary before MIU ready poll");
  if (m.options_.breakpoint && a == m.options_.breakpoint)
    m.stop("PC breakpoint");
}
void Msd309Machine::recordPc(uint32_t a) {
  if (a >= kFastBase && a < kFastEnd) {
    auto &c = pcHitsFast_[(a - kFastBase) >> 2];
    if (++c == 1) coverage_.insert(a);   // idempotent: after a restore the canonical map may already hold this PC
  } else {
    auto &v = pcHits_[a];
    if (++v == 1) coverage_.insert(a);
  }
  histRing_[histPushes_ & 63] = a;
  ++histPushes_;
}
// Conservative: true when executing this word may change $29 (sp) or $31 (ra).
static inline bool mayWriteSpOrRa(uint32_t w) {
  unsigned op = w >> 26, rt = (w >> 16) & 31, rd = (w >> 11) & 31;
  return op == 3 || op == 1 || rt == 29 || rt == 31 || rd == 29 || rd == 31;
}
void Msd309Machine::pushRecent(uint32_t a, uint32_t w) {
  // RA/SP as seen before this instruction: re-read only after a host register write, a restore, or an instruction that may have changed them.
  if (!shadowValid_ || shadowGen_ != cpu_.writeGen || mayWriteSpOrRa(prevWord_)) {
    shadowRa_ = cpu_.reg(UC_MIPS_REG_RA);
    shadowSp_ = cpu_.reg(UC_MIPS_REG_SP);
    shadowValid_ = true;
    shadowGen_ = cpu_.writeGen;
  }
  recentRing_[recentPushes_ & 4095] = {dispatches_, a, w, shadowRa_, shadowSp_};
  ++recentPushes_;
  prevWord_ = w;
}
void Msd309Machine::flushStats() const {
  for (size_t i = 0; i < pcHitsFast_.size(); ++i)
    if (pcHitsFast_[i]) { pcHits_[kFastBase + uint32_t(i * 4)] += pcHitsFast_[i]; pcHitsFast_[i] = 0; }
  history_.clear();
  uint64_t n = std::min<uint64_t>(histPushes_, 64);
  for (uint64_t k = histPushes_ - n; k < histPushes_; ++k) history_.push_back(histRing_[k & 63]);
  recentInstructions_.clear();
  n = std::min<uint64_t>(recentPushes_, 4096);
  for (uint64_t k = recentPushes_ - n; k < recentPushes_; ++k) recentInstructions_.push_back(recentRing_[k & 4095]);
  recentMmio_ = recentMmioBase_;
  n = std::min<uint64_t>(mmioPushes_, 256);
  for (uint64_t k = mmioPushes_ - n; k < mmioPushes_; ++k) {
    const auto &r = mmioRing_[k & 255];
    std::string line;
    formatEventLine(line, r.tick, r.pc, r.kind.view(), r.address, r.width, r.value, r.ra, r.sp, r.detail.view(), virtualTime_.cpi() != 1, virtualTime_.cycles(r.tick));
    recentMmio_.push_back(std::move(line));
  }
  while (recentMmio_.size() > 256) recentMmio_.pop_front();
}
void Msd309Machine::importStats() {
  pcHitsFast_.assign((kFastEnd - kFastBase) / 4, 0);
  histPushes_ = 0;
  for (auto pc : history_) { histRing_[histPushes_ & 63] = pc; ++histPushes_; }
  recentRing_.resize(4096);
  mmioRing_.resize(256);
  recentMmioBase_ = recentMmio_;
  mmioPushes_ = 0;
  recentPushes_ = 0;
  for (auto &i : recentInstructions_) { recentRing_[recentPushes_ & 4095] = i; ++recentPushes_; }
  shadowValid_ = false;
}
void Msd309Machine::buildSpecialPcs() {
  specialPc_.assign(((kFastEnd - kFastBase) / 4 + 7) / 8, 0);
  auto mark = [&](uint32_t pc) {
    if (pc >= kFastBase && pc < kFastEnd && !(pc & 3)) { size_t i = (pc - kFastBase) >> 2; specialPc_[i >> 3] |= uint8_t(1u << (i & 7)); }
  };
  // Every instruction address the full hook compares against a constant (superset: option-guarded ones are marked regardless of options).
  static constexpr uint32_t fixed[] = {
      0x803b0180, 0x803b0600, 0x80539ce4, 0x805f8300, 0x805f833c, 0x805e3e58, 0x8053dbe0, 0x8053dd14,   // exception vector, milestones, GOP/scaler/DLC/SPI
      0x8050962c, 0x804f2998, 0x80509d54, 0x80509d68,                                                       // VIF control flow probes
      0x80404bec, 0x8046a190, 0x80469e38,                                                                   // PM application entries
      0x804e0d48, 0x804e0dc4, 0x80536274, 0x80522d5c, 0x80520a84, 0x8040440c,                               // OSD probes
      0x804f7a60, 0x804f7a84, 0x804f7b58, 0x804f7b8c,                                                       // audio ack probes
      0x805f4b18, 0x805f4b44, 0x80538944, 0x8053a43c, 0x803c8250, 0x803c8424, 0x805d819c, 0x80545a40, 0x805f2518,
      0x80503828, 0x805f883c, 0x8053cb80, 0x8053cba4,                                                       // comb / restore-by-identity diagnostics
      0x80404bfc, 0x80404c04, 0x8046a198,                                                                   // PM non-resume scenario
      HtotalBranchJoin::branch};
  for (auto pc : fixed) mark(pc);
  for (auto pc : ZuiFlow::watchAddresses) mark(pc);
  if (options_.breakpoint) mark(options_.breakpoint);
  for (size_t i = 0; i < watchSet_.size() * 8; ++i)   // --watch-pc-regs
    if (watchSet_[i >> 3] >> (i & 7) & 1) mark(0x80000000u + uint32_t(i) * 4);
  allSlow_ = options_.traceInstructions || !options_.watchData.empty() || !options_.historyPcs.empty() || dramBacking_.empty();
}
void Msd309Machine::resyncTicks() {
  const uint32_t cause = cpu_.reg(UC_MIPS_REG_CP0_CAUSE);
  tickOff_ = (cause >> 27) & 1;
  const uint32_t dist = cpu_.reg(UC_MIPS_REG_CP0_COMPARE) - cpu_.reg(UC_MIPS_REG_CP0_COUNT);
  tickRemaining_ = dist ? dist : (uint64_t(1) << 32);   // Compare == Count: no crossing until Count wraps all the way round
  tickSynced_ = true;
}
// The engine adds `delta` to Count before every instruction and raises the timer interrupt on the tick that reaches Compare. Here the ticks are
// accumulated and handed over in one call exactly when that crossing tick happens, before the guest reads/writes Count/Compare/Cause, and at the
// host's observation points (flushTicks), which gives the same Count at every point the guest or the host can see it.
void Msd309Machine::tickInstr(uint32_t w) {
  if (!tickSynced_) resyncTicks();
  const uint32_t delta = virtualTime_.countDelta(dispatches_);
  if (!tickOff_ && delta) {
    if (tickRemaining_ <= delta || pendingTicks_ + delta >= (uint64_t(1) << 30)) {
      const bool crossing = tickRemaining_ <= delta;
      cpu_.tickCount(uint32_t(pendingTicks_ + delta));
      pendingTicks_ = 0;
      if (crossing) tickSynced_ = false; else tickRemaining_ -= delta;
    } else {
      tickRemaining_ -= delta;
      pendingTicks_ += delta;
    }
  }
  const unsigned op = w >> 26;
  if (op == 16) {
    const unsigned rs = (w >> 21) & 31, rd = (w >> 11) & 31;
    if (rs == 0 && rd == 9) flushTicks();                                   // MFC0 Count
    else if (rs == 4 && (rd == 9 || rd == 11 || rd == 13)) { flushTicks(); tickSynced_ = false; }   // MTC0 Count/Compare/Cause
  } else if (op == 31 && (w & 63) == 59 && ((w >> 11) & 31) == 2) flushTicks();   // RDHWR CC
}
// Exact busy-wait skipping. A loop whose complete register state (GPRs, HI, LO) is identical at two consecutive visits of its head, with no store,
// device access, event, CP0/FP operation or special address in between, is a pure function of RAM it only reads: the next iteration is identical
// again until an interrupt or a host service point intervenes. Whole iterations are then accounted for without being executed: dispatch counter,
// CP0 Count, per-PC hit counts, PC history and recent-instruction ring are advanced exactly as if they had run. The skip never crosses the next
// timer-compare crossing, a multiple of 0x4000 dispatches (remote-key / frame-history service points) or the dispatch budget.
bool Msd309Machine::loopSkipVisit(uint32_t a) {
  auto &c = loopTab_[(a >> 2) & 255];
  const uint64_t d = dispatches_;
  if (c.pc == a) {
    uint64_t delta = d - c.lastDispatch;
    if (delta == c.period && delta <= 64) ++c.conf;
    else { c.period = delta <= 64 ? uint32_t(delta) : 0; c.conf = 0; c.haveRegs = false; }
  } else {
    c = LoopCand{};
    c.pc = a;
  }
  c.lastDispatch = d;
  if (c.period == 0 || c.conf < 1) return false;
  if (c.cooldown) { --c.cooldown; return false; }
  std::array<uint32_t, 34> regs;
  for (int i = 0; i < 32; ++i) regs[size_t(i)] = cpu_.reg(UC_MIPS_REG_0 + i);
  regs[32] = cpu_.reg(UC_MIPS_REG_HI);
  regs[33] = cpu_.reg(UC_MIPS_REG_LO);
  const bool same = c.haveRegs && c.gen == sideEffectGen_ && c.regs == regs;
  const uint64_t flashPerIteration = flashReads_ - c.flashReads;
  c.regs = regs; c.haveRegs = true; c.gen = sideEffectGen_; c.flashReads = flashReads_;
  if (!same) {
    c.fails = std::min<uint32_t>(c.fails + 1, 12);
    c.cooldown = std::min<uint32_t>(1u << c.fails, 4096);
    return false;
  }
  c.fails = 0;
  const uint32_t L = c.period;
  if (histPushes_ < L || recentPushes_ < L || hookLimit_ <= d + 1) return false;
  uint32_t seq[64];
  for (uint32_t j = 0; j < L; ++j) {
    seq[j] = histRing_[(histPushes_ - L + j) & 63];
    if (seq[j] < kFastBase || seq[j] >= kFastEnd) return false;
  }
  if (seq[0] != a) return false;
  // ---- how many whole iterations may be skipped ----
  uint64_t nMax = hookLimit_ - d - 1;
  nMax = std::min<uint64_t>(nMax, ((d | 0x3fff) + 1) - d - 1);
  const uint32_t cpi = virtualTime_.cpi();
  if (options_.cp0Timer) {
    flushTicks();
    const uint32_t cause = cpu_.reg(UC_MIPS_REG_CP0_CAUSE);
    if (!(cause & (1u << 27))) {   // Cause.DC clear: Count runs
      const uint32_t dist = cpu_.reg(UC_MIPS_REG_CP0_COMPARE) - cpu_.reg(UC_MIPS_REG_CP0_COUNT);
      if (dist) {
        const uint64_t allowedTicks = dist - 1;   // the crossing tick itself must happen in normal execution
        uint64_t bound;
        if (cpi == 1) bound = 2 * (allowedTicks + (d / 2)) + 1 - d;   // ticks of instructions d+1..d+n: floor((d+n)/2) - floor(d/2)
        else if (cpi / 2 == 0) bound = UINT64_MAX;
        else bound = allowedTicks / (cpi / 2);
        if (allowedTicks + (d / 2) < (d / 2)) bound = UINT64_MAX;   // overflow guard
        nMax = std::min<uint64_t>(nMax, bound);
      }
    }
  }
  const uint64_t K = nMax / L;
  if (K < 2) return false;
  const uint64_t n = K * L;
  // ---- apply: counters ----
  uint64_t ticks = 0;
  if (options_.cp0Timer) ticks = cpi == 1 ? (d + n) / 2 - d / 2 : n * (cpi / 2);
  // recent-instruction words / registers of the iteration just executed
  Instruction it[64];
  for (uint32_t j = 0; j < L; ++j) it[j] = recentRing_[(recentPushes_ - L + j) & 4095];
  const uint64_t H0 = histPushes_, R0 = recentPushes_;
  dispatches_ += n;
  if (ticks) { cpu_.tickCount(uint32_t(ticks)); tickSynced_ = false; }
  flashReads_ += K * flashPerIteration;
  for (uint32_t j = 0; j < L; ++j) pcHitsFast_[(seq[j] - kFastBase) >> 2] += K;
  for (uint64_t q = (n > 64 ? n - 64 : 0); q < n; ++q) histRing_[(H0 + q) & 63] = seq[q % L];
  for (uint64_t q = (n > 4096 ? n - 4096 : 0); q < n; ++q) {
    auto &e = recentRing_[(R0 + q) & 4095];
    e = it[q % L];
    e.tick = d + q + 1;
  }
  histPushes_ = H0 + n;
  recentPushes_ = R0 + n;
  shadowValid_ = false;
  skippedInstructions_ += n;
  // the visit bookkeeping now refers to the new dispatch count
  c.lastDispatch = dispatches_;
  return true;
}
// Instruction hook. Non-special instructions in the firmware image take a lean path that performs exactly the same observable bookkeeping as
// codeHookFull (counters, coverage, PC history, recent instructions, timer ticks, provenance guards) without the address-specific probes.
void Msd309Machine::codeHook(uc_engine *uc, uint64_t a, uint32_t sz, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  if (m.replayingTlb_) return;
  uint32_t idx = (uint32_t(a) - kFastBase) >> 2;
  if (m.allSlow_ || uint32_t(a) < kFastBase || uint32_t(a) >= kFastEnd || (m.specialPc_[idx >> 3] >> (idx & 7) & 1) ||
      (!m.options_.saveState.empty() && !m.snapshotWritten_)) {
    codeHookFull(uc, a, sz, p);
    return;
  }
  if (m.dispatches_ >= m.hookLimit_) { m.budgetStop_ = true; uc_emu_stop(m.cpu_.handle); return; }
  if (uint32_t(a) <= m.prevPc_ && !m.options_.noLoopSkip) m.loopSkipVisit(uint32_t(a));   // backward transfer: possible busy-wait loop head
  m.prevPc_ = uint32_t(a);
  ++m.dispatches_;
  if (m.options_.exploratoryCopro && (m.dispatches_ & 0x3fff) == 0) m.copro_.run(0x4000);
  if ((m.dispatches_ & 0x3fff) == 0 && (!m.options_.irScript.empty() || m.options_.remoteQueue || !m.irWaiting_.empty())) m.serviceVirtualRemote();
  if (m.options_.previewStore && (m.dispatches_ & 0xfffff) == 0) m.publishPreview();
  else if (!m.options_.gopFrameHistory.empty() && (m.dispatches_ & 0x3fff) == 0) m.publishPreview();
  m.recordPc(uint32_t(a));
  const uint32_t observedPhysical = uint32_t(a) & 0x1fffffff;
  const auto *bytes = m.dramBacking_.data() + observedPhysical;
  uint32_t w = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
  if (m.options_.cp0Timer) m.tickInstr(w);
  const unsigned op = w >> 26;
  if (op >= 40 || op == 16 || (op >= 17 && op <= 19) || op == 49 || op == 50 || op == 53 || op == 54 || (op == 0 && ((w & 63) == 12 || (w & 63) == 13)))
    ++m.sideEffectGen_;   // store / cache / CP0 / FP / syscall: an iteration containing one is not a pure loop
  if (op == 16) {   // COP0: MTC0 to MMU registers, TLBWI/TLBWR (same code as the full hook)
    if (((w >> 21) & 31) == 4 && (((w >> 11) & 31) <= 6 || ((w >> 11) & 31) == 10)) {
      if ((w & 7) == 0) m.cp0Shadow_[(w >> 11) & 31] = m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31));
      m.event("cp0_mmu_write", (w >> 11) & 31, 4, m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31)),
              "original guest MTC0 MMU register; select " + std::to_string(w & 7));
    }
    if (w == 0x42000002) m.tlbInstalls_.push_back({m.cp0Shadow_[0], m.cp0Shadow_[5], m.cp0Shadow_[10], m.cp0Shadow_[2], m.cp0Shadow_[3]});
    if (w == 0x42000006) m.tlbwrSeen_ = true;
    if (w == 0x42000002 || w == 0x42000006) m.event("guest_tlb_write", uint32_t(a), 4, w, "original architectural TLB entry installation");
  }
  m.pushRecent(uint32_t(a), w);
  const bool mem = (op >= 32 && op <= 38) || (op >= 40 && op <= 43) || op == 46;
  uint32_t address = 0, ea = 0;
  if (mem) {
    ea = m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 21) & 31)) + uint32_t(int32_t(int16_t(w & 65535)));
    address = ea & 0x1fffffff;
  }
  if (m.options_.deferDiagnosticReads) {
    // Provenance guards: nothing to observe when no register holds an unknown value and the access cannot touch an unknown RAM byte.
    bool quiet = !m.unknownObservations_.anyRegisterUnknown() && m.preservedBits_.quiet() &&
                 (mem ? (m.unknownObservations_.memoryClean(ea, 4) && m.preservedBits_.memoryClean(ea, 4))
                      : ((op < 32 && op != 31) || op == 47 || op == 51));
    if (!quiet && observeBlock(m, a, w)) return;
  }
  if (mem) {
    if (op < 40 && ((address >= 0x1fc00000 && address < 0x20000000) || (m.options_.candidateSpi && address >= 0x14000000 && address < 0x14800000)))
      ++m.flashReads_;
    if (op >= 40 && address < 0x08000000) ++m.dramWrites_;
  }
}
void Msd309Machine::memoryHook(uc_engine *, uc_mem_type type, uint64_t a, int, int64_t, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  if (type == UC_MEM_READ && a >= 0x1fc00000 && a < 0x20000000)
    ++m.flashReads_;
  if (type == UC_MEM_WRITE && a < 0x08000000)
    ++m.dramWrites_;
}
bool Msd309Machine::invalidHook(uc_engine *, uc_mem_type t, uint64_t a, int n, int64_t v, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  m.event("memory_fault", uint32_t(a), unsigned(n), uint32_t(v),
          "engine memory fault type " + std::to_string(t));
  m.stop("unmapped/protected guest access", uint32_t(a));
  return false;
}
void Msd309Machine::interruptHook(uc_engine *, uint32_t n, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  auto last = m.lastHistoryPc();
  uint32_t words[4]{};
  m.cpu_.read(last & 0x1fffffff, words, 16);
  m.event("cpu_exception", last, 0, n,
          "engine exception boundary; instruction=" + hex(words[0]) + " next=" + hex(words[1]) +
              " v0=" + hex(m.cpu_.reg(UC_MIPS_REG_2)) + " ra=" + hex(m.cpu_.reg(UC_MIPS_REG_RA)) +
              " status=" + hex(m.cpu_.reg(UC_MIPS_REG_CP0_STATUS)));
  m.stop("CPU exception " + std::to_string(n), last);
}
uint64_t Msd309Machine::mmioRead(uc_engine *, uint64_t off, unsigned n, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  try {
    auto a = 0x1f000000 + uint32_t(off);
    auto pc = m.cpu_.reg(UC_MIPS_REG_PC);
    if(m.options_.explorePmNonresumeBranch && pc==0x80404bf4 && a==0x1f001cd1 && n==1){
      std::vector<uint8_t> helper(28);m.cpu_.read(0x00404bec,helper.data(),helper.size());
      if(sha256(helper)=="4405618c6483bb6fc54e12c7132b4713b570cea7f827f6452f0220338bef708c" &&
         m.pmActivityScenario_.arm(pc,a,n,m.cpu_.reg(UC_MIPS_REG_RA),m.riu_.knownBits(a,n))){
        m.preservedBits_.seed(2,1,0,[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("diagnostic_pm_activity_invalid_sample",a,n,0,
          "INVALID PM activity observation; one explicitly selected symbolic non-resume predicate may be explored; no register/reset value supplied");
      }
    }
    // Stock boot switches between cached and uncached ROM aliases. The RMW
    // proof address is an instruction identity; traces retain the real PC.
    if ((pc & 0x1fffffff) >= 0x1fc00000 && (pc & 0x1fffffff) < 0x1fc02c60)
      pc = (pc & 0x1fffffff) | 0xa0000000;
    if (m.options_.symbolicInit) {
      auto defined = m.proveImmediateRmw(pc, a, n);
      if (defined)
        m.riu_.prepareRmw(a, defined);
      else if (m.rmwIdentity_ && m.options_.provenanceExtensions)
        m.riu_.prepareRmw(a, 0);   // identity write-back of an unknown register byte: nothing becomes known
    }
    if (m.options_.symbolicInit) {
      uint32_t words[8]{};
      m.cpu_.read(pc & 0x1fffffff, words, sizeof(words));
      if(auto consumed=immediateReadMask(words))m.riu_.prepareMaskedRead(a,*consumed);
    }
    if(m.options_.exploratoryAvdHostResetEmpty && pc==0x805032a0 && a==0x1f206bb8 && n==1){
      std::vector<uint8_t> helper(256);m.cpu_.read(0x0050328c,helper.data(),helper.size());
      if(sha256(helper)!="926aa44601025abeca5733f2e3aed1b7437db476b6cff0e63e2609163ca1dfc5")
        throw std::runtime_error("AVD original host-service identity mismatch");
      // The byte is copied before ANDI 0C, so the immediate-mask shortcut is
      // inapplicable. Retain every unknown field in the normal bit tracker;
      // pending requests still cannot consume unknown bank/payload fields.
      m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
      m.riu_.prepareOpaqueRead(a);
      m.event("avd_host_command_partial_sample",a,n,m.riu_.knownBits(a,n),
        "byte-verified original LBU/copies/field tests; invalid bank/high bits remain guarded; no branch outcome or byte supplied");
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x8054cd88 &&
       n==1 && a>=0x1f204a00 && a<0x1f204c00){
      uint32_t actual[15]{};m.cpu_.read(0x0054cd88,actual,sizeof(actual));
      if(discardedAdcControlSample(actual)){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_adc_discarded_sample",a,n,0,
          "INVALID unused first LBU in byte-verified ADC update helper; original BEQ delay slot discards sample; second RMW remains independently guarded; read side effects unverified");
      }
    }
    if(m.options_.exploratoryDisplayClock && pc==0x805fbc8c && a==0x1f205e78 && n==1) {
      constexpr uint32_t expected[]={0x90440000,0x8ce39634,0x3c020020,0x34425e00,
        0x00621821,0x308400ff,0xa0650000,0x03e00008,0x30820001};
      uint32_t actual[9]{}; m.cpu_.read(pc&0x1fffffff,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual)))
        m.riu_.prepareMaskedRead(a,1); // full straight-line original reader; only bit 0 returned
    }
    if(m.options_.symbolicInit && pc==0x804f130c && a==0x1f001cd0 && n==1) {
      // Original PM bit-test helper 804F1304: LBU / ANDI 0xFF / AND a0 / JR / SLTU; only the bits selected by a0 reach the result.
      constexpr uint32_t expected[]={0x90a31cd0,0x306300ff,0x00831024,0x03e00008,0x0002102b};
      uint32_t actual[5]{}; m.cpu_.read(pc&0x1fffffff,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual)) && (m.cpu_.reg(UC_MIPS_REG_4)&0xff))
        m.riu_.prepareMaskedRead(a,m.cpu_.reg(UC_MIPS_REG_4)&0xff);
    }
    if(m.options_.deferDiagnosticReads && m.options_.symbolicInit && n==2 && m.riu_.scBank()==2 &&
       ((pc==0x805fe1b4 && a==0x1f205e10) || (pc==0x805fe1b8 && a==0x1f205e14))) {
      constexpr uint32_t expected[]={0x94850000,0x94620000,0x8ee40010,0x3c038073,0x30a5ffff,
        0x00021400,0x8c689840,0x3c03bfff,0x00451025,0x3473ffff,0x3c124000,0x00521024,
        0x00932024,0x0044a025};
      uint32_t actual[14]{};m.cpu_.read(0x005fe1b4,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
        auto rt=pc==0x805fe1b4?5:2;
        m.preservedBits_.seed(rt,n,m.riu_.knownBits(a,n),[&](unsigned byte){return m.riu_.controlByteKey(a+byte);});
        m.riu_.prepareOpaqueRead(a); // one-shot observation; provenance independently prevents use
        m.event("preserved_control_sample",a,n,m.riu_.knownBits(a,n),"INVALID banked scaler sample; stock operation retains only pre-scaling bit 30 and programs new coefficients");
      }
    }
    if(m.options_.deferDiagnosticReads && m.options_.symbolicInit && n==2 && m.riu_.scBank()==0xf &&
       ((pc==0x805fe144 && a==0x1f205e1c) || (pc==0x805fe0bc && a==0x1f205e20))) {
      uint32_t path[3]{};m.cpu_.read(pc&0x1fffffff,path,sizeof(path));
      if(path[0]==0x94a20000 && path[1]==(pc==0x805fe144?0x0817f811u:0x0817f814u) &&
         path[2]==(pc==0x805fe144?0x3046ffffu:0x3047ffffu)) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned byte){return m.riu_.controlByteKey(a+byte);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_scaler_geometry",a,n,0,"INVALID original geometry observation; first comparison/decision stops unless original feature gate makes value unused");
      }
    }
      if(m.options_.exploratoryDlcTimeout && m.options_.deferDiagnosticReads && m.options_.symbolicInit &&
         n==1 && m.riu_.scBank()==0x1a &&
         ((pc==0x805e3ddc && a==0x1f205e1d) || (pc==0x805e3df4 && a==0x1f205e1c))) {
        constexpr uint32_t expected[]={0x90430000,0x3c06806c,0x34a25e1c,0x306300ff,0xa4c395f4,
          0x00e21021,0x90440000,0x24020028,0xa3c20054,0x3c02806c,0xa04095f6,0x8fa20010,
          0x00031a00,0x34a55e00,0x308400ff,0x00641821,0x00e52821,0xa3c00052,0xa3c00053,
          0xa4c395f4,0xa0a20000};
        uint32_t actual[21]{};m.cpu_.read(0x005e3ddc,actual,sizeof(actual));
        if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
          m.preservedBits_.seed(pc==0x805e3ddc?3:4,n,m.riu_.knownBits(a,n),[&](unsigned byte){return m.riu_.controlByteKey(a+byte);});
          m.riu_.prepareOpaqueRead(a);
          m.event("deferred_dlc_statistic",a,n,0,"INVALID byte-pair statistic cache; disjoint concatenation/copies allowed, first arithmetic or decision stops");
        }
      }
    if (m.options_.symbolicInit && m.options_.deferDiagnosticReads && a == 0x1f205e00 && n == 1 &&
        m.riu_.knownBits(a, n) != 255 &&
        (pc == 0x805e5b54 || pc == 0x805e5b58 || pc == 0x805e5e28 || pc == 0x805e5e2c)) {
      bool second = pc >= 0x805e5e28;
      uint32_t first[3]{}, maskWord=0, storeWord=0;
      m.cpu_.read(second ? 0x005e5e28 : 0x005e5b54, first, sizeof(first));
      m.cpu_.read(second ? 0x005e5e4c : 0x005e5bfc, &maskWord, 4);
      m.cpu_.read(second ? 0x005e5eb4 : 0x005e5d44, &storeWord, 4);
      if (first[0]==(second ? 0x90665e00u : 0x906a5e00u) && first[1]==0x90625e00 &&
          first[2]==(second ? 0x24020024u : 0x24020010u) &&
          maskWord==(second ? 0x30c600ffu : 0x314a00ffu) && storeWord==(second ? 0xa0465e00u : 0xa06a5e00u)) {
        if (pc == 0x805e5b54 || pc == 0x805e5e28) {
          m.riu_.saveTimingSnapshot(); m.unknownObservations_.seedTimingSnapshot(second ? 6 : 10);
        }
        else m.unknownObservations_.seed(2); // immediately overwritten by known ADDIU
        m.event("opaque_timing_snapshot", a, n, 0xa5,
                "INVALID saved control byte; exact original restore only, no branch/output may consume it");
        return 0xa5;
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x805e0d14 &&
       a==0x1f205e50 && n==2 && m.riu_.scBank()==0x1a) {
      constexpr uint32_t expected[]={0x94a20000,0x306300ff,0xa0830000,0x03e00008,0x7c423a00};
      uint32_t actual[5]{};m.cpu_.read(pc&0x1fffffff,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_dlc_gain",a,n,0,"INVALID gain snapshot; original extract/copies/restore permitted, first unknown decision stops");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x805f87dc &&
       a==0x1f203ffc && n==1 && m.cpu_.reg(UC_MIPS_REG_31)==0x803c1e80) {
      constexpr uint32_t expected[]={0x8e440000,0xafa20010,0x92450005,0x90820000,0x90830001};
      uint32_t actual[5]{};m.cpu_.read(0x003c1e80,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_selector_snapshot",a,n,0,"INVALID stock PQ save/restore; first unknown bank selection/decision still stops");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x805f87dc &&
       a==0x1f203ffc && n==1 && m.cpu_.reg(UC_MIPS_REG_31)==0x803c1f60) {
      constexpr uint32_t leaf[]={0x90620000,0x03e00008,0x304200ff};
      constexpr uint32_t caller[]={0x3042000f,0x3c040010,0x02202821,0x34841ffe,0x1051ffd8,0x2406000f};
      uint32_t actualLeaf[3]{},actualCaller[6]{};
      m.cpu_.read(0x005f87dc,actualLeaf,sizeof(actualLeaf));
      m.cpu_.read(0x003c1f60,actualCaller,sizeof(actualCaller));
      if(std::equal(std::begin(leaf),std::end(leaf),std::begin(actualLeaf)) &&
         std::equal(std::begin(caller),std::end(caller),std::begin(actualCaller))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_selector_mask",a,n,0,"INVALID upper IRQ flags discarded by original caller ANDI; unknown low bank still rejects BEQ");
      }
    }
    // The stock decoder initializer reads an old configuration byte into
    // caller-clobbered t0, immediately overwrites the device byte, and never
    // uses the observation. Keep it invalid rather than inventing a reset.
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x803e3768 &&
       a==0x1f206a85 && n==1 && m.riu_.knownBits(a,n)!=255){
      std::vector<uint8_t> code(0x27c);m.cpu_.read(0x003e364c,code.data(),code.size());
      if(sha256(code)=="fa745ee810a0ae7f71667c399e1ecf2705d938c45e60db5fb75180c3575c3524"){
        m.preservedBits_.seed(8,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_decoder_unused_sample",a,n,0,
          "INVALID old configuration read; original next store replaces byte; no reset value assigned; any use of unknown observation remains guarded");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x80503b48 &&
       a==0x1f206ba8 && n==1 && m.cpu_.reg(UC_MIPS_REG_31)==0x80502eac && m.riu_.knownBits(a,n)!=255){
      std::vector<uint8_t> code(0x3c+0x2c);m.cpu_.read(0x00503b1c,code.data(),0x3c);
      m.cpu_.read(0x00502ea4,code.data()+0x3c,0x2c);
      if(sha256(code)=="bd5fcd1f4716e621b979bbdd7da019efdf561a1d90e89c4af83b3e6bada613c0"){
        m.preservedBits_.seed(3,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_avd_config_cache",a,n,0,
          "INVALID original register 35D4 cache at RAM 006D83BE; complete reader/caller verified; no reset value assigned, first unknown decision or hardware use remains guarded");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x804fa410 &&
       a==0x1f225800 && n==1 && m.riu_.knownBits(a,n)!=255){
      std::vector<uint8_t> code(0x60);m.cpu_.read(0x004fa3ec,code.data(),code.size());
      if(sha256(code)=="b8b2ae3431a5a8f62435bbedcdef9a4ae336d32aed3b4a2246c7eb9e46f4a880"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_dsp_reset_unused",a,n,0,
          "INVALID old control read discarded by next original ADDIU; stock code writes 3 then 0 with its own delays; no reset default or auxiliary ready response assigned");
      }
    }
    auto geCaller=m.cpu_.reg(UC_MIPS_REG_31);
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 && pc==0x804f90e8 &&
       a==0x1f225c40 && geCaller==0x804fde6c){
      std::vector<uint8_t> reader(0x50),caller(0x10);
      m.cpu_.read(0x004f90a4,reader.data(),reader.size());m.cpu_.read(0x004fde64,caller.data(),caller.size());
      if(sha256(reader)=="cc073460e4eef6be5c6f268663fae6157263ce835705baa1eaefc80016b84198" &&
         sha256(caller)=="006651808aa77069d06051288ffde71a79a654ccd97c38d372c9f85d33dfe07f"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_sif_standard_command",a,n,m.riu_.knownBits(a,n),
          "INVALID original command 87 / 2D20 read; caller masks to byte, retained bits may return only to same command register; unknown decisions or other device uses still stop");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 && a==0x1f225a30 &&
       (pc==0x804f90c4 || pc==0x804f9108)){
      const bool se=pc==0x804f9108;
      std::vector<uint8_t> code(se?0x40:0x50);
      m.cpu_.read(se?0x004f90f4:0x004f90a4,code.data(),code.size());
      if(sha256(code)==(se?"3bec6d9e4f85425d758f3b7b7759b12f517d1ee81bb053feac8cc6965246aeec":
          "cc073460e4eef6be5c6f268663fae6157263ce835705baa1eaefc80016b84198")){
        m.preservedBits_.seed(se?2:6,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_audio_word_selector",a,n,m.riu_.knownBits(a,n),
          "INVALID selector fields preserved by SHA-verified original halfword getter; only bit0 replaced, same-origin store required; subsequent data read remains independently guarded");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x804f9050 &&
       a==0x1f225dd8 && n==1 && geCaller==0x804fc1bc){
      uint32_t parentReturn=0;
      m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_SP)+0x14)&0x1fffffff,&parentReturn,4);
      std::vector<uint8_t> reader(0x58),wrapper(0xbc);
      m.cpu_.read(0x004f9004,reader.data(),reader.size());m.cpu_.read(0x004fc1ac,wrapper.data(),wrapper.size());
      uint32_t outer[6]{};m.cpu_.read(0x004fe23c,outer,sizeof(outer));
      constexpr uint32_t originalOuter[]={0x0c13f06b,0,0x304200ff,0x0c13f073,0x00502025,0x0813f78b};
      bool outerPreservation=parentReturn==0x804fe244 &&
        std::equal(std::begin(outer),std::end(outer),std::begin(originalOuter));
      if((parentReturn==0x804fc1f8 || parentReturn==0x804fc224 || parentReturn==0x804fc24c || outerPreservation) &&
         sha256(reader)=="1ca38efb1835870f93e7dfa62d4e7ad5548439c4fe882c6decc70b0d4c34dcfc" &&
         sha256(wrapper)=="437415d9fdd5d141394d289f9def13ac3903c6b83630dcabbafb2f2d3b0e9556"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_audio_mode_preservation",a,n,m.riu_.knownBits(a,n),
          "INVALID old 2DEC mode bits; SHA-verified original setter preserves same-byte bits only; decisions and copies to another device remain guarded");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==1 &&
       ((pc==0x804fb0d0 && a==0x1f225b4c) || (pc==0x804fb108 && a==0x1f225a5c))){
      std::vector<uint8_t> routine(0x684);m.cpu_.read(0x004fb05c,routine.data(),routine.size());
      if(sha256(routine)=="911ac037ce784d2e1bd4185863872f560773bd17500584dc5da37230b99c72b4"){
        m.preservedBits_.seed(pc==0x804fb0d0?7:3,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_sif_control_snapshot",a,n,0,
          "INVALID original temporary control snapshot; ANDI retains bit7/bit0 only; original804FB4D0/4E0 restores identical byte/bit via WriteMask2; no DSP readiness inferred");
      }
    }
    if(m.options_.exploratoryAudioNoAck && pc==0x804f9098 && a==0x1f225ab4 && n==1){
      std::vector<uint8_t> reader(0x48),caller(0xb4);
      m.cpu_.read(0x004f905c,reader.data(),reader.size());m.cpu_.read(0x004f79d8,caller.data(),caller.size());
      if(sha256(reader)=="782cbf8827f91e16b0177f5b1e5b0ad21da8cf6b8b6f3c59a8c61087c29f6dd1" &&
         sha256(caller)=="93899a94bf07a4f6d0bdd72c9845094de90c138d37e733e36ac0b4b194da5f9c" &&
         m.audioNoAck_.arm(pc,a,n,geCaller,m.riu_.knownBits(a,n))){
        m.preservedBits_.seed(2,n,0,[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("exploratory_audio_ack_invalid_sample",a,n,0,"INVALID reply retained; only the exact verified no-ACK comparison may consume its predicate; other unknown use stops");
      }
    }
    if(m.options_.exploratoryAudioNoAck && pc==0x804f9050 && a==0x1f225cb8 && n==1){
      std::vector<uint8_t> reader(0x58),caller(0x104);
      m.cpu_.read(0x004f9004,reader.data(),reader.size());m.cpu_.read(0x004f7a8c,caller.data(),caller.size());
      if(sha256(reader)=="1ca38efb1835870f93e7dfa62d4e7ad5548439c4fe882c6decc70b0d4c34dcfc" &&
         sha256(caller)=="b31c4969e0e91a2f1eb68d4952b3065a2fe62f769b805b9e8b4793b3d7e61e25" &&
         m.audioNoAck_.arm(pc,a,n,geCaller,m.riu_.knownBits(a,n))){
        m.preservedBits_.seed(2,n,0,[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("exploratory_audio_ack_invalid_sample",a,n,0,"INVALID decoder-load reply retained; exact SHA-verified no-ACK predicate only, no valid byte or ready response");
      }
    }
    if(m.options_.exploratoryAnalogDisconnected && pc==0x805f1ca4 && a==0x1f205ea4 && n==1 &&
       (m.riu_.scBank()==1 || m.riu_.scBank()==3)){
      std::vector<uint8_t> code(0x7c);m.cpu_.read(0x005f1c6c,code.data(),code.size());
      if(sha256(code)=="86ceb9987e92def5e205c445f7fa10f48aa94b09ee393430102db91089ba2685"){
        m.preservedBits_.seed(4,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("guarded_euro_hdtv_read",a,n,m.riu_.knownBits(a,n),"SHA-verified original two-bit detection predicate; unrelated byte fields stay invalid through copy/masks and are discarded before decision");
      }
    }
    if(m.options_.deferPlatformIdentity && n==4 &&
       ((pc==0x80500278 && a==0x1f200198) ||
        ((pc==0x8050027c || pc==0x8050028c) && a==0x1f20019c))) {
      std::vector<uint8_t> code(0x40);uint32_t instruction=0;
      m.cpu_.read(0x00500270,code.data(),code.size());m.cpu_.read(pc&0x1fffffff,&instruction,4);
      if(sha256(code)=="487c66522294e5a40c050da2060c8019822957fcfc988b893afe78f61e041294" && instruction>>26==35){
        m.preservedBits_.seed((instruction>>16)&31,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_platform_identity",a,n,0,"INVALID original cache observation; no hardware identity or authorization bits supplied; every decision/address/new-device use must stop");
      }
    }
    if(m.options_.exploratoryPqSramIdle && m.options_.deferDiagnosticReads &&
       pc==0x805f87dc && a==0x1f002880 && n==1 && m.cpu_.reg(UC_MIPS_REG_RA)==0x803be8b0){
      std::vector<uint8_t> caller(0x38),reader(0x24);
      m.cpu_.read(0x003be898,caller.data(),caller.size());m.cpu_.read(0x005f87c4,reader.data(),reader.size());
      if(sha256(caller)=="a5ac5d7fb7978bbe13a29769f0118ddc7b395c0d24bd6a90ddf904b41eedab74" &&
         sha256(reader)=="800e472c50e0aca66e5b03e90febcd803c78e924f256044d4e50fee4addc10d4"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_pq_dummy_observation",a,n,0,"INVALID unused byte from SAR aperture in original PQ configuration; value discarded before next device access, first data use stops; hardware meaning/read side effects unvalidated");
      }
    }
    if(m.options_.exploratoryPqSramIdle && pc==0x805f8a28 && n==2 && m.riu_.scBank()==0x23 && a==0x1f205f04){
      std::vector<uint8_t> code(0x80);m.cpu_.read(0x005f89d0,code.data(),code.size());
      if(sha256(code)=="62ec998080b3026dbcb7560f699b09e9bfd47d20a48cbf028a954f373dd10943"){
        m.preservedBits_.seed(16,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("guarded_pq_sram_read",a,n,m.riu_.knownBits(a,n),"original SRAM busy read; bit8 comes from pending coefficient transfer, unrelated fields retain unknown provenance");
      }
    }
    if(m.options_.exploratoryCiDetectHigh && pc==0x8051ef94 && a==0x1f206891 && n==1 &&
       m.cpu_.reg(UC_MIPS_REG_RA)==0x8051d794){
      uint32_t parentReturn=0;
      m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_SP)+0x14)&0x1fffffff,&parentReturn,4);
      std::vector<uint8_t> caller(0x164),wrapper(0x38),reader(0x24);
      m.cpu_.read(0x0051e94c,caller.data(),caller.size());
      m.cpu_.read(0x0051d770,wrapper.data(),wrapper.size());
      m.cpu_.read(0x0051ef78,reader.data(),reader.size());
      if(parentReturn==0x8051e964 &&
         sha256(caller)=="890094d9c6368720d8a426928ad481fd34a5dc0256f879cceb9ac401194d25ad" &&
         sha256(wrapper)=="bcb69c22ceada71fafe0906ad14cfeafa9411b7fb41a9996e266ae1bf147907a" &&
         sha256(reader)=="f7baf2797d6b3e857d99128c6c11caba5dd4b109a0f7d794f680c0256587e104"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("guarded_ci_detect_read",a,n,m.riu_.knownBits(a,n),
          "original PCMCIA detect wrapper; configured bit2 only, unrelated fields remain invalid until original ANDI discards them");
      }
    }
    if(m.options_.deferDiagnosticReads && m.options_.symbolicInit &&
       pc==0x8051ef94 && a==0x1f2068a0 && n==1 && m.cpu_.reg(UC_MIPS_REG_RA)==0x8051d794){
      uint32_t parentReturn=0;
      m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_SP)+0x14)&0x1fffffff,&parentReturn,4);
      std::vector<uint8_t> caller(0xcc),wrapper(0x38),reader(0x24);
      m.cpu_.read(0x0051ee70,caller.data(),caller.size());
      m.cpu_.read(0x0051d770,wrapper.data(),wrapper.size());
      m.cpu_.read(0x0051ef78,reader.data(),reader.size());
      if(parentReturn==0x8051eed0 &&
         sha256(caller)=="d7e642fa2af8abcd87b7ab59cd1209da95b76eed7ee0517259cb1f07cdc84a02" &&
         sha256(wrapper)=="bcb69c22ceada71fafe0906ad14cfeafa9411b7fb41a9996e266ae1bf147907a" &&
         sha256(reader)=="f7baf2797d6b3e857d99128c6c11caba5dd4b109a0f7d794f680c0256587e104"){
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_pcmcia_initial_observation",a,n,0,
          "INVALID original initialization pre-read: wrapper copies to caller stack, caller overwrites with 0x51; read side effects unverified, any unknown consumption still stops");
      }
    }
    if(m.options_.exploratoryScmiFormat>=0 && pc==0x805f8a28 && n==2 && m.riu_.scBank()==0x12 &&
       (a==0x1f205e08 || a==0x1f205f08)){
      std::vector<uint8_t> code(0x80);m.cpu_.read(0x005f89d0,code.data(),code.size());
      if(sha256(code)=="62ec998080b3026dbcb7560f699b09e9bfd47d20a48cbf028a954f373dd10943"){
        m.preservedBits_.seed(16,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("guarded_scmi_format_read",a,n,m.riu_.knownBits(a,n),"original shared read routine; configured initial low byte only; unknown upper byte retains origin, first decision/address/foreign-device use stops");
      }
    }
    if(m.options_.candidateTsp && m.options_.symbolicInit && m.options_.deferDiagnosticReads &&
       pc>=0x805d671c && pc<0x805d8fbc && n==2 &&
       ((a>=0x1f202a00 && a<0x1f202e00) ||
        (pc>=0x805d8a90 && pc<0x805d8b40 && a==0x1f201614))) {
      // This extends invalid-observation tracking across the verified original
      // TSP HAL, not hardware semantics. The entire loaded code region must
      // still match the immutable main stream. Every unknown branch/address/
      // foreign-device bit use is rejected by PreservedBits independently.
      std::vector<uint8_t> original(0x28a0);uint32_t instruction=0;
      m.cpu_.read(0x005d671c,original.data(),original.size());
      m.cpu_.read(pc&0x1fffffff,&instruction,4);
      if(instruction>>26==37 && sha256(original)=="2dd76c78083be436df9036c3acbfba69ac3eaaca53c2292a85dd8c4e04d50ca3") {
        m.preservedBits_.seed((instruction>>16)&31,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_tsp_hal_observation",a,n,0,"INVALID observation in SHA-verified original HAL; preserved bits may only restore to same origin; no unknown status or hardware read side effect validated");
      }
    }
    if(m.options_.candidateTsp && m.options_.symbolicInit && m.options_.deferDiagnosticReads &&
       pc==0x805d7b3c && a==0x1f202af8 && n==2) {
      constexpr uint32_t count[]={0x3c02806e,0x8c436e84,0x946200fc,0x946300f8,0x03e00008,0x3042001f};
      uint32_t actual[6]{};m.cpu_.read(0x005d7b30,actual,sizeof(actual));
      if(std::equal(std::begin(count),std::end(count),std::begin(actual))) {
        m.preservedBits_.seed(3,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_tsp_count_dummy",a,n,0,"INVALID unused paired low read; occupancy comes only from modeled high count bits");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 &&
       pc>=0x805d6b68 && pc<0x805d6d1c && a>=0x1f202a00 && a<0x1f202e00) {
      // Stock filter-route configuration retains unspecified fields from the
      // strided high/low register pairs. Observations remain INVALID: this is
      // provenance, not a TSP reset/status/firmware-ready model.
      constexpr uint32_t route[]={
        0x000418c0,0x000439c0,0x3c02806e,0x8c486e84,0x00e33823,0x00e43821,0x000738c0,0x01073821,
        0x94e301ec,0x94e201e8,0x2404ffec,0x00031c00,0x3042ffff,0x00621825,0x00641824,0x00651825,
        0x3063ffff,0xa4e301e8,0x94e201ec,0x94e301e8,0x30a900ff,0x00021400,0x3063ffff,0x00431025,
        0x00441024,0x00451025,0x00021402,0xa4e201ec,0x950201fc,0x950301f8,0x31240010,0x3063ffff,
        0xa50301f8,0x950201fc,0x950301f8,0x3042fff7,0xa50201fc,0x10800019,0x00000000,0x950200b4,
        0x950200b0,0x3042fffe,0xa50200b0,0x950300b4,0x950200b0,0x3063ffff,0xa50300b4,0x950201fc,
        0x950201f8,0x3042ffff,0xa50201f8,0x950301fc,0x950201f8,0x3c022000,0x00031c00,0x00431025,
        0x00021402,0xa50201fc,0x2cc20002,0x14400016,0x00000000,0x03e00008,0x00000000,0x950200b4,
        0x950200b0,0x3042fffe,0xa50200b0,0x950300b4,0x950200b0,0x3063ffff,0xa50300b4,0x950201fc,
        0x950201f8,0x3042ffff,0xa50201f8,0x950301fc,0x950201f8,0x2cc20002,0x3063dfff,0xa50301fc,
        0x1040001a,0x00000000,0x38c20001,0x2c420001,0x01221024,0x1440000c,0x00000000,0x95020114,
        0x95020110,0x3042feff,0x34420100,0xa5020110,0x95030114,0x95020110,0x3063ffff,0xa5030114,
        0x03e00008,0x00000000,0x95020114,0x95020110,0x3042fff7,0x34420008,0xa5020110,0x95030114,
        0x95020110,0x3063ffff,0xa5030114,0x03e00008,0x00000000};
      uint32_t actual[109]{};m.cpu_.read(0x005d6b68,actual,sizeof(actual));
      auto instruction=actual[(pc-0x805d6b68)/4];
      if(std::equal(std::begin(route),std::end(route),std::begin(actual)) && instruction>>26==37) {
        m.preservedBits_.seed((instruction>>16)&31,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_tsp_route_configuration",a,n,0,"INVALID stock strided filter-route RMW snapshot; only unchanged origin/bit restoration allowed; no live TSP input inferred");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 &&
       ((a==0x1f202bfc && (pc==0x805d7a14 || pc==0x805d7a28)) ||
        (a==0x1f202bf8 && (pc==0x805d7a18 || pc==0x805d7a2c)))) {
      constexpr uint32_t disable[]={0x3c02806e,0x8c456e84,0x00042202,0x00042027,
        0x94a201fc,0x94a301f8,0x3063ffff,0x00641824,0xa4a301f8,0x94a201fc,
        0x94a301f8,0x00021400,0x00441024,0x00021402,0xa4a201fc,0x03e00008,0};
      uint32_t actual[17]{};m.cpu_.read(0x005d7a04,actual,sizeof(actual));
      if(std::equal(std::begin(disable),std::end(disable),std::begin(actual))) {
        unsigned target=a==0x1f202bfc?2:3;
        m.preservedBits_.seed(target,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_tsp_irq_mask",a,n,0,"INVALID original strided IRQ-mask configuration snapshot; ignored high pre-reads and same-halfword restore only; no TSP interrupt/ready response supplied");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 && a==0x1f203ffc &&
       (pc==0x8053a4b8 || pc==0x8053a4c8 || pc==0x8053a500)) {
      constexpr uint32_t writer32[]={0x94e23ffc,0x3049000f,0x11240015,0x30a2ffff,
        0x94e23ffc,0x30a3ffff,0x00031840,0x3042fff0,0x3084000f,0x24e50004,
        0x00822025,0x00a32821,0x3102ffff,0x00063402,0x00671821,0xa4e43ffc,
        0xa4620000,0xa4a60000,0x94e23ffc,0x3042fff0,0x01221025,0xa4e23ffc,
        0x03e00008,0x00000000};
      uint32_t actual[24]{};m.cpu_.read(0x0053a4b8,actual,sizeof(actual));
      if(std::equal(std::begin(writer32),std::end(writer32),std::begin(actual))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_write32_selector",a,n,0,"INVALID IRQ/ACK flags retained by original 32-bit banked writer; only known selector nibble reaches comparison");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x8053a198 &&
       ((geCaller==0x8053a248 && a>=0x1f201600 && a<0x1f206000) ||
        (a==0x1f205e18 && m.riu_.scBank()==0) ||
        (a==0x1f205e14 && m.riu_.scBank()==0x10 && geCaller==0x8053adf0)) && n==2 &&
       (geCaller==0x8053a828 || geCaller==0x8053a248 || geCaller==0x8053adf0)) {
      constexpr uint32_t leaf[]={0x94640000,0x03e00008,0xa4c40000};
      uint32_t actual[3]{};m.cpu_.read(0x0053a198,actual,sizeof(actual));
      if(std::equal(std::begin(leaf),std::end(leaf),std::begin(actual))) {
        m.preservedBits_.seed(4,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_scaler_control",a,n,0,"INVALID original GOP scaler snapshot; original masks retain known fields only for decisions, unchanged unknown fields may restore to the same origin");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 && a==0x1f203ffc &&
       (pc==0x8053a2ec || pc==0x8053a2fc || pc==0x8053a320)) {
      constexpr uint32_t writer[]={0x94a23ffc,0x3046000f,0x10c40015,0x3242ffff,
        0x94a23ffc,0x3243ffff,0x3084000f,0x3042fff0,0x00031840,0x00822025,
        0x00651821,0xa4a43ffc,0xa4710000,0x94a23ffc,0x8fbf002c,0x8fb30028,
        0x3042fff0,0x00c21025,0x8fb20024,0x8fb10020,0x8fb0001c,0xa4a23ffc,
        0x03e00008,0x27bd0030};
      uint32_t actual[24]{};m.cpu_.read(0x0053a2ec,actual,sizeof(actual));
      if(std::equal(std::begin(writer),std::end(writer),std::begin(actual))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_selector_restore",a,n,0,"INVALID IRQ/ACK fields preserved by original masked bank write; only known low nibble reaches bank comparison");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && n==2 &&
       a>=0x1f203e00 && a<0x1f204000 &&
       (pc==0x8053a1ac || pc==0x8053a1bc || pc==0x8053a1e4 || pc==0x8053a1dc || pc==0x8053a204)) {
      constexpr uint32_t selector[]={0x94a23ffc,0x3047000f,0x10870011,0x3062ffff,
        0x94a23ffc,0x3084000f,0x3063ffff,0x3042fff0,0x00822025,0x00031840,
        0xa4a43ffc,0x00651821,0x94640000,0xa4c40000,0x94a23ffc,0x3042fff0,0x00e21025,0xa4a23ffc};
      constexpr uint32_t tail[]={0x94430000,0x03e00008,0xa4c30000};
      uint32_t actualSelector[18]{},actualTail[3]{};
      m.cpu_.read(0x0053a1ac,actualSelector,sizeof(actualSelector));
      m.cpu_.read(0x0053a204,actualTail,sizeof(actualTail));
      if(std::equal(std::begin(selector),std::end(selector),std::begin(actualSelector)) &&
         std::equal(std::begin(tail),std::end(tail),std::begin(actualTail))) {
        unsigned target=(pc==0x8053a1dc)?4:(pc==0x8053a204)?3:2;
        m.preservedBits_.seed(target,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_gop_halfword_rmw",a,n,0,"INVALID original HAL bank/read sample; unused IRQ fields stay unknown; first unknown decision/address/new-device use remains guarded");
      }
    }
    bool geInitCheck=false;
    if(m.options_.candidateGe && geCaller==0x805271e8 && a==0x1f205004) {
      constexpr uint32_t caller[]={0x30420002,0x1440001f,0x3c048063};
      uint32_t actualCaller[3]{};m.cpu_.read(geCaller&0x1fffffff,actualCaller,sizeof(actualCaller));
      geInitCheck=std::equal(std::begin(caller),std::end(caller),std::begin(actualCaller));
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x805268dc &&
       a>=0x1f205000 && a<0x1f205200 && n==2 && (geCaller==0x80526988 || geInitCheck)) {
      constexpr uint32_t expected[]={0x94640000,0x8fbf001c,0x8fb10018,0x00c42024,
        0x00a42025,0x3082ffff,0x8fb00014,0x03e00008,0x27bd0020};
      uint32_t actual[9]{};m.cpu_.read(pc&0x1fffffff,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
        m.preservedBits_.seed(4,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event(geInitCheck?"deferred_ge_init_check":"deferred_ge_context_sample",a,n,0,
          geInitCheck?"INVALID unused live control fields retained through original GE_ReadReg; verified caller ANDI keeps only independently known VCmd enable bit":
          "INVALID original GE_ReadReg context sample; only unchanged origin/bit restore permitted, no enable/queue/draw state inferred");
      }
    }
    if(m.options_.symbolicInit && m.options_.deferDiagnosticReads && pc==0x805f87dc && n==1 &&
       a>=0x1f206c04 && a<0x1f206e00 && (a&1) && m.cpu_.reg(UC_MIPS_REG_31)==0x803c69c8) {
      constexpr uint32_t expected[]={0x00408821,0x00141827,0x02b42024,0x00431024,0x00441025,
        0x7c021620,0x3043ffff,0x00031a00,0x00431025,0x3045ffff,0x02002021,
        0x0c17e204,0x3406ffff};
      uint32_t actual[13]{};m.cpu_.read(0x003c69c4,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual))) {
        m.preservedBits_.seed(2,n,m.riu_.knownBits(a,n),[&](unsigned b){return m.riu_.controlByteKey(a+b);});
        m.riu_.prepareOpaqueRead(a);
        m.event("deferred_comb_high_sample",a,n,0,"INVALID T4 high-byte workaround sample; masked field tracked, different-byte hardware use remains guarded");
      }
    }
    if (m.options_.symbolicInit && m.options_.deferDiagnosticReads &&
        ((pc >= 0x805e5890 && pc < 0x805e6200) || (pc >= 0x805e6b9c && pc < 0x805e6d40)) &&
        a >= 0x1f205e00 && a < 0x1f206600) {
      uint32_t path[4]{};
      m.cpu_.read(pc & 0x1fffffff, path, sizeof(path));
      auto op = path[0] >> 26, rt = (path[0] >> 16) & 31;
      auto nextOp = path[1] >> 26, nextRt = (path[1] >> 16) & 31;
      bool overwritten = nextOp == 9 && (path[1] & 0x03e00000) == 0 && nextRt == rt;
      bool replaced = false;
      for (unsigned i = 1; !overwritten && !replaced && i < 4; ++i) {
        auto futureOp = path[i] >> 26, futureRt = (path[i] >> 16) & 31, futureRs = (path[i] >> 21) & 31;
        if ((futureOp == 40 || futureOp == 41) && futureRt != rt &&
            ((path[0] ^ path[i]) & 0x03e0ffff) == 0) { replaced = true; break; }
        if (futureRt == rt && ((futureOp == 9 && futureRs == 0) || futureOp == 15 ||
            ((futureOp == 35 || futureOp == 36 || futureOp == 37) && futureRs != rt))) {
          overwritten = true; break;
        }
        if (futureOp != 9 || futureRt == rt || futureRs == rt || futureRt == ((path[0] >> 21) & 31)) break;
      }
      if ((op == 36 || op == 37) && rt && (overwritten || replaced)) {
        m.unknownObservations_.seed(rt);
        m.event("discarded_timing_read", a, n, 0xa5,
                "INVALID timing latch observation: original next instruction overwrites value or register; first other use stops");
        return n == 1 ? 0xa5 : 0xa5a5;
      }
    }
    if (m.options_.deferDiagnosticReads && m.riu_.knownBits(a, n) != 0xffff &&
        m.proveDiagnosticCache(pc, a, n)) {
      m.unknownObservations_.seed(3);
      ++m.deferredReads_;
      m.event("deferred_read", a, n, 0xa5a5,
              "INVALID unknown diagnostic observation; original guest may cache it, first "
              "consumption stops");
      return 0xa5a5;
    }
    if (m.options_.deferDiagnosticReads && n == 1 && m.cpu_.reg(UC_MIPS_REG_4) == 1 &&
        ((pc == 0x805fd41c && a == 0x1f201751) || (pc == 0x805fd424 && a == 0x1f201754))) {
      uint32_t path[13]{};
      m.cpu_.read(0x005fd41c, path, sizeof(path));
      constexpr uint32_t expected[] = {0x90c80000,0x00621821,0x90e90000,0xa4a00000,0x94620000,
        0x309000ff,0x24040001,0x3042ff00,0x00501025,0xa4620000,0x311100ff,0x0c172ef6,0x313200ff};
      if (std::equal(std::begin(path),std::end(path),std::begin(expected))) {
        m.unknownObservations_.seedClockSnapshot(pc == 0x805fd41c ? 8 : 9, a);
        m.event("unused_clock_snapshot", a, n, 0xa5,
                "INVALID snapshot in byte-verified mode-1 path; transparent saves allowed, first branch/calculation/device use stops");
        return 0xa5;
      }
    }
    auto mask = m.cpu_.reg(
        pc == 0x8055b578 || pc == 0x8055b584 || pc == 0x8053bc08 ? UC_MIPS_REG_7 : UC_MIPS_REG_3);
    if (pc == 0x8060a780 || pc == 0x8060a784) {
      auto bit = m.cpu_.reg(UC_MIPS_REG_7) & 31;
      auto low = m.cpu_.reg(UC_MIPS_REG_9) & 0x1fffffff;
      mask = (uint32_t(1) << bit) >> (a == low ? 0 : 16);
    }
    if (pc == 0x8053bbdc || pc == 0x8053bc84) {
      uint8_t b = 0;
      m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_3) + 4) & 0x1fffffff, &b, 1);
      mask = b;
    }
    if (pc == 0x8053bcb0 || pc == 0x8053bd18 || pc == 0x8053bd80 || pc == 0x8053bde4) {
      uint8_t b = 0;
      auto base = m.cpu_.reg(pc == 0x8053bcb0   ? UC_MIPS_REG_8
                             : pc == 0x8053bd18 ? UC_MIPS_REG_5
                                                : UC_MIPS_REG_6);
      m.cpu_.read((base + (pc == 0x8053bde4 ? 0x14 : 4)) & 0x1fffffff, &b, 1);
      mask = b;
    }
    if (n == 1 && (a == 0x1f207578 || a == 0x1f207478)) {
      uint32_t instructions[2]{};
      m.cpu_.read(pc & 0x1fffffff, instructions, sizeof(instructions));
      // LBU v0 followed by ANDI v0,v0,2: no other calibration bits consumed.
      if ((instructions[0] & 0xfc1f0000) == 0x90020000 && instructions[1] == 0x30420002)
        mask = 2;
    }
    if (n == 2 && a >= 0x1f001000 && a < 0x1f001100) {
      uint32_t instructions[2]{};
      m.cpu_.read(pc & 0x1fffffff, instructions, sizeof(instructions));
      if ((instructions[0] & 0xfc1f0000) == 0x94020000 && instructions[1] == 0x30420001)
        mask = 1;
      if (a == 0x1f001014 && (pc == 0x8053c710 || pc == 0x8053c8a0)) {
        uint32_t path[6]{};
        m.cpu_.read(pc & 0x1fffffff, path, sizeof(path));
        const uint32_t cfPath[] = {0x94e40000,0x02461821,0x24c60001,0x00d3102b,0x1440fff2,0xa0640000};
        const uint32_t idPath[] = {0x94e40000,0x02661821,0x24c60001,0x00d2102b,0x1440fff2,0xa0640000};
        if (std::equal(std::begin(path), std::end(path), pc == 0x8053c710 ? cfPath : idPath)) {
          mask = 255; // exact LHU -> SB path discards upper halfword
          if (pc == 0x8053c710 && m.options_.spiUnknownInput == SpiUnknownInput::Deferred && m.riu_.spiDataUnknown(m.virtualTime_.cycles(m.dispatches_))) {
            m.unknownObservations_.seedSpiCache(m.cpu_.reg(UC_MIPS_REG_18) + m.cpu_.reg(UC_MIPS_REG_6));
            m.event("deferred_spi_input", a, n, 0xa5, "INVALID undocumented CF sample; verified SB cache only, first consumption stops");
          }
        }
      }
    }
    auto v = m.riu_.read(a, n, pc, mask, m.virtualTime_.cycles(m.dispatches_));
    if (!m.stopped_ && m.riu_.stopReason=="unmodeled MMIO read" && !m.options_.opaqueReadPolicy.empty() &&
        m.observePolicyRead(pc,a,n))m.riu_.stopReason.clear();
    if (!m.riu_.stopReason.empty())
      m.stop(m.riu_.stopReason, a);
    return v;
  } catch (const std::exception &e) {
    m.stop(e.what(), 0x1f000000 + uint32_t(off));
    return 0xdeadbeef;
  }
}
bool Msd309Machine::observePolicyRead(uint32_t pc,uint32_t address,unsigned width){
  event("opaque_policy_read_request",address,width,riu_.knownBits(address,width),
    "unmodeled original read; no value supplied; debugger wait freezes ALL guest clocks; PC="+hex(pc));
  if(trace_)trace_.flush();
  auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(options_.opaquePolicyWaitMs);
  do{
    std::ifstream file(options_.opaqueReadPolicy,std::ios::binary);
    if(file){
      std::string text;char block[4096];
      while(file.read(block,sizeof(block)) || file.gcount()){
        text.append(block,size_t(file.gcount()));if(text.size()>65536)throw std::runtime_error("opaque policy exceeds64KiB");
      }
      file.close(); // Windows must release the handle before atomic policy replacement while paused
      for(const auto& rule:parseOpaqueReadPolicy(text))if(rule.pc==pc && rule.address==address && rule.width==width){
        std::vector<uint8_t> code(rule.size);cpu_.read(rule.start&0x1fffffff,code.data(),code.size());
        if(!validateOpaqueReadRule(rule,pc,address,width,code))throw std::runtime_error("opaque policy original-code hash or load mismatch");
        uint32_t word=0;cpu_.read(pc&0x1fffffff,&word,4);
        auto effective=(cpu_.reg(UC_MIPS_REG_0+int((word>>21)&31))+uint32_t(int32_t(int16_t(word))))&0x1fffffff;
        if(effective!=address)throw std::runtime_error("opaque policy effective address mismatch");
        preservedBits_.seed((word>>16)&31,width,riu_.knownBits(address,width),[&](unsigned b){return riu_.controlByteKey(address+b);});
        ++opaquePolicyReads_;
        auto identity=hex(pc)+" "+hex(address)+" "+std::to_string(width)+" "+rule.hash;
        opaquePolicyRules_+=identity+"\n";
        event("opaque_policy_invalid_observation",address,width,riu_.knownBits(address,width),
          identity+"; original read result retained INVALID; no repeated bus access, device state, CPU value or PC assignment; read side effects unverified");
        return true;
      }
    }
    if(std::chrono::steady_clock::now()>=deadline)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }while(true);
  return false;
}
bool Msd309Machine::proveDiagnosticCache(uint32_t pc, uint32_t a, unsigned n) {
  if (n != 2 || !((pc == 0x80469898 && a == 0x1f200a38) || (pc == 0x804698ac && a == 0x1f200a3c)))
    return false;
  // Byte identity of both complete paths, including the JAL delay-slot store.
  constexpr uint32_t words[] = {0x94430000, 0x36100a3c, 0x3c02806c, 0x00902021, 0xa443914a,
                                0x94830000, 0x3c02806c, 0x0c101182, 0xa443910e};
  uint32_t actual[9]{};
  cpu_.read(0x00469898, actual, sizeof(actual));
  return std::equal(std::begin(words), std::end(words), std::begin(actual)) &&
         cpu_.reg(UC_MIPS_REG_4) == (pc == 0x80469898 ? 0xbf000000 : 0xbf200a3c);
}
// Prove a straight-line immediate AND/OR RMW before allowing unknown reset
// bits through it. Reject sampled-value branches, calls, escaping stores and other
// uses of the sampled value. This is bit provenance, not a status response.
uint32_t Msd309Machine::proveImmediateRmw(uint32_t pc, uint32_t address, unsigned width) {
  rmwIdentity_ = false;
  if ((pc < 0x803b0180 || pc >= 0x806b8ce0) && (pc < 0xbfc00000 || pc >= 0xbfc02c60))
    return 0;
  uint32_t w;
  cpu_.read(pc & 0x1fffffff, &w, 4);
  auto op = w >> 26;
  if (op != 36 && op != 37 && op != 35)
    return 0;
  if ((op == 36 ? 1u : op == 37 ? 2u : 4u) != width)
    return 0;
  auto sampled = (w >> 16) & 31;
  uint32_t dependent[32]{};
  uint32_t values[32]{};
  for (unsigned r = 0; r < 32; ++r)
    values[r] = cpu_.reg(UC_MIPS_REG_0 + int(r));
  dependent[sampled] = width == 4 ? 0xffffffff : (uint32_t(1) << (width * 8)) - 1;
  bool changed = false, independentSpill=false;
  uint32_t proofPc=pc+4, jumpAfterDelay=0;
  for (unsigned i = 1; i <= 12; ++i) {
    cpu_.read(proofPc & 0x1fffffff, &w, 4);
    auto instructionPc=proofPc;
    bool delaySlot=jumpAfterDelay!=0;
    if(jumpAfterDelay) { proofPc=jumpAfterDelay; jumpAfterDelay=0; }
    else proofPc+=4;
    op = w >> 26;
    auto rs = (w >> 21) & 31, rt = (w >> 16) & 31;
    if(delaySlot && (op==2 || op==4 || op==5))return 0;
    if(op==2) {
      auto target=(instructionPc&0xf0000000)|((w&0x3ffffff)<<2);
      if(target+0x400<pc || target>pc+0x400 || target==instructionPc) return 0;
      jumpAfterDelay=target; // analyze original J delay slot then target; CPU still executes both
    } else if((op==4 || op==5) && !dependent[rs] && !dependent[rt] &&
              !unknownObservations_.registerUnknown(rs) && !unknownObservations_.registerUnknown(rt) &&
              !preservedBits_.mask(rs) && !preservedBits_.mask(rt)) {
      // Original 805FDC44 branches on its independently known enable arg.
      // Analyze that selected path INCLUDING its architectural delay slot.
      // A comparison involving sampled MMIO bits never qualifies.
      bool take=op==4?values[rs]==values[rt]:values[rs]!=values[rt];
      if(take){
        auto target=instructionPc+4+uint32_t(int32_t(int16_t(w))*4);
        if(target+0x400<pc || target>pc+0x400 || target==instructionPc)return 0;
        jumpAfterDelay=target;
      }
    } else if (op == 12 || op == 13) {
      if (rt == 0)
        return 0;
      dependent[rt] = op == 12 ? dependent[rs] & (w & 65535) : dependent[rs] & ~(w & 65535);
      values[rt] = op == 12 ? values[rs] & (w & 65535) : values[rs] | (w & 65535);
      changed = true;
    } else if (op == 0 && w == 0)
      continue;
    else if (op == 15 && rt != 0) {
      dependent[rt] = 0;
      values[rt] = (w & 65535) << 16;
    } else if (op == 9 && !dependent[rs] && rt != 0) {
      dependent[rt] = 0;
      values[rt] = values[rs] + uint32_t(int32_t(int16_t(w)));
    } else if(op==11 && !dependent[rs] && rt!=0) {
      dependent[rt]=0; values[rt]=values[rs]<uint32_t(int32_t(int16_t(w)));
    } else if (op == 0 && ((w & 63) == 33 || (w & 63) == 35) &&
               !dependent[rs] && !dependent[rt] && ((w >> 11) & 31) != 0) {
      auto rd = (w >> 11) & 31;
      dependent[rd] = 0;
      values[rd] = (w & 63) == 33 ? values[rs] + values[rt] : values[rs] - values[rt];
    } else if (op == 0 && (w & 63) == 0 && !dependent[rt] && ((w >> 11) & 31) != 0) {
      auto rd = (w >> 11) & 31;
      dependent[rd] = 0;
      values[rd] = values[rt] << ((w >> 6) & 31);
    } else if(op==0 && ((w&63)==42 || (w&63)==43) &&
              !dependent[rs] && !dependent[rt] && ((w>>11)&31)!=0){
      // Original 806001D8 converts an independent boolean argument with
      // SLTU before masking/storing another register's sampled value.
      // Comparing the MMIO sample itself is still rejected.
      auto rd=(w>>11)&31;dependent[rd]=0;
      values[rd]=(w&63)==42?int32_t(values[rs])<int32_t(values[rt]):values[rs]<values[rt];
    } else if (op == 0 && ((w & 63) == 10 || (w & 63) == 11) &&
               !dependent[rs] && !dependent[rt] && !dependent[(w >> 11) & 31]) {
      auto rd = (w >> 11) & 31;
      if (rd && ((w & 63) == 10 ? values[rt] == 0 : values[rt] != 0)) values[rd] = values[rs];
    } else if (options_.provenanceExtensions && op == 0 && ((w & 63) == 10 || (w & 63) == 11) && !dependent[rt] && ((w >> 11) & 31) != 0) {
      // EXPLORATORY (opt-in): MOVZ/MOVN whose condition register is independent selects, deterministically, either the source or the old destination;
      // the chosen register's bit dependencies carry over unchanged (no sampled bit is compared, so nothing is decided from it).
      auto rd = (w >> 11) & 31;
      if ((w & 63) == 10 ? values[rt] == 0 : values[rt] != 0) { values[rd] = values[rs]; dependent[rd] = dependent[rs]; }
    } else if (op == 31 && (w & 63) == 0) {
      // Original805EE830: LBU a0; EXT a1,a2,16,8; ANDI a0,BF; SB.
      // The independent EXT is not a use of the sampled control byte.
      auto extracted=independentExtract(w,values[rs],!dependent[rs] &&
        !unknownObservations_.registerUnknown(rs) && !preservedBits_.mask(rs));
      if(!extracted)return 0;
      values[rt]=*extracted;dependent[rt]=0;
    } else if (op == 31 && rs == 0 && (w & 0x7ff) == 0x620 && ((w >> 11) & 31)) {
      auto rd = (w >> 11) & 31;
      values[rd] = uint32_t(int32_t(int16_t(values[rt])));
      // SEH leaves the low sixteen bit identities unchanged. Replicated
      // unknown sign bits stay dependent until the original AND FFFF/SH
      // discards them; they cannot authorize a branch/address/other store.
      dependent[rd]=(dependent[rt]&0xffff)|((dependent[rt]&0x8000)?0xffff0000u:0);
    } else if (op == 0 && (w & 63) == 39 && !dependent[rs] && !dependent[rt] && ((w >> 11) & 31)) {
      auto rd = (w >> 11) & 31;
      values[rd] = ~(values[rs] | values[rt]); dependent[rd] = 0;
    } else if ((op == 35 || op == 36 || op == 37) && !dependent[rs] && rt != 0) {
      // An independent RAM callback-pointer load may occur inside a device
      // RMW. It neither consumes nor defines the sampled register bits.
      if(independentSpill)return 0; // never simulate a load using stale pre-spill RAM
      auto target = PreservedBits::backingAddress(values[rs] + uint32_t(int32_t(int16_t(w))));
      auto loadSize = op == 35 ? 4u : op == 37 ? 2u : 1u;
      auto ramSize=dramBacking_.empty()?0x08000000u:uint32_t(dramBacking_.size());
      if (target > ramSize - loadSize || (target & (loadSize - 1))) return 0;
      for (unsigned byte = 0; byte < loadSize; ++byte)
        if (unknownObservations_.bytes().contains(target + byte) || preservedBits_.memoryUnknown(target+byte)) return 0;
      values[rt] = 0;
      cpu_.read(target, &values[rt], loadSize);
      dependent[rt] = 0;
    } else if (op == 0 && ((w & 63) == 36 || (w & 63) == 37)) {
      auto rd = (w >> 11) & 31;
      if(!rmwLogicalInputTracked(dependent[rs],preservedBits_.mask(rs),unknownObservations_.registerUnknown(rs)) ||
         !rmwLogicalInputTracked(dependent[rt],preservedBits_.mask(rt),unknownObservations_.registerUnknown(rt)))return 0;
      if (rd == 0 || (dependent[rs] && dependent[rt]))
        return 0;
      auto known = dependent[rs] ? values[rt] : values[rs];
      auto dep = dependent[rs] | dependent[rt];
      dependent[rd] = (w & 63) == 36 ? dep & known : dep & ~known;
      values[rd] = (w & 63) == 36 ? values[rs] & values[rt] : values[rs] | values[rt];
      changed = true;
    } else if (op == 40 || op == 41 || op == 43) {
      auto target = (values[rs] + uint32_t(int32_t(int16_t(w)))) & 0x1fffffff;
      auto n = op == 40 ? 1u : op == 41 ? 2u : 4u;
      if(independentStackSpill(w,!dependent[rs] && !preservedBits_.mask(rs) &&
          !unknownObservations_.registerUnknown(rs),!dependent[rt])){
        auto ram=PreservedBits::backingAddress(values[rs]+uint32_t(int32_t(int16_t(w))));
        if((ram&3) || uint64_t(ram)+4>dramBacking_.size())return 0;
        independentSpill=true;continue; // CPU executes the real store; proof changes no memory
      }
      // A register may have held the sample, then been overwritten with an
      // independently known address. Its current dependency, not its name,
      // determines whether the same-device store has a proven destination.
      if (target != address || n != width || !changed || dependent[rs])
        return 0;
      auto full = width == 4 ? 0xffffffff : (uint32_t(1) << (width * 8)) - 1;
      // Only AND/OR with immediates and independent values reach here, so bit i of the stored value is bit i of the sample or a known
      // constant. When no bit is defined every stored bit is the sampled bit itself: an identity write-back, not a decision.
      if (!(full & ~dependent[rt]) && (dependent[rt] & full) == full) rmwIdentity_ = true;
      return full & ~dependent[rt];
    } else
      return 0;
  }
  return 0;
}
void Msd309Machine::mmioWrite(uc_engine *, uint64_t off, unsigned n, uint64_t v, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  try {
    auto a = 0x1f000000 + uint32_t(off);
    auto pc = m.cpu_.reg(UC_MIPS_REG_PC);
    if ((pc & 0x1fffffff) >= 0x1fc00000 && (pc & 0x1fffffff) < 0x1fc02c60)
      pc = (pc & 0x1fffffff) | 0xa0000000;
    if ((pc == 0x805e5d44 || pc == 0x805e5eb4) && a == 0x1f205e00 && n == 1 &&
        m.unknownObservations_.timingSnapshotUnknown(pc == 0x805e5d44 ? 10 : 6) && m.riu_.restoreTimingSnapshot()) {
      m.event("opaque_timing_restore", a, n, 0xa5,
              "original control snapshot restored with original unknown-bit provenance; poison not programmed as a hardware value");
      return;
    }
    m.riu_.write(a, n, uint32_t(v), pc, m.cpu_.reg(UC_MIPS_REG_3), m.virtualTime_.cycles(m.dispatches_));
    if (a >= 0x1f203e00 && a < 0x1f203ffc && m.riu_.gopBankKnown()) {
      auto bank = m.riu_.gopBank();
      for (unsigned window = 0; window < (bank == 1 ? 4u : 2u); ++window) {
        if (auto info = readGopWindow(m.riu_, bank, window))
          m.event("gop_window_descriptor", info->miuRelativeAddress, info->pitch, info->format,
                  "guest-selected bank " + std::to_string(bank) + " window " + std::to_string(window) +
                  " size " + std::to_string(info->width) + "x" + std::to_string(info->height) +
                  "; MIU selection/compositing unresolved; descriptor is not an authentic frame");
      }
    }
    if (!m.riu_.stopReason.empty())
      m.stop(m.riu_.stopReason, a);
  } catch (const std::exception &e) {
    m.stop(e.what(), 0x1f000000 + uint32_t(off));
  }
}
bool Msd309Machine::applyHtotalBranchJoin() {
  using J = HtotalBranchJoin;
  const uint32_t counter = 0x006d83d4, total = 0x006ad960;
  for (uint32_t cell : {counter, total, total + 1})
    if (unknownObservations_.bytes().contains(cell) || preservedBits_.memoryUnknown(cell))
      return false; // already-unknown cells: keep the strict stop
  J::Regs entry{};
  for (unsigned r = 0; r < 32; ++r) entry[r] = cpu_.reg(UC_MIPS_REG_0 + int(r));
  auto fetch = [&](uint32_t pc) { uint32_t w = 0; cpu_.read(pc & 0x1fffffff, &w, 4); return w; };
  auto load8 = [&](uint32_t address) { uint8_t b = 0; cpu_.read(address, &b, 1); return b; };
  J::Merge merge;
  try {
    merge = J::run(entry, fetch, load8);
  } catch (const std::exception& error) {
    event("diagnostic_htotal_branch_join_refused", J::branch, 4, 0, error.what());
    return false;
  }
  std::ostringstream detail;
  detail << "DIAGNOSTIC ONLY, NOT HARDWARE EXECUTION: both original outcomes executed separately in a private copy;"
            " live state identical, differing dead registers and RAM cells marked unknown;";
  for (auto r : merge.differingRegisters) {
    cpu_.reg(UC_MIPS_REG_0 + int(r), merge.outcomes[0].regs[r]);
    unknownObservations_.seed(r);
    preservedBits_.seed(r, 4, 0, [](unsigned) { return 0x1f206b90u; });
    detail << " reg" << r << "=unknown";
  }
  for (unsigned r = 1; r < 32; ++r) {
    if (r == 7) continue; // a3 keeps its unknown tag and placeholder
    bool differing = std::find(merge.differingRegisters.begin(), merge.differingRegisters.end(), r) !=
                     merge.differingRegisters.end();
    if (differing) continue;
    if (merge.common[r] != entry[r]) {
      cpu_.reg(UC_MIPS_REG_0 + int(r), merge.common[r]);
      unknownObservations_.markRegisterKnown(r);
    }
  }
  for (auto& [address, unknown] : merge.unknownBytes) {
    if (unknown) {
      unknownObservations_.markMemoryUnknown(address, 1, 0x1f206b90);
      preservedBits_.markMemoryUnknown(address, 0x1f206b90);
      detail << " ram" << hex(address) << "=unknown";
    } else {
      // Every outcome leaves the same final byte: apply it as a definite effect.
      uint8_t value = load8(address);
      for (auto& store : merge.outcomes[1].stores)
        for (unsigned i = 0; i < store.width; ++i)
          if (store.address + i == address) value = uint8_t(store.value >> (8 * i));
      cpu_.write(address, &value, 1);
      unknownObservations_.overwriteKnownMemory(address, 1);
      preservedBits_.overwriteKnownMemory(address, 1);
    }
  }
  ++htotalJoins_;
  event("diagnostic_htotal_branch_join", J::branch, 4, J::join, detail.str());
  joinResume_ = true;
  joinResumePc_ = J::join;
  cpu_.reg(UC_MIPS_REG_PC, J::join);
  uc_emu_stop(cpu_.handle);
  return true;
}
int Msd309Machine::run() {
  if (options_.initPrefix) {
    if (!options_.stage2 || !options_.symbolicInit)
      throw std::runtime_error("init-prefix requires diagnostic-stage2 and symbolic-init");
    preparingPrefix_ = true;
    cpu_.run(0xbfc00000, 100000);
    preparingPrefix_ = false;
    if (reason_ != "diagnostic prefix boundary before MIU ready poll")
      throw std::runtime_error("Early prefix did not reach boundary: " + reason_);
    stopped_ = false;
    reason_.clear();
    blockingPc_ = blockingAddress_ = 0;
    event("checkpoint", 0x81000000, 0, 0,
          "DIAGNOSTIC host entry transfer after real early register writes; "
          "MIU completion and stage copy bypassed");
  }
  if (!options_.loadState.empty()) {
    restoreSnapshot(options_.loadState);
    uint8_t ctl = 0;
    if (options_.exploratoryCopro && !copro_.started && riu_.coproPeekByte(0x1f201fe0u, ctl) && ctl == 3)
      throw std::runtime_error("the snapshot postdates the guest's coprocessor release (1F201FE0 = 3); the coprocessor model needs a snapshot from before that point");
  }
  if (!options_.saveState.empty() && options_.loadState.empty() && (options_.trace.empty() == false || options_.replay.empty() == false))
    throw std::runtime_error("save-state cannot be combined with --record-trace/--replay-trace");
  Sampler sampler;
  if (!options_.profile.empty()) sampler.start(500);
  // The dispatch budget is enforced by the hook itself (one callback per instruction instead of two: Unicorn's own count hook is not used).
  hookLimit_ = options_.limit ? options_.limit : UINT64_MAX;
  if (resumed_ && options_.limit && options_.limit <= dispatches_) hookLimit_ = dispatches_ + 1;   // an exhausted budget still executes one instruction
  shadowValid_ = false;
  std::unordered_map<uint32_t, uint64_t> pcBase;
  if (std::getenv("MANTA_PC_DELTA")) { flushStats(); pcBase = pcHits_; }
  auto e = resumed_ ? cpu_.run(resumePc_, 0) : cpu_.run(options_.stage2 ? 0x81000000 : 0xbfc00000, 0);
  while (!stopped_ && joinResume_) {
    // DIAGNOSTIC HTotal join: the hook stored the resume PC and stopped Unicorn.
    joinResume_ = false;
    if (dispatches_ >= options_.limit)break;
    shadowValid_ = false;
    e = cpu_.run(joinResumePc_, 0);
  }
  if (!options_.profile.empty()) sampler.stopAndWrite(options_.profile);
  if (std::getenv("MANTA_PC_DELTA")) {
    flushStats();
    std::vector<std::pair<uint64_t, uint32_t>> v;
    for (auto &kv : pcHits_) { auto it = pcBase.find(kv.first); uint64_t b = it == pcBase.end() ? 0 : it->second; if (kv.second > b) v.push_back({kv.second - b, kv.first}); }
    std::sort(v.rbegin(), v.rend());
    for (size_t i = 0; i < std::min<size_t>(v.size(), 40); ++i) std::fprintf(stderr, "pcdelta %08x %llu\n", v[i].second, (unsigned long long)v[i].first);
  }
  if (std::getenv("MANTA_LOOP_STATS")) { std::fprintf(stderr, "loop-skipped instructions: %llu of %llu\n", (unsigned long long)skippedInstructions_, (unsigned long long)dispatches_); }
  if (!stopped_) {
    blockingPc_ = cpu_.reg(UC_MIPS_REG_PC);
    reason_ = e == UC_ERR_OK ? "instruction budget exhausted" : uc_strerror(e);
  }
  if (replay_.is_open()) {
    std::string line;
    if (std::getline(replay_, line))
      reason_ = "replay has remaining events";
  }
  if (!options_.coverage.empty()) {
    std::ofstream f(options_.coverage);
    for (auto pc : coverage_)
      f << hex(pc) << '\n';
    f.flush();if(!f)throw std::runtime_error("cannot write complete guest PC coverage file");
  }
  if(!options_.pm51History.empty()){
    std::ofstream f(options_.pm51History);
    f<<riu_.pm51().historyJsonLines();
    f.flush();if(!f)throw std::runtime_error("cannot write complete bounded PM history");
  }
  publishPreview();
  auto s = summary();
  if (!options_.summary.empty()) {
    std::ofstream f(options_.summary);
    f << s << '\n';
    f.flush();if(!f)throw std::runtime_error("cannot write complete execution summary file");
  }
  if (!options_.exportGopFrame.empty()) exportGopFrames();
  if (!options_.gopFrameHistory.empty()) {
    publishPreview();
    std::ofstream idx(options_.gopFrameHistory / "index.json");
    idx << "{\n  \"note\": \"diagnostic GOP preview timeline; format 6 assumed ARGB1555; not panel output\",\n  \"frames\": [\n    " << historyIndex_ << "\n  ]\n}\n";
  }
  if (!options_.dumpRanges.empty()) dumpGuestRanges();
  std::cout << s << "\nUART: " << riu_.uart << '\n';
  return 2; // Research execution is incomplete even when a diagnostic
            // checkpoint is reached.
}
// Private copies of the memory behind every ENABLED + ACTIVE-LATCHED GOP window (never forces anything on).
std::vector<GopLayer> Msd309Machine::collectGopLayers() const {
  std::vector<GopLayer> layers;
  for (auto bank : {1u, 4u})
    for (unsigned window = 0; window < (bank == 1 ? 4u : 2u); ++window) {
      auto d = readGopWindow(riu_, bank, window, true);   // active latch AND enable bit required
      if (!d) continue;
      GopLayer l;
      l.bank = bank; l.window = window; l.x = d->x; l.y = d->y; l.width = d->width; l.height = d->height;
      l.pitch = d->pitch; l.format = d->format;
      auto physical = gopPhysicalAddress(riu_, bank, window, d->miuRelativeAddress);
      uint64_t size = uint64_t(d->pitch) * d->height;
      if (!physical || !size || size > 16 * 1024 * 1024 || uint64_t(*physical) + size > dramBacking_.size()) {
        l.skipReason = "physical address or size not resolvable";
      } else {
        l.physical = *physical;
        l.memory.assign(dramBacking_.begin() + *physical, dramBacking_.begin() + *physical + size);
        for (uint64_t i = 0; i < size; ++i) {
          auto address = *physical + uint32_t(i);
          l.invalidBytes += preservedBits_.memoryUnknown(address) || unknownObservations_.bytes().contains(address);
        }
      }
      layers.push_back(std::move(l));
    }
  return layers;
}
// Virtual remote delivery (emulator thread only). A host function becomes a decoded IR frame in the emulated PM IR FIFO; the original
// firmware then polls the FIFO itself. Events the receiver cannot take yet (firmware has not enabled it) wait, they are never dropped silently.
void Msd309Machine::deliverIrFunction(const std::string& function, uint64_t requestedAt) {
  int raw = -1;
  bool rawLiteral = function.rfind("raw=", 0) == 0;
  if (rawLiteral) raw = int(std::stoul(function.substr(4), nullptr, 0) & 255);
  else if (auto it = options_.irKeyMap.raw.find(function); it != options_.irKeyMap.raw.end()) raw = it->second;
  if (raw < 0) {
    irLog_.push_back({dispatches_, function, -1, false, "no raw IR byte is mapped for this function (no evidence-backed code yet)"});
    return;
  }
  try {
    bool ok = riu_.receivePmIrDecoded(uint8_t(raw), false);
    irLog_.push_back({dispatches_, function, raw, ok, ok ? "queued in the PM IR FIFO (requested at dispatch " + std::to_string(requestedAt) + ")" : "FIFO full"});
    event("virtual_remote_key", 0x1f007b51, 1, uint32_t(raw), "host function " + function + " delivered as a decoded IR frame; original firmware polls the FIFO");
  } catch (const std::exception& error) {
    if (irWaiting_.size() < 32) irWaiting_.push_back({function, requestedAt});
    irLog_.push_back({dispatches_, function, raw, false, std::string("deferred: ") + error.what()});
  }
}
void Msd309Machine::serviceVirtualRemote() {
  auto waiting = std::move(irWaiting_);
  irWaiting_.clear();
  for (auto& [function, at] : waiting) deliverIrFunction(function, at);
  while (!options_.irScript.empty() && options_.irScript.front().dispatch <= dispatches_) {
    auto item = options_.irScript.front();
    options_.irScript.erase(options_.irScript.begin());
    deliverIrFunction(item.function, item.dispatch);
  }
  if (options_.remoteQueue && options_.remoteQueue->pending())
    for (auto& function : options_.remoteQueue->drain()) deliverIrFunction(function, dispatches_);
}
void Msd309Machine::publishPreview() const {
  if (!options_.previewStore && options_.gopFrameHistory.empty()) return;
  if (!options_.offscreenInspector.empty()) {
    // DIAGNOSTIC offscreen inspector: read-only copy of a guest surface using the stated geometry; never a GOP window, never panel output.
    auto s = options_.offscreenInspector.front();
    GopLayer l;
    l.bank = 99; l.window = 0; l.x = 0; l.y = 0; l.width = s.width; l.height = s.height; l.pitch = s.pitch; l.format = 6;
    uint64_t size = uint64_t(s.pitch) * s.height;
    if (size && size <= 16 * 1024 * 1024 && uint64_t(s.address) + size <= dramBacking_.size()) {
      l.physical = s.address;
      l.memory.assign(dramBacking_.begin() + s.address, dramBacking_.begin() + s.address + size);
      for (uint64_t i = 0; i < size; ++i) {
        auto address = s.address + uint32_t(i);
        l.invalidBytes += preservedBits_.memoryUnknown(address) || unknownObservations_.bytes().contains(address);
      }
    } else l.skipReason = "surface address or size not resolvable";
    std::vector<GopLayer> layers;
    layers.push_back(std::move(l));
    auto off = composeGopPreview(layers);
    off.label = "OFFSCREEN SURFACE INSPECTOR (diagnostic, read-only) - guest surface " + hex(s.address) + "; NOT a GOP window, NOT panel output; ARGB1555 assumed";
    if (riu_.geUniformStretches || riu_.geI2Glyphs || riu_.geBlendCopies) off.label += " | EXPLORATORY GE models in use";
    options_.previewStore->publishOffscreen(std::move(off));
  }
  auto layers = collectGopLayers();
  auto frame = composeGopPreview(layers,1920,1080,options_.videoPlaneBlackKey && !options_.videoPlane.empty());
  if(options_.exploratoryGeHeight5Full)frame.label+=" | HYPOTHESIS: register 6F=height-5 copies full height";
  if(riu_.geBlendCopies)frame.label+=" | EXPLORATORY GE blend-copy model in use (not Titania4 behavior)";
  if(riu_.geI2Glyphs)frame.label+=" | EXPLORATORY GE I2-glyph model in use (not Titania4 behavior)";
  if(riu_.geUniformStretches)frame.label+=" | EXPLORATORY GE uniform-stretch model in use (alpha semantics are a hypothesis, not Titania4 behavior)";
  if(!options_.videoPlane.empty()){
    // EXPLORATORY: the emulator has no video plane. Pixels no enabled GOP window provides (or whose ARGB1555 alpha bit is clear) are shown in a
    // flat colour instead of the checker. Colour source: fixed black/blue, or the guest's own Blue Screen setting (original settings byte
    // 806FBD85 bit 4; verified to toggle with the menu item on the original firmware image only). No input signal exists, so this is the
    // no-signal background a TV would show there; it is not decoded video and not verified panel output.
    bool blue=options_.videoPlane=="blue";
    std::string why=options_.videoPlane;
    if(options_.videoPlane=="setting"){
      blue=false;why="setting: guest Blue Screen setting unreadable -> black";
      constexpr uint32_t setting=0x006fbd85;
      if(flash_.stockDigest()=="471e1f1c284e3c0fc9af68be0d8a79d0d635e0a1cca31066cd1e6df91e406d83" && dramBacking_.size()>setting && !preservedBits_.memoryUnknown(setting) && !unknownObservations_.bytes().contains(setting)){
        blue=(dramBacking_[setting]&0x10)!=0;why=std::string("setting: guest Blue Screen ")+(blue?"On -> blue":"Off -> black");
      }
    }
    frame.planeActive=true;frame.planeRgb=blue?0x000000ffu:0u;
    frame.label+=" | EXPLORATORY VIDEO PLANE (not emulated video; "+why+") behind the guest graphics";
    if(options_.videoPlaneBlackKey)frame.label+=" | HYPOTHESIS: opaque black guest pixels are colour-keyed transparent (unverified)";
  }
  if (!options_.gopFrameHistory.empty() && !frame.empty() && frame.checksum != historyChecksum_ && historyFrames_ < 128) {
    // Every distinct GOP content state, in guest-time order (diagnostic timeline; the window itself only shows the latest).
    historyChecksum_ = frame.checksum;
    auto name = options_.gopFrameHistory / ("gop_" + std::to_string(historyFrames_) + "_d" + std::to_string(dispatches_) + ".bmp");
    auto bmp = previewFrameToBmp32(frame);
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bmp.data()), std::streamsize(bmp.size()));
    historyIndex_ += (historyFrames_ ? ",\n    " : "") + std::string("{\"file\":\"") + name.filename().string() + "\",\"dispatches\":" + std::to_string(dispatches_) +
                     ",\"checksum\":\"" + hex(frame.checksum) + "\",\"covered_pixels\":" + std::to_string(std::count(frame.covered.begin(), frame.covered.end(), uint8_t(1))) + "}";
    ++historyFrames_;
  }
  if (!options_.previewStore) return;
  options_.previewStore->setGuestStatus("MIPS dispatches " + std::to_string(dispatches_));
  options_.previewStore->publish(std::move(frame));
}
// Debug export of every enabled + latched GOP window (never composed with a panel path).
void Msd309Machine::exportGopFrames() const {
  std::ostringstream j;
  j << "{\n  \"category\": \"host-side decode of guest framebuffer bytes; not a claim of television output\","
       "\n  \"authentic_frame\": false,\n  \"panel_composition_modeled\": false,\n  \"format6_is_argb1555_ASSUMED\": true,"
       "\n  \"stop_pc\": \"" << hex(blockingPc_) << "\",\n  \"windows\": [";
  bool first = true;
  for (auto& l : collectGopLayers()) {
    if (!first) j << ',';
    first = false;
    j << "\n    {\"bank\":" << l.bank << ",\"window\":" << l.window << ",\"x\":" << l.x << ",\"y\":" << l.y
      << ",\"width\":" << l.width << ",\"height\":" << l.height << ",\"pitch\":" << l.pitch << ",\"format\":" << l.format;
    if (!l.skipReason.empty()) { j << ",\"exported\":false,\"reason\":\"" << l.skipReason << "\"}"; continue; }
    uint64_t nonzero = 0;
    for (auto b : l.memory) nonzero += b != 0;
    j << ",\"physical_address\":\"" << hex(l.physical) << "\",\"source_sha256\":\"" << sha256(l.memory)
      << "\",\"nonzero_bytes\":" << nonzero << ",\"invalid_bytes\":" << l.invalidBytes;
    if (l.format != 6) { j << ",\"exported\":false,\"reason\":\"only format 6 (assumed ARGB1555) is decoded\"}"; continue; }
    if (l.invalidBytes) { j << ",\"exported\":false,\"reason\":\"window memory contains unknown bytes\"}"; continue; }
    auto image = argb1555WindowToBmp32(l.memory, l.pitch, l.width, l.height);
    auto name = options_.exportGopFrame;
    name.replace_extension();
    name += "_bank" + std::to_string(l.bank) + "_win" + std::to_string(l.window) + ".bmp";
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    f.flush();
    if (!f) throw std::runtime_error("cannot write GOP frame export");
    j << ",\"exported\":true,\"bmp\":\"" << name.filename().string() << "\",\"bmp_sha256\":\"" << sha256(image) << "\"}";
  }
  j << "\n  ],\n  \"gop_register_banks_note\": \"guest-programmed GOP register bytes; offset=value, '/kMM' = partially known mask; shadow = written, active = latched\",\n  \"gop_register_banks\": {";
  bool firstBank = true;
  for (unsigned bank = 0; bank < 16; ++bank) {
    std::ostringstream shadow, active;
    bool any = false, anyActive = false;
    auto hex2 = [](unsigned v) { char t[8]; snprintf(t, sizeof t, "%02X", v & 255); return std::string(t); };
    for (unsigned offset = 0; offset < 0x1fc; ++offset) {
      auto b = riu_.gopByte(bank, offset);
      if (!b.known) continue;
      char t[16]; snprintf(t, sizeof t, "%03X=", offset);
      shadow << (any ? " " : "") << t << hex2(b.value & b.known) << (b.known == 255 ? "" : "/k" + hex2(b.known));
      any = true;
    }
    for (unsigned offset = 0; offset < 0x1fc; ++offset) {
      auto b = riu_.gopActiveByte(bank, offset);
      if (!b.known) continue;
      char t[16]; snprintf(t, sizeof t, "%03X=", offset);
      active << (anyActive ? " " : "") << t << hex2(b.value & b.known) << (b.known == 255 ? "" : "/k" + hex2(b.known));
      anyActive = true;
    }
    if (!any && !anyActive) continue;
    if (!firstBank) j << ',';
    firstBank = false;
    j << "\n    \"" << bank << "\": {\"shadow\": \"" << shadow.str() << "\", \"active\": \"" << active.str() << "\"}";
  }
  j << "\n  }\n}\n";
  std::ofstream meta(options_.exportGopFrame);
  meta << j.str();
  meta.flush();
  if (!meta) throw std::runtime_error("cannot write GOP frame export metadata");
}
void Msd309Machine::dumpGuestRanges() const {
  for (auto [address, length] : options_.dumpRanges) {
    if (!length || uint64_t(address) + length > dramBacking_.size()) throw std::runtime_error("guest range dump outside mapped DRAM");
    auto name = options_.dumpPrefix;
    name += "_" + hex(address) + ".bin";
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(dramBacking_.data() + address), std::streamsize(length));
    f.flush();
    if (!f) throw std::runtime_error("cannot write guest range dump");
  }
}
std::string Msd309Machine::summary() const {
  if (!options_.diagnosticDramDumpFile.empty() && uint64_t(options_.diagnosticDramDumpAddr) + options_.diagnosticDramDumpSize <= dramBacking_.size()) {
    std::ofstream d(options_.diagnosticDramDumpFile, std::ios::binary);
    d.write(reinterpret_cast<const char*>(dramBacking_.data()) + options_.diagnosticDramDumpAddr, std::streamsize(options_.diagnosticDramDumpSize));
  }
  flushStats();
  const_cast<Msd309Machine *>(this)->flushTicks();
  std::ostringstream s;
  s << "{\n  \"mode\": \""
    << (options_.stage2 ? "diagnostic second-stage; reset initialization bypassed"
                        : "firmware from reset flash")
    << "\",\n  \"symbolic_init\": " << (options_.symbolicInit ? "true" : "false")
    << ",\n  \"candidate_bdma\": " << (options_.candidateBdma ? "true" : "false")
    << ",\n  \"diagnostic_init_prefix\": " << (options_.initPrefix ? "true" : "false")
    << ",\n  \"candidate_uart\": " << (options_.candidateUart ? "true" : "false")
    << ",\n  \"gpio_i2c\": " << (options_.gpioI2c ? "true" : "false")
    << ",\n  \"candidate_miu\": " << (options_.candidateMiu ? "true" : "false")
    << ",\n  \"deferred_diagnostic_reads\": " << deferredReads_
    << ",\n  \"candidate_spi\": " << (options_.candidateSpi ? "true" : "false")
    << ",\n  \"candidate_semaphore\": " << (options_.candidateSemaphore ? "true" : "false")
    << ",\n  \"exploratory_semaphore_idle\": " << (options_.exploratorySemaphoreIdle ? "true" : "false")
    << ",\n  \"semaphore_acquisitions\": " << riu_.semaphoreAcquisitions()
    << ",\n  \"semaphore_releases\": " << riu_.semaphoreReleases()
    << ",\n  \"semaphore_contentions\": " << riu_.semaphoreContentions()
    << ",\n  \"candidate_chip_id\": " << (options_.candidateChipId ? "true" : "false")
    << ",\n  \"spi_flash_model\": \"" << (options_.flashModel == NorModel::W25Q64BV ? "w25q64bv" : "mx25l6405d") << "\""
    << ",\n  \"spi_unknown_input_policy\": " << unsigned(options_.spiUnknownInput)
    << ",\n  \"spi_undocumented_samples\": " << riu_.undocumentedSpiSamples()
    << ",\n  \"spi_nor_programs\": " << riu_.spiIsp().nor.programs
    << ",\n  \"spi_nor_erases\": " << riu_.spiIsp().nor.erases
    << ",\n  \"spi_nor_programmed_bytes\": " << riu_.spiIsp().nor.programmedBytes
    << ",\n  \"spi_nor_ignored_writes\": " << riu_.spiIsp().nor.ignoredWrites
    << ",\n  \"spi_nor_zero_to_one_requests\": " << riu_.spiIsp().nor.norViolations
    << ",\n  \"flash_overlay_sectors\": " << flash_.overlaySectors()
    << ",\n  \"flash_overlay_sha256\": \"" << flash_.overlayDigest() << "\""
    << ",\n  \"flash_stock_sha256\": \"" << flash_.stockDigest() << "\""
    << ",\n  \"spi_jedec_read_returned\": " << (spiJedecRead_ ? "true" : "false")
    << ",\n  \"spi_descriptor_selected\": " << (spiDescriptorSelected_ ? "true" : "false")
    << ",\n  \"spi_jedec\": [" << unsigned(spiJedec_[0]) << ',' << unsigned(spiJedec_[1]) << ',' << unsigned(spiJedec_[2]) << ']'
    << ",\n  \"candidate_utmi\": " << (options_.candidateUtmi ? "true" : "false")
    << ",\n  \"virtual_rtc\": " << (options_.virtualRtc ? "true" : "false")
    << ",\n  \"cp0_timer\": " << (options_.cp0Timer ? "true" : "false")
    << ",\n  \"guest_exception_vector_entries\": " << timerEntries_
    << ",\n  \"defer_diagnostic_reads_enabled\": "
    << (options_.deferDiagnosticReads ? "true" : "false")
    << ",\n  \"i2c_sda_stuck_fixture\": " << (options_.i2cSdaStuck ? "true" : "false")
    << ",\n  \"max_unchanged_mmio_poll\": " << maxPoll_ << ",\n  \"guest_decompressed_sha256\": \""
    << guestDecompressedSha_ << "\""
    << ",\n  \"instruction_dispatches\": " << dispatches_
    << ",\n  \"unique_pcs\": " << coverage_.size() << ",\n  \"mmio_events\": " << mmio_
    << ",\n  \"flash_load_dispatches\": " << flashReads_
    << ",\n  \"dram_store_dispatches\": " << dramWrites_ << ",\n  \"stop_pc\": \""
    << hex(blockingPc_) << "\",\n  \"stop_address\": \"" << hex(blockingAddress_)
    << "\",\n  \"reason\": \"" << reason_
    << "\",\n  \"second_stage_reached\": " << (stage2Reached_ ? "true" : "false")
    << ",\n  \"guest_inflate_entered\": " << (decompressReached_ ? "true" : "false")
    << ",\n  \"runtime_entry_reached\": " << (runtimeReached_ ? "true" : "false")
    << ",\n  \"osd_demonstrated\": false,\n  \"last_pcs\": [";
  bool first = true;
  for (auto pc : history_) {
    if (!first)
      s << ',';
    first = false;
    s << '"' << hex(pc) << '"';
  }
  s << "],\n  \"registers\": [";
  for (int i = 0; i < 32; ++i) {
    if (i)
      s << ',';
    s << '"' << hex(cpu_.reg(UC_MIPS_REG_0 + i)) << '"';
  }
  s << "],\n  \"stack_words\": [";
  auto sp = cpu_.reg(UC_MIPS_REG_SP) & 0x1fffffff;
  if (sp < 0x08000000 - 64) {
    uint32_t words[16]{};
    cpu_.read(sp, words, sizeof(words));
    for (int i = 0; i < 16; ++i) {
      if (i)
        s << ',';
      s << '"' << hex(words[i]) << '"';
    }
  }
  s << "],\n  \"hot_pcs\": [";
  std::vector<std::pair<uint32_t, uint64_t>> hot(pcHits_.begin(), pcHits_.end());
  std::sort(hot.begin(), hot.end(), [](auto &a, auto &b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  for (size_t i = 0; i < std::min(size_t(10), hot.size()); ++i) {
    if (i)
      s << ',';
    s << "{\"pc\":\"" << hex(hot[i].first) << "\",\"dispatches\":" << hot[i].second << '}';
  }
#ifdef UC_MIPS_MANTA_TIMER_API
  s << "],\n  \"cp0_count\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_COUNT))
    << "\",\n  \"cp0_compare\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_COMPARE))
    << "\",\n  \"cp0_cause\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_CAUSE)) << "\",\n  \"cp0_epc\":\""
    << hex(cpu_.reg(UC_MIPS_REG_CP0_EPC)) << "\"";
#else
  s << "]";
#endif
  uint32_t softwareTick = 0;
  cpu_.read(0x006e561c, &softwareTick, 4);
  s << ",\n  \"guest_software_tick\": " << softwareTick
    << ",\n  \"gop_initializer_entered\": " << (gopInitializerEntered_ ? "true" : "false")
    << ",\n  \"gop_initializer_entries\": " << gopInitializerEntries_
    << ",\n  \"scaler_initializer_entries\": " << scalerInitializerEntries_
    << ",\n  \"scaler_initializer_returns\": " << scalerInitializerReturns_
    << ",\n  \"dlc_initializer_returns\": " << dlcInitializerReturns_
    << ",\n  \"preserved_bit_unknown_ram_bytes\": " << preservedBits_.unknownMemoryBytes()
    << ",\n  \"exploratory_sc_initial_bank\": " << options_.exploratoryScBank
    << ",\n  \"candidate_gamma\": " << (options_.candidateGamma ? "true" : "false")
    << ",\n  \"exploratory_gamma_initial_enable\": " << options_.exploratoryGammaEnable
    << ",\n  \"gamma_ram_commits\": " << riu_.gammaCommits()
    << ",\n  \"exploratory_display_clock_hz\": " << options_.exploratoryDisplayClock
    << ",\n  \"exploratory_subwindow_initial_enable\": " << options_.exploratorySubwindowEnable
    << ",\n  \"exploratory_dlc_initial_pending\": " << options_.exploratoryDlcPending
    << ",\n  \"exploratory_dlc_timeout\": " << (options_.exploratoryDlcTimeout ? "true":"false")
    << ",\n  \"exploratory_comb_initial_bank\": " << options_.exploratoryCombBank
    << ",\n  \"exploratory_gop_initial_bank\": " << options_.exploratoryGopBank
    << ",\n  \"exploratory_comb_paired_restore\": " << (options_.exploratoryCombRestore?"true":"false")
    << ",\n  \"comb_paired_restores\": " << riu_.combRestoreCount()
    << ",\n  \"comb_paired_restore_pending\": " << (riu_.combRestorePending()?"true":"false")
    << ",\n  \"virtual_output_frames\": " << riu_.displayFrames(virtualTime_.cycles(dispatches_))
    << ",\n  \"output_sync_reads\": " << riu_.outputSyncReads()
    << ",\n  \"output_sync_observed_transitions\": " << riu_.outputSyncTransitions()
    << ",\n  \"gop_aperture_events\": " << gopEvents_
    << ",\n  \"ge_candidate_aperture_events\": " << geEvents_
    << ",\n  \"ge_blocked_aperture_events\": " << geBlockedEvents_;
  s << ",\n  \"candidate_ge\": " << (options_.candidateGe?"true":"false")
    << ",\n  \"exploratory_ge_reset\": " << (options_.exploratoryGeReset?"true":"false")
    << ",\n  \"ge_parameter_submissions\": " << riu_.geSubmitted()
    << ",\n  \"ge_parameter_transfers\": " << riu_.geTransferred()
    << ",\n  \"ge_parameter_retirements\": " << riu_.geCompleted();
  if(options_.exploratoryAvdNoSyncEvents)s<<",\n  \"exploratory_avd_no_sync_events\": true,\n  \"avd_event_acks\": "<<riu_.avdMcuBus().eventAcks;
  if(options_.diagnosticAvd11dbBit7>=0)s<<",\n  \"diagnostic_avd_11db_bit7\": "<<options_.diagnosticAvd11dbBit7;
  if(!options_.watchData.empty()){
    s<<",\n  \"watched_unknown_bit_origins\": [";bool firstWo=true;
    for(auto& [wa,wl]:options_.watchData)for(uint32_t k=0;k<wl && k<64;++k){
      auto d=preservedBits_.describeMemory(wa+k);if(d.empty())continue;
      if(!firstWo)s<<',';firstWo=false;
      s<<"\n    {\"address\":\""<<hex(wa+k)<<"\",\"origins\":\""<<d<<"\"}";
    }
    s<<"\n  ]";
  }
  if(!dataAccess_.empty()){
    s<<",\n  \"watched_data_accesses\": [";bool firstDa=true;
    for(auto& [key,e]:dataAccess_){
      if(!firstDa)s<<',';firstDa=false;
      s<<"\n    {\"pc\":\""<<hex(uint32_t(key>>1))<<"\",\"kind\":\""<<((key&1)?"store":"load")<<"\",\"count\":"<<e.count<<",\"first_dispatch\":"<<e.firstDispatch<<",\"address\":\""<<hex(e.address)<<"\",\"ra\":\""<<hex(e.ra)<<"\",\"first_value\":\""<<hex(e.firstValue)<<"\",\"last_value\":\""<<hex(e.lastValue)<<"\"}";
    }
    s<<"\n  ]";
  }
  if(!watchHits_.empty()){
    s<<",\n  \"watched_pc_hits\": {";bool firstPc=true;
    for(auto& [pc,entry]:watchHits_){
      if(!firstPc)s<<',';firstPc=false;
      s<<"\n    \""<<hex(pc)<<"\": {\"count\": "<<entry.first<<", \"hits\": [";
      bool firstHit=true;
      for(auto& hit:entry.second){
        if(!firstHit)s<<',';firstHit=false;
        s<<"{\"dispatch\":"<<hit.dispatch<<",\"a0\":\""<<hex(hit.reg[4])<<"\",\"a1\":\""<<hex(hit.reg[5])<<"\",\"a2\":\""<<hex(hit.reg[6])<<"\",\"a3\":\""<<hex(hit.reg[7])
         <<"\",\"v0\":\""<<hex(hit.reg[2])<<"\",\"v1\":\""<<hex(hit.reg[3])<<"\",\"s0\":\""<<hex(hit.reg[16])<<"\",\"s1\":\""<<hex(hit.reg[17])<<"\",\"s2\":\""<<hex(hit.reg[18])<<"\",\"ra\":\""<<hex(hit.reg[31])<<"\",\"sp\":\""<<hex(hit.reg[29])<<"\",\"stack\":[";
        for(unsigned i=0;i<8;++i)s<<(i?",":"")<<'"'<<hex(hit.stack[i])<<'"';
        s<<"]}";
      }
      s<<"]}";
    }
    s<<"\n  }";
  }
  if(options_.exploratoryAfecIndirect>=0)s<<",\n  \"exploratory_afec_indirect_6a3c_initial\": "<<options_.exploratoryAfecIndirect<<",\n  \"afec_indirect_reads\": "<<riu_.afecIndirectReads()<<",\n  \"afec_indirect_writes\": "<<riu_.afecIndirectWrites()<<",\n  \"afec_indirect_final_value\": "<<riu_.afecIndirectValue();
  if(!options_.bitHypotheses.empty()){
    s<<",\n  \"exploratory_mmio_bits\": [";
    bool firstBits=true;
    for(const auto& h:riu_.hypothesisBits()){
      if(!firstBits)s<<',';firstBits=false;
      s<<"{\"address\":\""<<hex(h.address)<<"\",\"mask\":"<<unsigned(h.mask)<<",\"value\":"<<unsigned(h.value)<<",\"reads\":"<<h.reads<<'}';
    }
    s<<']';
  }
  if(!riu_.gopLog.empty() && !options_.gopFrameHistory.empty()){
    // bank 0/3/4 writes and every latch from --gop-timeline-from (at most 900 entries)
    s<<",\n  \"gop_timeline_tail\": [";
    size_t first=0,printed=0;bool firstEntry=true;
    for(size_t i=first;i<riu_.gopLog.size();++i){
      const auto& e=riu_.gopLog[i];
      if(e.tick<options_.gopTimelineFrom || printed>=900)continue;
      ++printed;
      if(e.kind=='W' && e.bank!=4)continue;
      if(!firstEntry)s<<',';firstEntry=false;
      s<<"\n    {\"tick\":"<<e.tick<<",\"k\":\""<<e.kind<<"\"";
      if(e.kind=='W')s<<",\"pc\":\""<<hex(e.pc)<<"\",\"bank\":"<<e.bank<<",\"off\":\""<<hex(e.offset)<<"\",\"val\":\""<<hex(e.value)<<"\",\"n\":"<<e.n;
      s<<'}';
    }
    s<<"\n  ]";
  }
  if(options_.exploratoryPmIrRptLatch)s<<",\n  \"exploratory_pm_ir_rpt_latch\": true";
  if(options_.candidatePmIrFifo)s<<",\n  \"pm_ir_fifo_pops\": "<<riu_.pmIrFifo().pops()<<",\n  \"pm_ir_fifo_received\": "<<riu_.pmIrFifo().received();
  if(!irLog_.empty()){
    s<<",\n  \"virtual_remote_log\": [";
    for(size_t i=0;i<irLog_.size();++i){
      const auto& e=irLog_[i];
      s<<(i?",":"")<<"\n    {\"dispatch\":"<<e.dispatch<<",\"function\":\""<<e.function<<"\",\"raw\":"<<e.raw<<",\"accepted\":"<<(e.accepted?"true":"false")<<",\"note\":\""<<e.note<<"\"}";
    }
    s<<"\n  ]";
  }
  if(options_.exploratoryAvdNoise>=0)s<<",\n  \"exploratory_avd_noise_constant\": "<<options_.exploratoryAvdNoise<<",\n  \"exploratory_avd_noise_reads\": "<<riu_.avdNoiseReads();
  if(options_.exploratoryAvdHtotal>=0)s<<",\n  \"exploratory_avd_htotal_constant\": "<<options_.exploratoryAvdHtotal<<",\n  \"exploratory_avd_htotal_reads\": "<<riu_.avdHtotalReads();
  if(!riu_.guardedStretchDetail().empty())s<<",\n  \"ge_guarded_stretch_request\": \""<<riu_.guardedStretchDetail()<<"\"";
  if(!snapshotOptionDelta_.empty())s<<",\n  \"snapshot_option_delta\": \""<<snapshotOptionDelta_<<"\"";
  if(options_.stateDigest){
    auto sections=const_cast<Msd309Machine*>(this)->captureSections(cpu_.reg(UC_MIPS_REG_PC));
    std::string all;
    s<<",\n  \"state_digest\": {";
    bool firstSection=true;
    for(auto& sec:sections){
      auto d=sha256(sec.bytes);all+=sec.name+d;
      s<<(firstSection?"":", ")<<"\""<<sec.name<<"\": \""<<d<<"\"";firstSection=false;
    }
    s<<", \"overall\": \""<<sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(all.data()),all.size()))<<"\"}";
  }
  if(options_.exploratoryGeYuvStretch)s<<",\n  \"exploratory_ge_yuv_stretch\": true,\n  \"ge_yuv_stretches_retired\": "<<riu_.geYuvStretches<<",\n  \"ge_yuv_stretch_pixels_written\": "<<riu_.geYuvStretchPixels;
  if(options_.exploratoryGeNearestStretch)s<<",\n  \"exploratory_ge_nearest_stretch\": true,\n  \"ge_nearest_stretches_retired\": "<<riu_.geNearestStretches;
  if(options_.exploratoryGeUniformStretch>=0)s<<",\n  \"exploratory_ge_uniform_stretch\": \""<<(options_.exploratoryGeUniformStretch==0?"opaque-source":"alpha-bit")<<"\",\n  \"ge_uniform_stretches_retired\": "<<riu_.geUniformStretches<<",\n  \"ge_uniform_stretch_pixels_written\": "<<riu_.geUniformStretchPixels;
  if(options_.diagnosticRiuWriteLogFrom!=UINT64_MAX){
    s<<",\n  \"diagnostic_riu_write_log\": [";
    bool f=true;for(auto& kv:riu_.writeLog){s<<(f?"":", ")<<"[\""<<hex(kv.first)<<"\", "<<kv.second.count<<", \""<<hex(kv.second.firstPc)<<"\", \""<<hex(kv.second.last)<<"\"]";f=false;}
    s<<"]";
  }
  if(options_.exploratoryRiuZeroUnknown){
    s<<",\n  \"diagnostic_riu_zero_unknown_reads\": {\"total\": "<<riu_.zeroUnknownTotal<<", \"addresses\": [";
    bool f=true;for(auto& kv:riu_.zeroUnknownLog){s<<(f?"":", ")<<"[\""<<hex(kv.first)<<"\", "<<kv.second<<"]";f=false;}
    s<<"]}";
  }
  if(options_.exploratoryCopro){
    s<<",\n  \"exploratory_copro\": {\"started\": "<<(copro_.started?"true":"false")<<", \"stopped\": "<<(copro_.stopped?"true":"false")
     <<", \"stop_reason\": \""<<jsonText(copro_.stopReason)<<"\", \"instructions\": "<<copro_.instructions()<<", \"pc\": \""<<hex(copro_.pc())
     <<"\", \"jpd\": {\"starts\": "<<copro_.jpd.starts<<", \"completed\": "<<copro_.jpd.completed<<", \"blocks\": "<<copro_.jpd.blocks<<", \"note\": \""<<jsonText(copro_.jpd.lastNote)<<"\"}, \"fiq_raises\": "<<riu_.fiqRaises<<", \"fiq_status_reads\": "<<riu_.fiqStatusReads<<", \"fiq_status_writes\": "<<riu_.fiqStatusWrites<<", \"fiq_line_changes\": "<<riu_.fiqLineChanges<<", \"fiq_pending_bank1\": "<<riu_.fiqPending[1]<<", \"fiq_pending_bank3\": "<<riu_.fiqPending[3]<<", \"fiq_mask_words\": ["<<[&]{std::string o;for(uint32_t base:{0x1f203390u,0x1f203394u,0x1f203398u,0x1f20339cu}){uint8_t b0=0,b1=0;bool k=riu_.coproPeekByte(base,b0)&&riu_.coproPeekByte(base+1,b1);o+=(o.empty()?"":", ");o+=k?std::to_string(b0|(b1<<8)):std::string("-1");}return o;}()<<"], \"starts\": "<<copro_.starts<<", \"holds\": "<<copro_.holds<<", \"mailbox_irqs\": "<<copro_.mailboxIrqs<<", \"host_notify_writes\": "<<copro_.hostNotifies<<", \"latched_unknown_effect_writes\": "<<copro_.latchedWrites
     <<", \"zero_unknown_reads\": "<<(options_.exploratoryCoproZeroUnknown?"true":"false")<<", \"unknown_read_total\": "<<copro_.unknownReadTotal<<", \"unknown_reads_first_512\": [";
    bool firstUnknown=true;
    for(auto& kv:copro_.unknownReads){s<<(firstUnknown?"":", ")<<"[\""<<hex(kv.first)<<"\", "<<kv.second<<"]";firstUnknown=false;}
    s<<"], \"watch_notes\": [";
    for(size_t i=0;i<copro_.notes.size();++i)s<<(i?", ":"")<<"\""<<jsonText(copro_.notes[i])<<"\"";
    s<<"], \"seq\": ["; for(size_t i=0;i<copro_.seq.size();++i){auto& e=copro_.seq[i];s<<(i?",":"")<<"["<<e[0]<<","<<e[1]<<","<<e[2]<<","<<e[3]<<"]";}
    s<<"], \"captured_writes\": ["; for(size_t i=0;i<copro_.captured.size();++i)s<<(i?",":"")<<copro_.captured[i];
    s<<"], \"io_log\": ["; {bool f=true; for(auto& kv:copro_.ioLog){s<<(f?"":", ")<<"[\""<<hex(kv.first)<<"\", "<<kv.second.reads<<", "<<kv.second.writes<<", \""<<hex(kv.second.last)<<"\", \""<<hex(kv.second.firstPc)<<"\"]"; f=false;}}
    s<<"], \"mail_log\": [";
    for(size_t i=0;i<copro_.mailLog.size();++i)s<<(i?", ":"")<<"\""<<jsonText(copro_.mailLog[i])<<"\"";
    s<<"], \"uart\": \""<<jsonText(copro_.uart)<<"\"}";
  }
  if(!options_.exploratoryUsbPendrive.empty()){
    auto& u=riu_.usbHost;
    s<<",\n  \"exploratory_usb_pendrive\": {\"transactions\": "<<u.transactions<<", \"qtds_retired\": "<<u.qtdsRetired<<", \"async_scans\": "<<u.scans;
    if(auto* d=u.device())s<<", \"scsi_commands\": "<<d->commands<<", \"blocks_read\": "<<d->readBlocks<<", \"blocks_written\": "<<d->writtenBlocks;
    s<<", \"trace\": [";
    bool first=true;
    for(auto& t:u.trace){s<<(first?"":", ")<<"\""<<t<<"\"";first=false;}
    if(auto* d=u.device())for(auto& t:d->log){s<<(first?"":", ")<<"\""<<t<<"\"";first=false;}
    s<<"]}";
  }
  if(options_.exploratoryGeLine)s<<",\n  \"exploratory_ge_line\": true,\n  \"ge_lines_retired\": "<<riu_.geLines<<",\n  \"ge_line_pixels_written\": "<<riu_.geLinePixels;
  if(options_.exploratoryGeGradientFill)s<<",\n  \"exploratory_ge_gradient_fill\": true,\n  \"ge_gradient_fills_retired\": "<<riu_.geGradientFills;
  if(options_.exploratoryGeFillIgnoredReg00)s<<",\n  \"exploratory_ge_fill_ignored_reg00_bits\": "<<options_.exploratoryGeFillIgnoredReg00;
  if(options_.exploratoryGeI8Fill)s<<",\n  \"exploratory_ge_i8_fill\": true,\n  \"ge_i8_fills_retired\": "<<riu_.geI8Fills<<",\n  \"ge_i8_fill_pixels_written\": "<<riu_.geI8FillPixels;
  if(options_.exploratoryGeBlendCopy && !riu_.geBlendCopyLog.empty()){
    s<<",\n  \"ge_blend_copy_log\": [";
    for(size_t i=0;i<riu_.geBlendCopyLog.size();++i)s<<(i?",\n    ":"\n    ")<<riu_.geBlendCopyLog[i];
    s<<"\n  ]";
  }
  if(options_.exploratoryGeCopySnapshot)s<<",\n  \"exploratory_ge_copy_snapshot\": true";
  if(options_.exploratoryGeHeight5Full)s<<",\n  \"exploratory_ge_height5_full_copy\": true";
  if(options_.exploratoryGeBlendCopy)s<<",\n  \"exploratory_ge_blend_copy\": true,\n  \"ge_blend_copies_retired\": "<<riu_.geBlendCopies<<",\n  \"ge_blend_copy_pixels_written\": "<<riu_.geBlendCopyPixels;
  if(options_.exploratoryGeI2Glyph)s<<",\n  \"exploratory_ge_i2_glyph\": true,\n  \"ge_i2_glyphs_retired\": "<<riu_.geI2Glyphs<<",\n  \"ge_i2_glyph_pixels_written\": "<<riu_.geI2GlyphPixels;
  if(options_.diagnosticVifCrLock>=0)s<<",\n  \"diagnostic_vif_cr_lock\": "<<options_.diagnosticVifCrLock;
  if(options_.exploratoryUsbNoDevice)s<<",\n  \"exploratory_usb_no_device\": true";
  if(options_.diagnosticHtotalBranchJoin)s<<",\n  \"diagnostic_htotal_branch_join\": true,\n  \"diagnostic_htotal_branch_joins\": "<<htotalJoins_;
  if(options_.candidateGeBitBlt)s<<",\n  \"candidate_ge_bitblt\": true"
    <<",\n  \"ge_bitblts_retired\": "<<riu_.geBitBlts
    <<",\n  \"ge_bitblt_pixels_written\": "<<riu_.geBitBltPixels;
  if(options_.candidateGeRectFill)s<<",\n  \"candidate_ge_rect_fill\": true"
    <<",\n  \"ge_rectangles_retired\": "<<riu_.geRectangles
    <<",\n  \"ge_raster_pixels_written\": "<<riu_.geRasterPixels;
  s << ",\n  \"candidate_gop_palette\": " << (options_.candidateGopPalette?"true":"false")
    << ",\n  \"gop_palette_commits\": " << riu_.paletteCommits()
    << ",\n  \"gop_palette_defined_entries\": [" << riu_.paletteDefined(0) << ',' << riu_.paletteDefined(1) << ']';
  s << ",\n  \"candidate_tsp\": " << (options_.candidateTsp?"true":"false")
    << ",\n  \"exploratory_tsp_cpu\": " << (options_.exploratoryTspCpu?"true":"false")
    << ",\n  \"or1k_instructions\": " << riu_.tspCpuInstructions()
    << ",\n  \"or1k_pc\": \"" << hex(riu_.tspCpuPc()) << "\""
    << ",\n  \"or1k_data_reads\": " << riu_.tspCpuReads()
    << ",\n  \"or1k_data_writes\": " << riu_.tspCpuWrites()
    << ",\n  \"or1k_running\": " << (riu_.tspCpuRunning()?"true":"false")
    << ",\n  \"exploratory_tsp_reset_low\": " << (options_.exploratoryTspResetLow?"true":"false")
    << ",\n  \"tsp_queue_known\": " << (riu_.tspQueueKnown()?"true":"false")
    << ",\n  \"tsp_reset_held\": " << (riu_.tspResetHeld()?"true":"false")
    << ",\n  \"tsp_reset_assertions\": " << riu_.tspResetCount()
    << ",\n  \"tsp_reset_releases\": " << riu_.tspResetReleases()
    << ",\n  \"tsp_filein_submissions\": " << riu_.tspSubmissions()
    << ",\n  \"tsp_indirect_reads\": " << riu_.tspIndirectReads()
    << ",\n  \"tsp_indirect_writes\": " << riu_.tspIndirectWrites()
    << ",\n  \"tsp_firmware_download_requests\": " << riu_.tspDownloadRequests()
    << ",\n  \"tsp_firmware_download_pending\": " << (riu_.tspDownloadPending()?"true":"false")
    << ",\n  \"tsp_firmware_download_completions\": " << riu_.tspDownloadCompletions()
    << ",\n  \"tsp_firmware_download_bytes\": " << riu_.tspDownloadBytes()
    << ",\n  \"tsp_firmware_download_source\": \"" << hex(riu_.tspDownloadSource()) << "\""
    << ",\n  \"tsp_queue_count\": ";
  if(riu_.tspQueueKnown())s<<riu_.tspQueueCount();else s<<"null";
  if(options_.deferPlatformIdentity)s<<",\n  \"deferred_platform_identity_enabled\": true";
  if(options_.exploratoryPmAcOn)s<<",\n  \"exploratory_pm_ac_on\": true";
  if(options_.exploratoryScartGrounded)s<<",\n  \"exploratory_scart_grounded\": true,\n  \"scart_input_reads\": "<<riu_.scartReads();
  if(options_.exploratoryScmiFormat>=0)s<<",\n  \"exploratory_scmi_format_byte\": "<<options_.exploratoryScmiFormat;
  if(options_.exploratoryFetchRevisionByte>=0)s<<",\n  \"exploratory_fetch_revision_byte\": "<<options_.exploratoryFetchRevisionByte
    <<",\n  \"fetch_patch_enabled_decisions\": "<<fetchPatchEnabled_<<",\n  \"fetch_patch_disabled_decisions\": "<<fetchPatchDisabled_;
  if(options_.exploratoryAnalogDisconnected)s<<",\n  \"exploratory_analog_disconnected\": true";
  if(options_.exploratoryIrCapturePresent)s<<",\n  \"exploratory_ir_capture_activity\": true,\n  \"ir_capture_activity_reads\": "<<riu_.irCaptureStatusReads()
    <<",\n  \"ir_capture_activity_hardware_verified\": false";
  if(options_.candidatePmIrFifo){
    s<<",\n  \"candidate_pm_ir_fifo\": true,\n  \"exploratory_pm_ir_reset_empty\": "<<(options_.exploratoryPmIrResetEmpty?"true":"false")
     <<",\n  \"pm_ir_fifo_status_reads\": "<<riu_.pmIrStatusReads()<<",\n  \"pm_ir_fifo_guest_clears\": "<<riu_.pmIrFifo().clears()
     <<",\n  \"pm_ir_fifo_pops\": "<<riu_.pmIrFifo().pops()<<",\n  \"pm_ir_fifo_external_packets\": "<<riu_.pmIrFifo().received();
  }
  if(options_.exploratoryAvdNoHsync)s<<",\n  \"exploratory_avd_no_hsync\": true,\n  \"avd_absent_hsync_reads\": "<<riu_.avdHsyncReads()
    <<",\n  \"avd_detector_timing_hardware_verified\": false";
  if(options_.exploratoryVifRevision>=0)s<<",\n  \"exploratory_vif_chip_revision_byte\": "<<options_.exploratoryVifRevision
    <<",\n  \"vif_chip_revision_reads\": "<<riu_.vifRevisionReads()<<",\n  \"vif_chip_revision_hardware_verified\": false";
  if(options_.exploratoryVifZeroInputSettlingCycles)s<<",\n  \"exploratory_vif_ideal_zero_energy\": true"
    <<",\n  \"vif_zero_input_settle_cycles\": "<<riu_.vifZeroInput().settlingCycles()
    <<",\n  \"vif_zero_input_settlements\": "<<riu_.vifZeroInput().settlements()
    <<",\n  \"vif_zero_input_samples\": "<<riu_.vifZeroInputReads()
    <<",\n  \"vif_zero_input_state_sha256\": \""<<sha256(riu_.vifZeroInput().stateBytes())<<"\""
    <<",\n  \"vif_disconnected_antenna_hardware_verified\": false";
  if(options_.exploratoryCiDetectHigh)s<<",\n  \"exploratory_ci_detect_high\": true,\n  \"ci_detect_reads\": "<<riu_.ciDetectReads;
  if(options_.exploratoryPqSramIdle)s<<",\n  \"exploratory_pq_sram_idle\": true,\n  \"pq_sram_submissions\": "<<riu_.pqSram().submissions<<",\n  \"pq_sram_commits\": "<<riu_.pqSram().commits<<",\n  \"pq_sram_state_sha256\": \""<<sha256(riu_.pqSram().stateBytes())<<"\"";
  if(options_.exploratoryPqDisabledPorts)s<<",\n  \"exploratory_pq_disabled_ports\": true";
  if(options_.candidateVdMcuProgram){
    auto image=riu_.vdMcuProgram().contiguousImage();
    s<<",\n  \"candidate_vd_mcu_program\": true,\n  \"vd_mcu_program_writes\": "<<riu_.vdMcuProgram().writes
      <<",\n  \"vd_mcu_program_reads\": "<<riu_.vdMcuProgram().reads<<",\n  \"vd_mcu_start_requests\": "<<riu_.vdMcuProgram().startRequests
      <<",\n  \"vd_mcu_contiguous_program_bytes\": "<<image.size()<<",\n  \"vd_mcu_program_sha256\": \""<<sha256(image)<<"\"";
    if(options_.exploratoryVdSuspendedBoundary)s<<",\n  \"mcu_mailbox_interrupt_requests\": "<<riu_.vdMcuProgram().interruptRequests
      <<",\n  \"mcu_mailbox_interrupt_pending\": "<<(riu_.vdMcuProgram().interruptPending?"true":"false");
  }
  if(options_.candidateAvdMcuProgram){
    auto image=riu_.avdMcuProgram().contiguousImage();
    s<<",\n  \"candidate_avd_mcu_program\": true,\n  \"avd_mcu_program_writes\": "<<riu_.avdMcuProgram().writes
      <<",\n  \"avd_mcu_program_reads\": "<<riu_.avdMcuProgram().reads
      <<",\n  \"avd_mcu_start_requests\": "<<riu_.avdMcuProgram().startRequests
      <<",\n  \"avd_mcu_program_bytes\": "<<image.size()<<",\n  \"avd_mcu_program_sha256\": \""<<sha256(image)<<"\""
      <<",\n  \"avd_mcu_instructions\": "<<riu_.avdMcuCpu().totalInstructions();
    if(options_.exploratoryAvdMcuStartup)s<<",\n  \"exploratory_avd_mcu_startup\": true,\n  \"avd_mcu_pc\": \""<<hex(riu_.avdMcuCpu().pc())
      <<"\",\n  \"avd_mcu_suspended\": "<<(riu_.avdMcuSuspended()?"true":"false");
    if(options_.exploratoryAvdSharedRun)s<<",\n  \"exploratory_avd_shared_run\": true"
      <<",\n  \"diagnostic_avd_deferred_startup_writes\": "<<(options_.diagnosticAvdDeferredStartup?"true":"false")
      <<",\n  \"avd_dispatch_divider\": "<<options_.avdDivider<<",\n  \"avd_instruction_budget\": "<<options_.avdBudget
      <<",\n  \"avd_interrupt_entries\": "<<riu_.avdMcuCpu().interruptEntries()
      <<",\n  \"avd_shared_register_reads\": "<<riu_.avdMcuBus().sharedReads
      <<",\n  \"avd_shared_register_writes\": "<<riu_.avdMcuBus().sharedWrites
      <<",\n  \"avd_private_ram_reads\": "<<riu_.avdMcuBus().ramReads<<",\n  \"avd_private_ram_writes\": "<<riu_.avdMcuBus().ramWrites
      <<",\n  \"avd_deferred_configuration_writes\": "<<riu_.avdMcuBus().configurationWrites
      <<",\n  \"avd_hardware_boundary_reason\": "<<std::quoted(riu_.avdMcuBoundaryReason())
      <<",\n  \"avd_shared_state_sha256\": \""<<riu_.avdSharedStateSha()<<"\""
      <<",\n  \"avd_shared_mapping_clock_reset_verified\": false";
    if(options_.exploratoryAvdHostResetEmpty)s<<",\n  \"exploratory_avd_host_request_reset_empty\": true"
      <<",\n  \"avd_host_request_latch_resets\": "<<riu_.avdHostRequestLatchResets
      <<",\n  \"avd_host_request_latch_reset_verified\": false";
    if(options_.diagnosticAvdSymbolicCodeTables)s<<",\n  \"diagnostic_avd_symbolic_code_tables\": true";
  }
  if(options_.candidateAudioIdma){
    // Existing audio diagnostics retain invalid ACK bytes independently of PM.
    if(options_.exploratoryAudioNoAck)s<<",\n  \"exploratory_audio_no_ack\": true,\n  \"audio_ack_unknown_samples\": "<<audioNoAck_.samples
      <<",\n  \"audio_ack_constrained_comparisons\": "<<audioNoAck_.comparisons<<",\n  \"audio_ack_timeouts\": "<<audioNoAck_.timeouts
      <<",\n  \"audio_ack_failure_returns\": "<<audioNoAck_.returns;
    s<<",\n  \"candidate_audio_idma\": true,\n  \"audio_idma_ports\": [";
    for(unsigned i=0;i<2;++i){const auto& p=riu_.audioIdma(i);if(i)s<<',';
      s<<"{\"word_uploads\":"<<p.wordWrites<<",\"word_reads\":"<<p.wordReads<<",\"initialized_words\":"<<p.initializedWords()
        <<",\"addressed_memory_sha256\":\""<<sha256(p.stateBytes())<<"\"}";}
    s<<']';
  }
  if(options_.exploratoryVdMcuStartup)s<<",\n  \"exploratory_vd_mcu_startup\": true,\n  \"vd_mcu_instructions\": "<<riu_.vdMcuCpu().instructions()
    <<",\n  \"vd_mcu_upstream_ticks\": "<<riu_.vdMcuCpu().cycles()<<",\n  \"vd_mcu_pc\": \""<<hex(riu_.vdMcuCpu().pc())<<"\",\n  \"vd_mcu_stack\": "<<unsigned(riu_.vdMcuCpu().stack());
  if(options_.deferVdSfr8eWrite)s<<",\n  \"deferred_vd_sfr8e_write_enabled\": true";
  if(options_.deferVdResetKeyWrites)s<<",\n  \"deferred_vd_reset_key_writes_enabled\": true";
  if(options_.exploratoryVdSharedRun)s<<",\n  \"exploratory_vd_shared_run\": true"
    <<",\n  \"vd_mcu_total_instructions\": "<<riu_.vdMcuCpu().totalInstructions()
    <<",\n  \"vd_mcu_interrupt_entries\": "<<riu_.vdMcuCpu().interruptEntries()
    <<",\n  \"vd_mcu_running\": "<<(riu_.vdMcuCpu().running()?"true":"false")
    <<",\n  \"vd_mailbox_reads\": "<<riu_.vdMailboxReads()
    <<",\n  \"vd_mailbox_writes\": "<<riu_.vdMailboxWrites()
    <<",\n  \"vd_dispatch_divider\": "<<riu_.vdDispatchDivider;
  if(options_.exploratoryPm51SpiStartup){
    const auto& p=riu_.pm51();
    if(options_.exploratoryPmPowerGood || options_.candidatePm51Uart)
      s<<",\n  \"exploratory_pm_power_good\": "<<(options_.exploratoryPmPowerGood?"true":"false")
       <<",\n  \"candidate_pm51_uart\": "<<(options_.candidatePm51Uart?"true":"false")
       <<",\n  \"pm_power_good_input_transitions\": "<<riu_.pmPowerGood().transitions;
    s<<",\n  \"exploratory_pm51_spi_startup\": true,\n  \"pm51_connected\": true"
      <<",\n  \"pm51_instructions\": "<<p.cpu().totalInstructions()
      <<",\n  \"pm51_ticks\": "<<p.cpu().cycles()<<",\n  \"pm51_pc\": \""<<hex(p.cpu().pc())<<"\""
      <<",\n  \"pm51_interrupt_entries\": "<<p.cpu().interruptEntries()
      <<",\n  \"pm51_running\": "<<(p.cpu().running()?"true":"false")
      <<",\n  \"pm51_suspended\": "<<(p.suspended()?"true":"false")
      <<",\n  \"pm51_boundary\": \""<<p.boundary()<<"\""
      <<",\n  \"pm51_ram_writes\": "<<p.ramWrites<<",\n  \"pm51_ram_reads\": "<<p.ramReads
      <<",\n  \"pm51_riu_reads\": "<<p.riuReads<<",\n  \"pm51_marker_writes\": "<<p.markerWrites
      <<",\n  \"pm51_spi_control_writes\": "<<p.controlWrites
      <<",\n  \"pm51_dispatch_divider_hypothesis\": "<<p.divider
      <<",\n  \"pm51_accumulator_known\": "<<unsigned(p.cpu().accumulatorKnown())
      <<",\n  \"pm51_unknown_origin\": \""<<hex(p.cpu().accumulatorOrigin())<<"\""
      <<",\n  \"pm51_state_sha256\": \""<<p.stateSha()<<"\"";
  }
  if(options_.explorePmNonresumeBranch)
    s<<",\n  \"diagnostic_pm_nonresume_branch\": true"
     <<",\n  \"pm_activity_invalid_samples\": "<<pmActivityScenario_.samples
     <<",\n  \"pm_activity_constrained_predicates\": "<<pmActivityScenario_.predicates
     <<",\n  \"pm_activity_original_branches\": "<<pmActivityScenario_.branches
     <<",\n  \"pm_activity_byte_known\": "<<riu_.knownBits(0x1f001cd1,1)
     <<",\n  \"PM_cold_boot_hardware_verified\": false";
  if(options_.exploratoryVdSuspendedBoundary)s<<",\n  \"exploratory_vd_suspended_boundary\": true,\n  \"vd_mcu_suspended\": "<<(riu_.vdMcuSuspended()?"true":"false")
    <<",\n  \"vd_mcu_boundary_reason\": \""<<riu_.vdMcuBoundaryReason()<<"\"";
  if(options_.osdProbes){
    s<<",\n  \"osd_probes_enabled\": true";
    s<<",\n  \"osd_routine_entries\": {";
    bool firstProbe=true;
    for(auto [pc,count]:osdProbeEntries_){if(!firstProbe)s<<',';firstProbe=false;s<<'\"'<<hex(pc)<<"\":"<<count;}
    s<<"}";
  }
  if(options_.zuiFlowProbes){
    s<<",\n  \"zui_flow_probes_enabled\": true,\n  \"zui_flow\": {\"read_only\":true,\"signature_failures\":"
      <<zuiFlow_.signatureFailures<<",\"dropped_observations\":"<<zuiFlow_.dropped<<",\"entries\":{";
    bool first=true;for(auto [pc,count]:zuiFlow_.entries){if(!first)s<<',';first=false;s<<'\"'<<hex(pc)<<"\":"<<count;}
    s<<"},\"snapshots\":[";first=true;
    for(const auto& observation:zuiFlow_.snapshots){
      if(!first)s<<',';
      first=false;
      s<<"{\"pc\":\""<<hex(observation.pc)<<"\",\"tick\":"<<observation.tick;
      auto field=[&](const char* name,auto value){s<<",\""<<name<<"\":";if(value)s<<*value;else s<<"null";};
      field("state",observation.state);field("previous_state",observation.previous);field("active_osd",observation.active);
      field("queue_head",observation.head);field("queue_tail",observation.tail);
      field("window_count",observation.windowCount);field("window_list",observation.windowList);
      field("software_tick",observation.softwareTick);field("wrapper_state",observation.wrapperState);
      field("input_service_state",observation.inputServiceState);field("input_key_raw",observation.inputKey);
      field("source_id_raw",observation.sourceId);field("source_phase_raw",observation.sourcePhase);
      field("source_timeout_timestamp",observation.sourceTimeoutTimestamp);field("software_flags_raw",observation.softwareFlags);
      field("bitmap196_address_raw",observation.bitmap196Address);field("bitmap196_bytes_raw",observation.bitmap196Bytes);
      field("bitmap196_pitch_raw",observation.bitmap196Pitch);field("bitmap196_width_raw",observation.bitmap196Width);
      field("bitmap196_height_raw",observation.bitmap196Height);field("bitmap196_format_raw",observation.bitmap196Format);
      field("bitmap196_valid_raw",observation.bitmap196Valid);field("surface_flags_raw",observation.surfaceFlags);
      s<<",\"bitmap196_source_sha256\":";
      if(observation.bitmap196Sha256)s<<'\"'<<*observation.bitmap196Sha256<<'\"';else s<<"null";
      field("ge_context_pointer_raw",observation.geContextPointer);field("ge_static_signature_raw",observation.geStaticSignature);
      field("ge_static_format_raw",observation.geStaticFormat);field("ge_static_destination_low_raw",observation.geStaticDestinationLow);
      field("ge_static_destination_high_raw",observation.geStaticDestinationHigh);field("ge_static_destination_pitch_raw",observation.geStaticDestinationPitch);
      s<<",\"queue_indices_valid\":"<<(observation.indicesValid?"true":"false")
        <<",\"known_hwnd_slots\":"<<observation.knownSlots<<",\"raw_occupied_hwnd_slots\":"<<observation.occupiedSlots<<'}';
    }
    s<<"]}";
  }
  if(options_.graphicsProbes){
    s<<",\n  \"graphics_probes\": {\"window_program_entries\":"<<gopWindowProgramEntries_<<",\"write32_entries\":"<<gopWrite32Entries_<<",\"decoded_windows\":[";
    bool first=true;
    for(auto bank:{1u,4u})for(unsigned w=0;w<(bank==1?4u:2u);++w)if(auto g=readGopWindow(riu_,bank,w)){
      if(!first)s<<',';
      first=false;
      s<<"{\"bank\":"<<bank<<",\"window\":"<<w<<",\"MIU_relative_address\":\""<<hex(g->miuRelativeAddress)<<"\",\"pitch\":"<<g->pitch
       <<",\"width\":"<<g->width<<",\"height\":"<<g->height<<",\"format\":"<<unsigned(g->format)<<",\"bpp\":"<<unsigned(g->bpp)<<'}';
    }
    s<<"]}";
  }
  if(options_.exploratorySarFullscale)s<<",\n  \"exploratory_sar_fullscale\": true,\n  \"sar_conversions\": "<<riu_.sarConversions();
  if(options_.exploratoryScartIdGrounded)s<<",\n  \"exploratory_scart_id_grounded\": true,\n  \"scart_id_sar_samples\": "<<riu_.scartIdSamples()
    <<",\n  \"scart_id_board_wiring_hardware_verified\": false";
  if(options_.exploratoryTspCpu)s<<",\n  \"or1k_trace_first_visits\": "<<(options_.orFirstVisits?"true":"false")
    <<",\n  \"or1k_budget\": "<<options_.orBudget<<",\n  \"or1k_original_word_fetches\": "<<riu_.orOriginalWords()
    <<",\n  \"or1k_modified_word_fetches\": "<<riu_.orModifiedWords()<<",\n  \"or1k_unique_pcs\": "<<riu_.orVisited()
    <<",\n  \"or1k_core_QMEM_state_sha256\": \""<<riu_.tspCoreStateSha()<<"\"";
  if(options_.exploratoryTspCpu)s<<",\n  \"or1k_dispatch_divider\": "<<options_.orDivider;
  if(options_.exploratoryGopResetWindows)s<<",\n  \"exploratory_gop_reset_windows\": true,\n  \"gop_reset_pulses\": "<<riu_.gopResetPulses();
  if(options_.exploratoryIrDmaReset){
    s<<",\n  \"exploratory_ir_dma_reset\": true,\n  \"ir_dma_starts\": "<<riu_.irDmaStarts()
      <<",\n  \"ir_dma_captures\": "<<riu_.irDmaCaptures()<<",\n  \"ir_dma_base\": \""<<hex(riu_.irDmaAddress())
      <<"\",\n  \"ir_dma_capacity\": "<<riu_.irDmaCapacity()<<",\n  \"ir_dma_producer\": ";
    if(auto index=riu_.irDmaProducer())s<<*index;else s<<"null";
  }
  if(auto bank=riu_.scBank()) s << ",\n  \"current_sc_bank\": " << *bank;
  else s << ",\n  \"current_sc_bank\": null";
  if(auto bank=riu_.combBank())s << ",\n  \"current_comb_bank\": " << *bank;
  else s << ",\n  \"current_comb_bank\": null";
  if(auto t=riu_.panelTiming()) s << ",\n  \"panel_timing\": {\"active_width\":" << t->hend-t->hstart+1
    << ",\"active_height\":" << t->vend-t->vstart+1 << ",\"htotal\":" << t->htotal
    << ",\"vtotal\":" << t->vtotal << ",\"vsync_start\":" << t->vsyncStart << ",\"vsync_end\":" << t->vsyncEnd
    << ",\"invert\":" << (t->invert?"true":"false") << "}";
  s << ",\n  \"cp0_status\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_STATUS))
    << "\",\n  \"invalid_observation_bytes\": [";
  first = true;
  for (auto [address, origin] : unknownObservations_.bytes()) {
    if (!first)
      s << ',';
    first = false;
    s << "{\"address\":\"" << hex(address) << "\",\"origin\":\"" << hex(origin) << "\"}";
  }
  s << "],\n  \"recent_instructions\": [";
  first = true;
  for (auto &i : recentInstructions_) {
    if (!first)
      s << ',';
    first = false;
    s << "{\"tick\":" << i.tick << ",\"pc\":\"" << hex(i.pc) << "\",\"word\":\"" << hex(i.word)
      << "\",\"ra\":\"" << hex(i.ra) << "\",\"sp\":\"" << hex(i.sp) << "\"}";
  }
  s << "],\n  \"recent_mmio\": [";
  first = true;
  for (auto &line : recentMmio_) {
    if (!first)
      s << ',';
    first = false;
    s << line;
  }
  s << ']';
  if(virtualTime_.cpi()!=1)
    s << ",\n  \"exploratory_average_cpi\": " << virtualTime_.cpi()
      << ",\n  \"virtual_cpu_cycles\": " << virtualTime_.cycles(dispatches_)
      << ",\n  \"cpu_cycle_model_hardware_verified\": false";
  if(options_.coproProbes)
    s << ",\n  \"copro_loader_inspections\": " << coproLoadInspections_
      << ",\n  \"copro_guest_loaded_sha256\": \"" << coproLoadedSha_ << "\""
      << ",\n  \"copro_original_program_match\": "
      << (coproLoadedSha_=="bc00e532ab47b35e2f3a8be0b59a40b7ae80111dd366b6ceb741cdb74ca95ae1"?"true":"false");
  if(options_.noOrQmemTrace)
    s << ",\n  \"or1k_qmem_trace_disabled\": true,\n  \"or1k_qmem_trace_records_suppressed\": " << suppressedOrQmemTrace_;
  if(options_.orIdlePollChanges)
    s << ",\n  \"or1k_idle_poll_log_changes_only\": true,\n  \"or1k_idle_poll_records_suppressed\": " << suppressedOrIdlePolls_;
  if(options_.orRegisterLogChanges)
    s << ",\n  \"or1k_stored_read_log_changes_only\": true,\n  \"or1k_stored_read_records_suppressed\": " << suppressedOrStoredReads_;
  if(options_.exploratoryGopVsyncLatch){
    s << ",\n  \"exploratory_gop_sync_latch\": true,\n  \"gop_update_requests\": "<<riu_.gopUpdate().requests
      << ",\n  \"gop_force_latches\": "<<riu_.gopUpdate().forced<<",\n  \"gop_register_latches\": "<<riu_.gopUpdate().latches
      << ",\n  \"gop_ack_known_mask\": "<<riu_.gopUpdate().known()<<",\n  \"gop_active_register_bytes\": "<<riu_.gopActiveBytes()
      << ",\n  \"gop_latch_clock_domain_hardware_verified\": false,\n  \"gop_active_descriptors\": [";
    bool firstActive=true;
    for(auto bank:{1u,4u})for(unsigned window=0;window<(bank==1?4u:2u);++window){
      auto descriptor=readGopWindow(riu_,bank,window,true);
      if(!descriptor)continue;
      if(!firstActive)s<<',';
      firstActive=false;
      s<<"{\"bank\":"<<bank<<",\"window\":"<<window
       <<",\"miu_relative_address\":\""<<hex(descriptor->miuRelativeAddress)<<"\",\"pitch\":"<<descriptor->pitch
       <<",\"width\":"<<descriptor->width<<",\"height\":"<<descriptor->height
       <<",\"format\":"<<unsigned(descriptor->format)<<",\"bpp\":"<<unsigned(descriptor->bpp)<<'}';
    }
    s<<"],\n  \"gop_configured_descriptors\": [";
    bool firstConfigured=true;
    for(bool active:{false,true})for(auto bank:{1u,4u})for(unsigned window=0;window<(bank==1?4u:2u);++window){
      auto descriptor=readGopWindow(riu_,bank,window,active,false);
      if(!descriptor)continue;
      if(!firstConfigured)s<<',';
      firstConfigured=false;
      s<<"{\"active_latch\":"<<(active?"true":"false")<<",\"enabled\":"<<(descriptor->enabled?"true":"false")
       <<",\"bank\":"<<bank<<",\"window\":"<<window<<",\"miu_relative_address\":\""<<hex(descriptor->miuRelativeAddress)
       <<"\",\"pitch\":"<<descriptor->pitch<<",\"width\":"<<descriptor->width<<",\"height\":"<<descriptor->height
       <<",\"format\":"<<unsigned(descriptor->format)<<",\"bpp\":"<<unsigned(descriptor->bpp);
      // Read-only observation, never proof of allocation or authentic drawing.
      auto physical=gopPhysicalAddress(riu_,bank,window,descriptor->miuRelativeAddress);
      uint64_t size=uint64_t(descriptor->pitch)*descriptor->height;
      if(physical && size && size<=16*1024*1024 && uint64_t(*physical)+size<=dramBacking_.size()){
        auto bytes=std::span<const uint8_t>(dramBacking_).subspan(*physical,size);
        uint64_t nonzero=0,invalid=0;
        for(size_t i=0;i<bytes.size();++i){
          nonzero+=bytes[i]!=0;
          auto address=*physical+uint32_t(i);
          invalid+=preservedBits_.memoryUnknown(address) || unknownObservations_.bytes().contains(address);
        }
        s<<",\"memory_observation\":{\"physical_address\":\""<<hex(*physical)<<"\",\"byte_count\":"<<size
         <<",\"nonzero_bytes\":"<<nonzero<<",\"invalid_bytes\":"<<invalid<<",\"sha256\":\""<<sha256(bytes)
         <<"\",\"authentic_frame\":false}";
      }
      s<<'}';
    }
    s<<']';
  }
  if(!options_.opaqueReadPolicy.empty()){
    std::vector<uint8_t> rules(opaquePolicyRules_.begin(),opaquePolicyRules_.end());
    s<<",\n  \"opaque_policy_enabled\": true,\n  \"opaque_policy_reads_deferred\": "<<opaquePolicyReads_
      <<",\n  \"opaque_policy_accepted_sequence_sha256\": \""<<sha256(rules)<<"\"";
  }
  s << "\n}";
  return s.str();
}
} // namespace manta
