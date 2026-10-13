#include "board/machine.hpp"
#include "board/mmio_read_mask.hpp"
#include "storage/sha256.hpp"
#include "video/gop.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
namespace manta {
static std::string hex(uint64_t n) {
  std::ostringstream s;
  s << "0x" << std::hex << n;
  return s.str();
}
Msd309Machine::Msd309Machine(Flash f, Options o) : flash_(std::move(f)), options_(o) {
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
  riu_.candidateGopPalette=o.candidateGopPalette;
  riu_.candidateTsp=o.candidateTsp;
  riu_.candidateVdMcuProgram=o.candidateVdMcuProgram;
  if(o.deferVdSfr8eWrite && !o.exploratoryVdMcuStartup)
    throw std::runtime_error("VD SFR write-effect deferral requires the explicit bounded startup experiment");
  riu_.deferVdSfr8eWrite=o.deferVdSfr8eWrite;
  riu_.candidateAvdMcuProgram=o.candidateAvdMcuProgram;
  riu_.candidateAudioIdma=o.candidateAudioIdma;
  if(o.exploratoryAvdMcuStartup && (!o.candidateAvdMcuProgram || !o.symbolicInit || !o.deferDiagnosticReads))
    throw std::runtime_error("AVD original startup experiment requires the original programming port and guarded exploration");
  if(o.exploratoryAvdSuspendedBoundary && !o.exploratoryAvdMcuStartup)
    throw std::runtime_error("AVD boundary suspension requires original auxiliary instruction execution");
  riu_.exploratoryAvdMcuStartup=o.exploratoryAvdMcuStartup;
  riu_.exploratoryAvdSuspendedBoundary=o.exploratoryAvdSuspendedBoundary;
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
  riu_.configureOrTraceBudget(o.orFirstVisits,o.orBudget,o.orDivider);
  if(o.exploratorySarFullscale){
    if(!o.symbolicInit || !o.deferDiagnosticReads)throw std::runtime_error("SAR input/reference fixture requires explicitly guarded exploration");
    riu_.configureExploratorySarFullscale();
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
  riu_.dramRead=[this](uint32_t address,size_t size) {
    if(!options_.candidateMiu || size==0 || size>0x4000 || uint64_t(address)+size>dramBacking_.size())
      throw std::runtime_error("TSP DMA guest DRAM source outside mapped bounds");
    for(size_t i=0;i<size;++i)
      if(preservedBits_.memoryUnknown(address+uint32_t(i)) || unknownObservations_.bytes().contains(address+uint32_t(i)))
        throw std::runtime_error("TSP DMA source contains invalid hardware-origin bits");
    std::vector<uint8_t> bytes(size);cpu_.read(address,bytes.data(),size);return bytes;
  };
  riu_.event = [this](auto &k, auto a, auto n, auto v, auto &d) { event(k, a, n, v, d); };
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
void Msd309Machine::event(const std::string &k, uint32_t a, unsigned n, uint32_t v,
                          const std::string &d) {
  std::ostringstream s;
  s << "{\"tick\":" << dispatches_ << ",\"pc\":\"" << hex(cpu_.reg(UC_MIPS_REG_PC))
    << "\",\"kind\":\"" << k << "\",\"address\":\"" << hex(a) << "\",\"width\":" << n
    << ",\"value\":\"" << hex(v) << "\",\"ra\":\"" << hex(cpu_.reg(UC_MIPS_REG_RA))
    << "\",\"sp\":\"" << hex(cpu_.reg(UC_MIPS_REG_SP)) << "\",\"detail\":\"" << d << "\"}";
  auto line = s.str();
  if ((k == "read" || k == "write") && a >= 0x1f203e00 && a < 0x1f204000)
    ++gopEvents_;
  if ((k == "read" || k == "write") && a >= 0x1f205000 && a < 0x1f205200)
    ++geEvents_;
  if ((k == "blocked_read" || k == "blocked_write") && a >= 0x1f205000 && a < 0x1f205200)
    ++geBlockedEvents_;
  if (k == "read" || k == "write" || k == "blocked_read" || k == "deferred_read") {
    recentMmio_.push_back(line);
    if (recentMmio_.size() > 256)
      recentMmio_.pop_front();
  }
  if (trace_.is_open()){
    trace_ << line << '\n';
    if(!trace_)stop("trace write failure; execution evidence truncated",a);
  }
  if (replay_.is_open()) {
    std::string expected;
    if (!std::getline(replay_, expected) || expected != line)
      stop("deterministic replay mismatch", a);
  }
  if (k == "read" || k == "write" || k == "blocked_read")
    ++mmio_;
  if (k == "write")
    ++writeEpoch_;
  if (k == "read") {
    auto key = (uint64_t(cpu_.reg(UC_MIPS_REG_PC)) << 32) | a;
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
void Msd309Machine::codeHook(uc_engine *, uint64_t a, uint32_t, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  ++m.dispatches_;
  if(m.options_.osdProbes && (a==0x804e0d48 || a==0x804e0dc4 || a==0x80536274 ||
      a==0x80522d5c || a==0x80520a84 || a==0x8040440c)){
    ++m.osdProbeEntries_[uint32_t(a)];
    m.event("guest_osd_routine_entry",uint32_t(a),0,m.cpu_.reg(UC_MIPS_REG_4),
      "original routine entry; a1="+hex(m.cpu_.reg(UC_MIPS_REG_5))+", a2="+hex(m.cpu_.reg(UC_MIPS_REG_6))+
      ", a3="+hex(m.cpu_.reg(UC_MIPS_REG_7))+"; 804E0D48=ZUI state initialization, 804E0DC4=state dispatch, 80536274=GOP CreateWinImpl2, 80520A84=font callback renderer; no host invocation");
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
  if (m.options_.cp0Timer && (m.dispatches_ & 1) == 0)
    m.cpu_.tickCount(1);
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
  m.coverage_.insert(uint32_t(a));
  ++m.pcHits_[uint32_t(a)];
  m.history_.push_back(uint32_t(a));
  if (m.history_.size() > 64)
    m.history_.pop_front();
  if (a == 0x81000000)
    m.stage2Reached_ = true;
  if (a == 0x81000480 || a == 0x81004f08)
    m.decompressReached_ = true;
  if (a == 0x803b0600)
    m.runtimeReached_ = true;
  uint32_t w = 0;
  m.cpu_.read(uint32_t(a) & 0x1fffffff, &w, 4);
  if ((w >> 26) == 16 && ((w >> 21) & 31) == 4 &&
      (((w >> 11) & 31) <= 6 || ((w >> 11) & 31) == 10)) {
    m.event("cp0_mmu_write", (w >> 11) & 31, 4, m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 16) & 31)),
            "original guest MTC0 MMU register; select " + std::to_string(w & 7));
  }
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
  if (a >= 0x803b0180 && a < 0x806b8ce0) {
    m.recentInstructions_.push_back(
        {m.dispatches_, uint32_t(a), w, m.cpu_.reg(UC_MIPS_REG_RA), m.cpu_.reg(UC_MIPS_REG_SP)});
    if (m.recentInstructions_.size() > 4096)
      m.recentInstructions_.pop_front();
  }
  if (m.options_.deferDiagnosticReads) {
    auto why = m.unknownObservations_.observe(
        uint32_t(a), w, [&](unsigned i) { return m.cpu_.reg(UC_MIPS_REG_0 + int(i)); });
    if (!why.empty()) {
      m.event("unknown_observation_use", uint32_t(a), 4, w, why);
      auto op = w >> 26;
      auto address = (m.cpu_.reg(UC_MIPS_REG_0 + int((w >> 21) & 31)) + uint32_t(int32_t(int16_t(w)))) & 0x1fffffff;
      m.stop(why, op >= 32 && op <= 38 ? address : 0);
      return;
    }
    if(a==0x805f883c && w==0xa4820000 && m.cpu_.reg(UC_MIPS_REG_31)==0x803c69f8) {
      auto target=m.cpu_.reg(UC_MIPS_REG_4)&0x1fffffff;
      m.riu_.beginCombRestore(target,m.preservedBits_.mask(2)&0xffff);
    }
    auto preservedWhy=m.preservedBits_.observe(w,[&](unsigned r){return m.cpu_.reg(UC_MIPS_REG_0+int(r));},
      [&](uint32_t address){return m.riu_.preservedStoreKey(address);},
      [&](uint32_t address,unsigned width,uint32_t unknown){
        m.riu_.preparePreservedWrite(address,width,unknown);
        m.event("preserved_control_write",address,width,unknown,"original unknown bits return to the same banked byte/bit identities; no decision or new hardware consumer authorized");
      });
    if(!preservedWhy.empty()) {
      auto op=w>>26;
      auto target=(m.cpu_.reg(UC_MIPS_REG_0+int((w>>21)&31))+uint32_t(int32_t(int16_t(w))))&0x1fffffff;
      m.stop(preservedWhy,op>=32 && op<=43?target:0);return;
    }
  }
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
  auto last = m.history_.empty() ? 0 : m.history_.back();
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
    // Stock boot switches between cached and uncached ROM aliases. The RMW
    // proof address is an instruction identity; traces retain the real PC.
    if ((pc & 0x1fffffff) >= 0x1fc00000 && (pc & 0x1fffffff) < 0x1fc02c60)
      pc = (pc & 0x1fffffff) | 0xa0000000;
    if (m.options_.symbolicInit) {
      auto defined = m.proveImmediateRmw(pc, a, n);
      if (defined)
        m.riu_.prepareRmw(a, defined);
    }
    if (m.options_.symbolicInit) {
      uint32_t words[8]{};
      m.cpu_.read(pc & 0x1fffffff, words, sizeof(words));
      if(auto consumed=immediateReadMask(words))m.riu_.prepareMaskedRead(a,*consumed);
    }
    if(m.options_.exploratoryDisplayClock && pc==0x805fbc8c && a==0x1f205e78 && n==1) {
      constexpr uint32_t expected[]={0x90440000,0x8ce39634,0x3c020020,0x34425e00,
        0x00621821,0x308400ff,0xa0650000,0x03e00008,0x30820001};
      uint32_t actual[9]{}; m.cpu_.read(pc&0x1fffffff,actual,sizeof(actual));
      if(std::equal(std::begin(expected),std::end(expected),std::begin(actual)))
        m.riu_.prepareMaskedRead(a,1); // full straight-line original reader; only bit 0 returned
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
          if (pc == 0x8053c710 && m.options_.spiUnknownInput == SpiUnknownInput::Deferred && m.riu_.spiDataUnknown(m.dispatches_)) {
            m.unknownObservations_.seedSpiCache(m.cpu_.reg(UC_MIPS_REG_18) + m.cpu_.reg(UC_MIPS_REG_6));
            m.event("deferred_spi_input", a, n, 0xa5, "INVALID undocumented CF sample; verified SB cache only, first consumption stops");
          }
        }
      }
    }
    auto v = m.riu_.read(a, n, pc, mask, m.dispatches_);
    if (!m.riu_.stopReason.empty())
      m.stop(m.riu_.stopReason, a);
    return v;
  } catch (const std::exception &e) {
    m.stop(e.what(), 0x1f000000 + uint32_t(off));
    return 0xdeadbeef;
  }
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
  bool changed = false;
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
      // A register may have held the sample, then been overwritten with an
      // independently known address. Its current dependency, not its name,
      // determines whether the same-device store has a proven destination.
      if (target != address || n != width || !changed || dependent[rs])
        return 0;
      auto full = width == 4 ? 0xffffffff : (uint32_t(1) << (width * 8)) - 1;
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
    m.riu_.write(a, n, uint32_t(v), pc, m.cpu_.reg(UC_MIPS_REG_3), m.dispatches_);
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
  auto e = cpu_.run(options_.stage2 ? 0x81000000 : 0xbfc00000, options_.limit);
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
  auto s = summary();
  if (!options_.summary.empty()) {
    std::ofstream f(options_.summary);
    f << s << '\n';
    f.flush();if(!f)throw std::runtime_error("cannot write complete execution summary file");
  }
  std::cout << s << "\nUART: " << riu_.uart << '\n';
  return 2; // Research execution is incomplete even when a diagnostic
            // checkpoint is reached.
}
std::string Msd309Machine::summary() const {
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
    << ",\n  \"virtual_output_frames\": " << riu_.displayFrames(dispatches_)
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
  }
  if(options_.candidateAudioIdma){
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
  if(options_.exploratoryVdSuspendedBoundary)s<<",\n  \"exploratory_vd_suspended_boundary\": true,\n  \"vd_mcu_suspended\": "<<(riu_.vdMcuSuspended()?"true":"false")
    <<",\n  \"vd_mcu_boundary_reason\": \""<<riu_.vdMcuBoundaryReason()<<"\"";
  if(options_.osdProbes){
    s<<",\n  \"osd_probes_enabled\": true";
    s<<",\n  \"osd_routine_entries\": {";
    bool firstProbe=true;
    for(auto [pc,count]:osdProbeEntries_){if(!firstProbe)s<<',';firstProbe=false;s<<'\"'<<hex(pc)<<"\":"<<count;}
    s<<"}";
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
  s << "]\n}";
  return s.str();
}
} // namespace manta
