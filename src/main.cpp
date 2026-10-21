#include "board/machine.hpp"
#include "ui/native_window.hpp"
#include <filesystem>
#include "storage/sha256.hpp"
#include <set>
#include <iostream>
#include <string>
#include <vector>
#include <future>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
static std::filesystem::path utf8Path(const std::string &s) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
// Hash of every option that can change guest-visible machine behaviour. Output/observation options and the run length are excluded so a
// snapshot can be resumed with a different summary path, budget or remote script, but never under different hardware assumptions.
static std::string stateFingerprint(const std::vector<std::string> &args, std::string &canonicalOut) {
  static const std::set<std::string> valued = {"--summary", "--coverage", "--pm51-history", "--save-state", "--load-state", "--profile", "--save-state-at",
    "--save-state-max-defer", "--max-instructions", "--ir-script", "--ir-keymap", "--gop-frame-history", "--gop-timeline-from", "--preview-capture",
    "--preview-close-after-ms", "--exploratory-video-plane", "--preview-capture-offscreen", "--export-gop-frame", "--dump-guest-range", "--watch-guest-access", "--dump-prefix", "--watch-pc-regs",
    "--offscreen-inspector", "--tsp-firmware-dump", "--record-trace", "--replay-trace", "--firmware", "--nvram", "--gop-frame-history-at"};
  static const std::set<std::string> flags = {"--exploratory-gop-black-colorkey", "--save-state-stop", "--state-digest", "--load-state-allow-extra-options", "--headless", "--window-smoke-test"};
  std::string canonical;
  for (size_t i = 1; i < args.size(); ++i) {
    if (valued.count(args[i])) { ++i; continue; }
    if (flags.count(args[i])) continue;
    canonical += args[i];
    canonical.push_back(char(10));
  }
  canonicalOut = canonical;
  return manta::sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(canonical.data()), canonical.size()));
}
static int appMain(const std::vector<std::string> &args) {
  try {
    manta::Options o;
    std::filesystem::path firmware, nvram;
    bool headless = false, windowSmoke = false;
    manta::PreviewWindowOptions previewOptions;
    std::string previewSelftest;
    auto value = [&](size_t &i) {
      if (++i >= args.size())
        throw std::runtime_error("Missing argument value");
      return args[i];
    };
    for (size_t i = 1; i < args.size(); ++i) {
      const auto &a = args[i];
      if (a == "--firmware")
        firmware = utf8Path(value(i));
      else if (a == "--nvram")
        nvram = utf8Path(value(i));
      else if (a == "--headless")
        headless = true;
      else if (a == "--window-smoke-test")
        windowSmoke = true;
      else if (a == "--diagnostic-stage2")
        o.stage2 = true;
      else if (a == "--diagnostic-init-prefix")
        o.initPrefix = true;
      else if (a == "--symbolic-init")
        o.symbolicInit = true;
      else if (a == "--candidate-bdma")
        o.candidateBdma = true;
      else if (a == "--candidate-uart")
        o.candidateUart = true;
      else if (a == "--gpio-i2c")
        o.gpioI2c = true;
      else if (a == "--candidate-miu")
        o.candidateMiu = true;
      else if (a == "--i2c-sda-stuck")
        o.i2cSdaStuck = true;
      else if (a == "--trace-instructions")
        o.traceInstructions = true;
      else if (a == "--defer-diagnostic-reads")
        o.deferDiagnosticReads = true;
      else if (a == "--virtual-rtc")
        o.virtualRtc = true;
      else if (a == "--candidate-spi")
        o.candidateSpi = true;
      else if (a == "--spi-flash") {
        auto model = value(i);
        if (model == "w25q64bv") o.flashModel = manta::NorModel::W25Q64BV;
        else if (model == "mx25l6405d") o.flashModel = manta::NorModel::MX25L6405D;
        else throw std::runtime_error("Unsupported SPI flash model: " + model);
      } else if (a == "--defer-spi-probe")
        o.spiUnknownInput = manta::SpiUnknownInput::Deferred;
      else if (a == "--exploratory-spi-input") {
        auto input = value(i);
        if (input == "pull-up") o.spiUnknownInput = manta::SpiUnknownInput::AssumedPullUp;
        else if (input == "pull-down") o.spiUnknownInput = manta::SpiUnknownInput::AssumedPullDown;
        else throw std::runtime_error("Expected pull-up or pull-down electrical hypothesis");
      }
      else if (a == "--candidate-utmi")
        o.candidateUtmi = true;
      else if (a == "--candidate-chip-id")
        o.candidateChipId = true;
      else if (a == "--cp0-timer")
        o.cp0Timer = true;
      else if (a == "--exploratory-cpi")o.exploratoryCpi=std::stoull(value(i),nullptr,0);
      else if (a == "--copro-probes")o.coproProbes=true;
      else if (a == "--exploratory-copro")o.exploratoryCopro=true;
      else if (a == "--exploratory-copro-seq"){o.exploratoryCoproSeqLo=uint32_t(std::stoul(value(i),nullptr,0));o.exploratoryCoproSeqHi=uint32_t(std::stoul(value(i),nullptr,0));}
      else if (a == "--exploratory-copro-capture")o.exploratoryCoproCapture=uint32_t(std::stoul(value(i),nullptr,0));
      else if (a == "--exploratory-copro-watch")o.exploratoryCoproWatch=uint32_t(std::stoul(value(i),nullptr,0));
      else if (a == "--diagnostic-riu-zero-unknown-reads")o.exploratoryRiuZeroUnknown=true;
      else if (a == "--exploratory-copro-mips-irq")o.exploratoryCoproMipsIrq=std::stoi(value(i),nullptr,0);
      else if (a == "--exploratory-copro-dump-start")o.exploratoryCoproDumpStart=value(i);
      else if (a == "--exploratory-copro-zero-unknown-reads")o.exploratoryCoproZeroUnknown=true;
      else if (a == "--opaque-read-policy")o.opaqueReadPolicy=value(i);
      else if (a == "--opaque-policy-wait-ms")o.opaquePolicyWaitMs=std::stoull(value(i),nullptr,0);
      else if (a == "--or1k-no-qmem-trace")o.noOrQmemTrace=true;
      else if (a == "--or1k-idle-poll-log-changes")o.orIdlePollChanges=true;
      else if (a == "--or1k-register-log-changes")o.orRegisterLogChanges=true;
      else if (a == "--exploratory-gop-vsync-latch")o.exploratoryGopVsyncLatch=true;
      else if (a == "--exploratory-scart-grounded")o.exploratoryScartGrounded=true;
      else if (a == "--graphics-probes")o.graphicsProbes=true;
      else if (a == "--osd-probes")o.osdProbes=true;
      else if (a == "--zui-flow-probes")o.zuiFlowProbes=true;
      else if (a == "--candidate-vd-mcu-program")o.candidateVdMcuProgram=true;
      else if (a == "--candidate-avd-mcu-program")o.candidateAvdMcuProgram=true;
      else if (a == "--candidate-audio-idma")o.candidateAudioIdma=true;
      else if (a == "--exploratory-pm51-spi-startup")o.exploratoryPm51SpiStartup=true;
      else if (a == "--exploratory-pm-power-good")o.exploratoryPmPowerGood=true;
      else if (a == "--candidate-pm51-uart")o.candidatePm51Uart=true;
      else if (a == "--explore-pm-nonresume-branch")o.explorePmNonresumeBranch=true;
      else if (a == "--pm51-history")o.pm51History=utf8Path(value(i));
      else if (a == "--pm51-dispatch-divider"){
        o.pm51Divider=std::stoull(value(i),nullptr,0);
        if(!o.pm51Divider || o.pm51Divider>4096)throw std::runtime_error("PM divider must be 1..4096; relationship remains hypothetical");
      }
      else if (a == "--exploratory-audio-no-ack")o.exploratoryAudioNoAck=true;
      else if (a == "--exploratory-avd-mcu-startup")o.exploratoryAvdMcuStartup=true;
      else if (a == "--exploratory-avd-suspended-boundary")o.exploratoryAvdSuspendedBoundary=true;
      else if(a=="--exploratory-avd-shared-run")o.exploratoryAvdSharedRun=true;
      else if(a=="--exploratory-avd-host-request-reset-empty")o.exploratoryAvdHostResetEmpty=true;
      else if(a=="--diagnostic-avd-deferred-startup-writes")o.diagnosticAvdDeferredStartup=true;
      else if(a=="--diagnostic-avd-symbolic-code-tables")o.diagnosticAvdSymbolicCodeTables=true;
      else if(a=="--avd-dispatch-divider")o.avdDivider=std::stoull(value(i),nullptr,0);
      else if(a=="--max-avd-instructions")o.avdBudget=std::stoull(value(i),nullptr,0);
      else if (a == "--exploratory-vd-mcu-startup")o.exploratoryVdMcuStartup=true;
      else if (a == "--defer-vd-sfr8e-write")o.deferVdSfr8eWrite=true;
      else if (a == "--defer-vd-reset-key-writes")o.deferVdResetKeyWrites=true;
      else if (a == "--exploratory-vd-shared-run")o.exploratoryVdSharedRun=true;
      else if (a == "--exploratory-vd-suspended-boundary")o.exploratoryVdSuspendedBoundary=true;
      else if (a == "--exploratory-fetch-revision-byte"){
        auto v=std::stoul(value(i),nullptr,0);
        if(v>255)throw std::runtime_error("Fetch revision hypothesis byte must be 0..255");
        o.exploratoryFetchRevisionByte=int(v);
      }
      else if(a=="--exploratory-analog-disconnected")o.exploratoryAnalogDisconnected=true;
      else if(a=="--exploratory-avd-no-hsync")o.exploratoryAvdNoHsync=true;
      else if(a=="--diagnostic-htotal-branch-join")o.diagnosticHtotalBranchJoin=true;
      else if(a=="--candidate-spi-nor-write")o.candidateSpiNorWrite=true;
      else if(a=="--exploratory-usb-no-device")o.exploratoryUsbNoDevice=true;
      else if(a=="--exploratory-provenance-extensions")o.provenanceExtensions=true;
      else if(a=="--exploratory-mmio-bits"){
        auto spec=value(i);   // HEXADDR:HEXMASK:HEXVALUE
        unsigned long addr=0,mask=0,val=0;char c1=0,c2=0;int scBank=-1;
        auto at=spec.find('@');
        if(at!=std::string::npos){scBank=int(std::stoul(spec.substr(at+1),nullptr,0));spec=spec.substr(0,at);}
        bool rmw=false,init=false,ack=false;
        for(bool again=true;again;){
          again=false;
          if(spec.size()>4 && spec.compare(spec.size()-4,4,"+rmw")==0){rmw=true;spec.resize(spec.size()-4);again=true;}
          if(spec.size()>5 && spec.compare(spec.size()-5,5,"+init")==0){init=true;spec.resize(spec.size()-5);again=true;}
          if(spec.size()>4 && spec.compare(spec.size()-4,4,"+ack")==0){ack=true;spec.resize(spec.size()-4);again=true;}
        }
        if(std::sscanf(spec.c_str(),"%lx%c%lx%c%lx",&addr,&c1,&mask,&c2,&val)!=5 || c1!=':' || c2!=':' || mask>255 || val>255 || (val&~mask) || !(addr>=0x1f000000ul && addr<0x1f400000ul))
          throw std::runtime_error("--exploratory-mmio-bits needs a RIU address 1F000000..1F3FFFFF:MASK:VALUE[+rmw][+init][+ack][@SCBANK] (hex, value within mask)");
        o.bitHypotheses.push_back({uint32_t(addr),unsigned(mask),unsigned(val),scBank,rmw,init,ack});
      }
      else if(a=="--exploratory-afec-indirect-6a3c"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>0xffff || token[0]=='-')throw std::runtime_error("AFEC indirect 6A3C content must be 0..0xFFFF");
        o.exploratoryAfecIndirect=int(n);
      }
      else if(a=="--exploratory-avd-noise-constant"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>0xff || token[0]=='-')throw std::runtime_error("AVD noise constant must be 0..0xFF");
        o.exploratoryAvdNoise=int(n);
      }
      else if(a=="--exploratory-avd-htotal-constant"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>0xffff || token[0]=='-')throw std::runtime_error("AVD HTotal constant must be 0..0xFFFF");
        o.exploratoryAvdHtotal=int(n);
      }
      else if(a=="--exploratory-ge-i2-glyph")o.exploratoryGeI2Glyph=true;
      else if(a=="--exploratory-ge-blend-copy")o.exploratoryGeBlendCopy=true;
      else if(a=="--exploratory-ge-i8-fill")o.exploratoryGeI8Fill=true;
      else if(a=="--exploratory-ge-nearest-stretch")o.exploratoryGeNearestStretch=true;
      else if(a=="--exploratory-ge-gradient-fill")o.exploratoryGeGradientFill=true;
      else if(a=="--exploratory-ge-line")o.exploratoryGeLine=true;
      else if(a=="--no-loop-skip")o.noLoopSkip=true;
      else if(a=="--exploratory-usb-pendrive")o.exploratoryUsbPendrive=value(i);
      else if(a=="--exploratory-ge-fill-ignore-reg00"){auto v=std::stoul(value(i),nullptr,0);if(v==0||v>6||(v&1))throw std::runtime_error("--exploratory-ge-fill-ignore-reg00 takes a mask within bits 1..2 of GE register 00");o.exploratoryGeFillIgnoredReg00=unsigned(v);}
      else if(a=="--exploratory-ge-height5-full-copy")o.exploratoryGeHeight5Full=true;
      else if(a=="--exploratory-ge-copy-snapshot")o.exploratoryGeCopySnapshot=true;
      else if(a=="--exploratory-ge-uniform-stretch"){
        auto token=value(i);
        if(token=="opaque-source")o.exploratoryGeUniformStretch=0;
        else if(token=="alpha-bit")o.exploratoryGeUniformStretch=1;
        else throw std::runtime_error("--exploratory-ge-uniform-stretch must be opaque-source or alpha-bit");
      }
      else if(a=="--diagnostic-vif-cr-lock"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>1)throw std::runtime_error("VIF carrier-lock bracketing value must be 0 or 1");
        o.diagnosticVifCrLock=int(n);
      }
      else if(a=="--diagnostic-usb-unpowered-dplus"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>1)throw std::runtime_error("USB powered-down D+ bracketing value must be 0 or 1");
        o.diagnosticUsbUnpoweredDplus=int(n);
      }
      else if(a=="--diagnostic-restore-by-identity-branch")o.diagnosticRestoreByIdentityBranch=true;
      else if(a=="--exploratory-avd-no-sync-events")o.exploratoryAvdNoSyncEvents=true;
      else if(a=="--diagnostic-avd-invalid-publication")o.diagnosticAvdInvalidPublication=true;
      else if(a=="--diagnostic-avd-11db-bit7"){
        auto token=value(i);size_t used=0;auto n=std::stoul(token,&used,0);
        if(used!=token.size() || n>1)throw std::runtime_error("11DB bit 7 bracketing value must be 0 or 1");
        o.diagnosticAvd11dbBit7=int(n);
      }
      else if(a=="--exploratory-vif-zero-input-settle-cycles"){
        auto token=value(i);size_t used=0;auto n=std::stoull(token,&used,0);
        if(!n || used!=token.size() || token[0]=='-')throw std::runtime_error("VIF zero-energy settling interval must be positive virtual CPU cycles");
        o.exploratoryVifZeroInputSettlingCycles=n;
      }
      else if(a=="--exploratory-vif-chip-revision-byte"){
        auto token=value(i);size_t used=0;auto n=std::stoull(token,&used,0);
        if(n>255 || used!=token.size())throw std::runtime_error("VIF chip revision hypothesis must be a byte");
        o.exploratoryVifRevision=int(n);
      }
      else if(a=="--exploratory-ci-detect-high")o.exploratoryCiDetectHigh=true;
      else if(a=="--exploratory-pq-sram-idle")o.exploratoryPqSramIdle=true;
      else if(a=="--exploratory-pq-disabled-ports")o.exploratoryPqDisabledPorts=true;
      else if (a == "--exploratory-scmi-format-byte"){
        auto v=std::stoul(value(i),nullptr,0);if(v>255)throw std::runtime_error("SCMI initial format byte must be 0..255");
        o.exploratoryScmiFormat=int(v);
      }
      else if (a == "--max-instructions")
        o.limit = std::stoull(value(i), nullptr, 0);
      else if (a == "--save-state")o.saveState = utf8Path(value(i));
      else if (a == "--save-state-at")o.saveStateAt = std::stoull(value(i), nullptr, 0);
      else if (a == "--save-state-stop")o.saveStateStop = true;
      else if (a == "--state-digest")o.stateDigest = true;
      else if (a == "--load-state-allow-extra-options")o.loadAllowExtraOptions = true;
      else if (a == "--profile")o.profile = utf8Path(value(i));
      else if (a == "--exploratory-gop-black-colorkey")o.videoPlaneBlackKey = true;
      else if (a == "--exploratory-video-plane"){
        o.videoPlane = value(i);
        if(o.videoPlane!="black" && o.videoPlane!="blue" && o.videoPlane!="setting")throw std::runtime_error("--exploratory-video-plane needs black, blue or setting");
      }
      else if (a == "--save-state-max-defer")o.saveStateMaxDefer = std::stoull(value(i), nullptr, 0);
      else if (a == "--load-state")o.loadState = utf8Path(value(i));
      else if (a == "--record-trace")
        o.trace = utf8Path(value(i));
      else if (a == "--replay-trace")
        o.replay = utf8Path(value(i));
      else if (a == "--candidate-gamma") o.candidateGamma = true;
      else if (a == "--exploratory-display-clock") o.exploratoryDisplayClock=std::stoull(value(i));
      else if(a=="--exploratory-subwindow-enable") {
        auto v=value(i);if(v!="0" && v!="1")throw std::runtime_error("Subwindow enable must be 0 or 1");
        o.exploratorySubwindowEnable=v=="1";
      }
      else if(a=="--exploratory-dlc-pending") {
        auto v=value(i);if(v!="0" && v!="1")throw std::runtime_error("DLC pending must be 0 or 1");
        o.exploratoryDlcPending=v=="1";
      }
      else if(a=="--exploratory-dlc-timeout") o.exploratoryDlcTimeout=true;
      else if(a=="--exploratory-comb-paired-restore") o.exploratoryCombRestore=true;
      else if(a=="--candidate-semaphore") o.candidateSemaphore=true;
      else if(a=="--candidate-ge") o.candidateGe=true;
      else if(a=="--candidate-ge-rect-fill") o.candidateGeRectFill=true;
      else if(a=="--candidate-ge-bitblt") o.candidateGeBitBlt=true;
      else if(a=="--candidate-tsp") o.candidateTsp=true;
      else if(a=="--exploratory-tsp-cpu") o.exploratoryTspCpu=true;
      else if(a=="--defer-platform-identity") o.deferPlatformIdentity=true;
      else if(a=="--exploratory-pm-ac-on") o.exploratoryPmAcOn=true;
      else if(a=="--exploratory-sar-fullscale") o.exploratorySarFullscale=true;
      else if(a=="--exploratory-scart-id-grounded") o.exploratoryScartIdGrounded=true;
      else if(a=="--exploratory-ir-dma-reset") o.exploratoryIrDmaReset=true;
      else if(a=="--exploratory-ir-capture-present") o.exploratoryIrCapturePresent=true;
      else if(a=="--candidate-pm-ir-fifo") o.candidatePmIrFifo=true;
      else if(a=="--exploratory-pm-ir-reset-empty") o.exploratoryPmIrResetEmpty=true;
      else if(a=="--exploratory-gop-reset-windows") o.exploratoryGopResetWindows=true;
      else if(a=="--or1k-trace-first-visits") o.orFirstVisits=true;
      else if(a=="--max-or1k-instructions") {o.orBudget=std::stoull(value(i));if(!o.orBudget)throw std::runtime_error("OR instruction budget must be positive");}
      else if(a=="--or1k-dispatch-divider") {o.orDivider=std::stoull(value(i));if(!o.orDivider || o.orDivider>1024)throw std::runtime_error("OR dispatch divider must be 1..1024");}
      else if(a=="--exploratory-tsp-reset-low") o.exploratoryTspResetLow=true;
      else if(a=="--candidate-gop-palette") o.candidateGopPalette=true;
      else if(a=="--exploratory-ge-reset") o.exploratoryGeReset=true;
      else if(a=="--exploratory-semaphore-idle") o.exploratorySemaphoreIdle=true;
      else if(a=="--exploratory-gop-bank") {
        auto b=std::stoul(value(i),nullptr,0);
        if(b>15)throw std::runtime_error("GOP bank out of range");
        o.exploratoryGopBank=int(b);
      }
      else if(a=="--exploratory-comb-bank") {
        auto b=std::stoul(value(i),nullptr,0);
        if(b>255)throw std::runtime_error("COMB bank out of range");
        o.exploratoryCombBank=int(b);
      }
      else if (a == "--exploratory-gamma-enable") {
        auto state = value(i);
        if (state != "0" && state != "1") throw std::runtime_error("Gamma enable must be 0 or 1");
        o.exploratoryGammaEnable = state == "1";
      }
      else if (a == "--exploratory-sc-bank") {
        auto bank = std::stoul(value(i), nullptr, 0);
        if (bank > 255) throw std::runtime_error("Exploratory SC bank out of range");
        o.exploratoryScBank = int(bank);
      }
      else if (a == "--window-preview-selftest")
        previewSelftest = value(i);
      else if (a == "--watch-pc-regs"){
        auto list=value(i);size_t pos=0;
        while(pos<list.size()){
          auto comma=list.find(',',pos);if(comma==std::string::npos)comma=list.size();
          auto token=list.substr(pos,comma-pos);size_t used=0;auto pc=std::stoul(token,&used,16);
          if(used!=token.size() || pc<0x80000000ul || (pc&3))throw std::runtime_error("--watch-pc-regs needs aligned KSEG0 hex addresses");
          o.watchPcRegs.push_back(uint32_t(pc));pos=comma+1;
        }
      }
      else if (a == "--watch-guest-access"){
        auto spec=value(i);auto at=spec.find('+');
        if(at==std::string::npos)throw std::runtime_error("--watch-guest-access needs HEXADDR+HEXLEN");
        o.watchData.push_back({uint32_t(std::stoul(spec.substr(0,at),nullptr,16)),uint32_t(std::stoul(spec.substr(at+1),nullptr,16))});
      }
      else if (a == "--dump-guest-range"){
        auto spec=value(i);auto at=spec.find('+');
        if(at==std::string::npos)throw std::runtime_error("--dump-guest-range needs HEXADDR+HEXLEN");
        o.dumpRanges.push_back({uint32_t(std::stoul(spec.substr(0,at),nullptr,16)),uint32_t(std::stoul(spec.substr(at+1),nullptr,16))});
      }
      else if (a == "--dump-prefix")
        o.dumpPrefix = utf8Path(value(i));
      else if (a == "--offscreen-inspector"){
        auto spec=value(i);   // ADDR:WIDTHxHEIGHT:PITCH (hex address, decimal geometry)
        unsigned w=0,h=0,p=0;char* end=nullptr;
        auto address=std::strtoul(spec.c_str(),&end,16);
        if(!end || *end!=':' || std::sscanf(end,":%ux%u:%u",&w,&h,&p)!=3 || !w || !h || p<w*2)throw std::runtime_error("--offscreen-inspector needs HEXADDR:WIDTHxHEIGHT:PITCH");
        o.offscreenInspector.push_back({uint32_t(address),w,h,p});
      }
      else if (a == "--preview-capture-offscreen")
        previewOptions.captureOffscreenPath = value(i);
      else if (a == "--gop-frame-history-at"){
        auto list=value(i);size_t pos=0;
        while(pos<list.size()){
          auto comma=list.find(',',pos);if(comma==std::string::npos)comma=list.size();
          auto token=list.substr(pos,comma-pos);size_t used=0;auto pc=std::stoul(token,&used,16);
          if(used!=token.size() || pc<0x80000000ul || pc>=0x81000000ul || (pc&3))throw std::runtime_error("--gop-frame-history-at needs aligned KSEG0 hex addresses");
          o.historyPcs.push_back(uint32_t(pc));pos=comma+1;
        }
      }
      else if (a == "--ir-keymap"){
        std::ifstream f(utf8Path(value(i)));
        if(!f)throw std::runtime_error("cannot read --ir-keymap file");
        std::stringstream ss;ss<<f.rdbuf();
        auto map=manta::IrKeyMap::parse(ss.str());
        for(auto& [k,v]:map.raw)o.irKeyMap.raw[k]=v;
        for(auto& [k,v]:map.evidence)o.irKeyMap.evidence[k]=v;
      }
      else if (a == "--ir-script"){
        auto items=manta::parseIrScript(value(i));
        o.irScript.insert(o.irScript.end(),items.begin(),items.end());
      }
      else if (a == "--exploratory-pm-ir-rpt-latch")o.exploratoryPmIrRptLatch=true;
      else if (a == "--gop-timeline-from")
        o.gopTimelineFrom = std::stoull(value(i));
      else if (a == "--gop-frame-history")
        o.gopFrameHistory = utf8Path(value(i));
      else if (a == "--preview-capture")
        previewOptions.capturePath = value(i);
      else if (a == "--preview-close-after-ms")
        previewOptions.closeAfterFinishMs = unsigned(std::stoul(value(i)));
      else if (a == "--export-gop-frame")
        o.exportGopFrame = utf8Path(value(i));
      else if (a == "--summary")
        o.summary = utf8Path(value(i));
      else if(a=="--tsp-firmware-dump") o.tspFirmwareDump=utf8Path(value(i));
      else if (a == "--coverage")
        o.coverage = utf8Path(value(i));
      else if (a == "--break-pc")
        o.breakpoint = uint32_t(std::stoul(value(i), nullptr, 0));
      else if (a == "--pause-on-mmio")
        o.pauseMmio = uint32_t(std::stoul(value(i), nullptr, 0));
      else if (a == "--help") {
        std::cout
            << "manta --firmware ORIGINAL.bin [--headless] [--nvram "
               "overlay.cow]\n"
               "--record-trace FILE --replay-trace FILE --trace-instructions\n"
               "--defer-diagnostic-reads: unknown register observations stay invalid; halt at unsafe use\n"
               "--virtual-rtc: guest-programmed counter with deterministic virtual clock\n"
               "--cp0-timer: source-built engine Count/Compare and guest interrupt entry\n"
               "--exploratory-cpi N: UNCALIBRATED average cycles/instruction 1(default),2,4,8,16; shared peripheral/Count clock; guarded exploration only\n"
               "--exploratory-copro: EXPLORATORY run the original multimedia coprocessor image (OpenRISC) after the guest releases it; unknown registers stop it\n"
               "--exploratory-copro-mips-irq N: EXPLORATORY raise MIPS FIQ hardware interrupt N (32..63 or 96..127) when the coprocessor replies; needs the IRQ-capable engine\n"
               "--diagnostic-riu-zero-unknown-reads: LABELLED HYPOTHESIS, DIAGNOSTIC ONLY: any never-defined MIPS-visible register reads as 0 (addresses listed in the summary); invalidates strict evidence\n"
               "--exploratory-copro-zero-unknown-reads: LABELLED HYPOTHESIS, unknown coprocessor registers read as 0\n"
               "--copro-probes: inspect original multimedia loader output in guest RAM; no processor/reset/status emulation\n"
               "--opaque-read-policy FILE: guarded INVALID observations at exact code/hash/PC/address/width only; no values or readiness\n"
               "--opaque-policy-wait-ms N: debugger freeze of ALL virtual clocks at unmatched reads, maximum300000; default0\n"
               "--or1k-no-qmem-trace: filter repetitive auxiliary QMEM write records only; memory effects, MMIO, mailbox events and pause points remain active\n"
               "--or1k-idle-poll-log-changes: filter repeated fully-known zero command polls at the same OR PC; all reads execute; commands, replies and pause points retained\n"
               "--or1k-register-log-changes: log first/changed internal values AND validity masks at each OR PC; duplicate records summarized; all reads execute; mailbox traffic and pause points retained\n"
               "--exploratory-gop-vsync-latch: PROVISIONAL global shadow-register update at programmed output VSYNC;200h force pulse; reset ACK/IRQ fields remain unknown\n"
               "--candidate-utmi: calibration handshake; analogue data unknown\n"
               "--candidate-chip-id: Titania4 identity 18h inferred from stock HAL identity check\n"
               "--candidate-spi: ISP read/status subset; unknown responses stop\n"
               "--spi-flash w25q64bv|mx25l6405d: separate documented chip models\n"
               "--defer-spi-probe: cache CF bytes as INVALID; stop before use\n"
               "--exploratory-spi-input pull-up|pull-down: HYPOTHETICAL undocumented CF output\n"
               "--exploratory-sc-bank N: HYPOTHETICAL initial SC selector (0..255); banked register state\n"
               "--candidate-gamma: original address/sample/channel transfers, provisional virtual timing\n"
               "--exploratory-display-clock HZ: HYPOTHETICAL output clock; guest timing drives output VSYNC\n"
               "--exploratory-subwindow-enable 0|1: HYPOTHETICAL power-on PIP enable bit, other bits remain unknown\n"
               "--exploratory-dlc-pending 0|1: HYPOTHETICAL pre-existing capture-range request; completion remains unknown\n"
               "--exploratory-dlc-timeout: HYPOTHETICAL held-low completion to test original timeout path\n"
               "--exploratory-comb-bank N: HYPOTHETICAL initial comb-filter bank selector\n"
               "--exploratory-comb-paired-restore: HYPOTHETICAL quarantined T4 duplicate/restore transaction, no invented status values\n"
               "--exploratory-gop-bank N: HYPOTHETICAL initial selector only (0..15); no IRQ/ACK reset assumption\n"
               "--candidate-semaphore: stateful resource-ID acquire/release; reset owners stay unknown\n"
                "--candidate-ge: stateful GE parameter queues; reset occupancy remains unknown\n"
                "--exploratory-avd-no-sync-events: HYPOTHESIS: with no sync input the AVD event latch 11FC bits 0 and 2 stay clear (other bits invalid); acks are clear-only; requires absent-HSYNC and shared AVD run\n"
                "--diagnostic-avd-invalid-publication: DIAGNOSTIC: AVD stores of invalid/partially known data into 11C0..11DF are kept with their exact known mask (no information created); requires the shared AVD run\n"
                "--exploratory-mmio-bits ...+rmw / +init: +rmw = the register is guest-writable and only MASK is hardware-owned (a store that carries MASK back unchanged is allowed); +init = RESET-VALUE hypothesis: the MASK bits start known with VALUE and the guest registers behave normally afterwards (bracket both values)\n"
                "--exploratory-mmio-bits HEXADDR:MASK:VALUE: HYPOTHESIS (bracket it), not a measurement: only the MASK bits of that hardware-owned status byte read as VALUE; other bits keep their provenance; guest writes rejected; requires guarded exploration (repeatable)\n"
                "--exploratory-afec-indirect-6a3c N: HYPOTHESIS (bracket it), not a measurement: the 16-bit content of the AFEC register indirectly addressed through 11A1:11A0 = 6A3C (+bit7) and read/written via 119D/119E starts as N 0..0xFFFF; requires guarded exploration\n"
                "--exploratory-avd-noise-constant N: HYPOTHESIS (bracket it), not a measurement: AFEC noise-magnitude byte (BF206A04) reads constant N 0..0xFF; requires the absent-HSYNC hypothesis and guarded exploration\n"
                "--exploratory-avd-htotal-constant N: HYPOTHESIS (bracket it), not a TV measurement: published AFEC HTotal (BF206B8D/BF206B90) reads constant N 0..0xFFFF; requires the absent-HSYNC hypothesis and guarded exploration\n"
                "--exploratory-ge-height5-full-copy: HYPOTHESIS, NOT TITANIA4-VERIFIED: GE copies/glyph blits cover every destination row although the original programs register 6F = height-5 (805241C4); relaxes the hidden-rows acceptance rule of the exploratory blend-copy and I2 glyph models\n"
                "--exploratory-ge-i8-fill: EXPLORATORY, NOT TITANIA4-VERIFIED: execute a plain GE fill whose destination format is 4 (8 bits per pixel per the original format table), writing the low byte of colour registers 70/71 as the index; requires --candidate-ge-rect-fill\n"
                "--exploratory-ge-line: HYPOTHESIS (unverified): GE command 0x10 axis-aligned one-pixel line into format C (colour quantized like the exploratory fill); other slopes stay refused; requires --candidate-ge-rect-fill\n"
                "--exploratory-ge-gradient-fill: EXPLORATORY, NOT TITANIA4-VERIFIED: execute GE command 2030 (solid fill plus per-channel 7.12 steps in registers 72..7D, as computed by the original routine 8052158C) into the format-C destination; accumulation origin, truncation and ARGB1555 quantization are assumed; requires --candidate-ge-rect-fill\n"
                "--exploratory-ge-nearest-stretch: EXPLORATORY, NOT TITANIA4-VERIFIED: also execute NON-uniform ARGB1555 -> format C stretched BitBlts (8040) by nearest-neighbour sampling floor(d*ratio/4096), phase 0, opaque source, dither/blend assumed no-ops; requires --exploratory-ge-uniform-stretch opaque-source\n"
                "--exploratory-ge-fill-ignore-reg00 MASK: EXPLORATORY, NOT TITANIA4-VERIFIED: treat GE register 00 bits 1..2 (set by the original code via 80520A30/80525B78, meaning unknown) as having no effect on a solid rectangle fill; requires --candidate-ge-rect-fill\n"
                "--exploratory-ge-copy-snapshot: HYPOTHESIS B4, NOT TITANIA4-VERIFIED: an overlapping blend copy (the 769-row page flip aliasing its first destination row) reads all source rows before writing; requires --exploratory-ge-blend-copy\n"
                "--exploratory-ge-blend-copy: EXPLORATORY, NOT TITANIA4-VERIFIED: execute the original ZUI offscreen-to-GOP ARGB1555->C copy (0040, control 0005, blend coefficient ONE read as replace); requires --candidate-ge-bitblt\n"
                "--exploratory-ge-i2-glyph: EXPLORATORY, NOT TITANIA4-VERIFIED: execute the original ZUI anti-aliased I2 glyph blit (0040, control 0045, formats 0C01) under hypotheses H1..H5 (see video/ge_i2_glyph.hpp); requires --candidate-ge-bitblt\n"
                "--exploratory-ge-uniform-stretch opaque-source|alpha-bit: EXPLORATORY, NOT TITANIA4-VERIFIED: execute a stretched GE BitBlt only when its whole sampled source is one colour (sampling/filter cannot matter) under the named alpha hypothesis; dither assumed no-op; requires --candidate-ge-bitblt\n"
                "--diagnostic-vif-cr-lock N: BRACKETING DIAGNOSTIC (run both 0 and 1), not a reset value: defines only 0x1F224471 bit0 (VIF carrier-recovery lock status); requires guarded exploration\n"
                "--diagnostic-avd-11db-bit7 N: BRACKETING DIAGNOSTIC (run both 0 and 1), not a reset value: defines AFEC 11DB bit 7 whose producer is unknown\n"
                "--candidate-spi-nor-write: opt-in guest SPI NOR WRSR/page-program/erase (06/01/02/20/52/D8) into a copy-on-write overlay; stock image never written; requires --candidate-spi\n"
                "--diagnostic-restore-by-identity-branch: DIAGNOSTIC ONLY: SerFlash write routine 8053CB80 branch whose both arms restore ISP 1C8 bit 2 to its tested value; requires --candidate-spi-nor-write\n"
                "--exploratory-provenance-extensions: opt-in generic extensions of the unknown-bit tracking: an identity read-write-back of an unknown register byte (LBU/ANDI FF/SB to the same address) and bit-field INS keep their bit identities instead of stopping; off by default so the strict boot stays byte-identical; requires guarded exploration\n"
                "--exploratory-usb-no-device: HYPOTHESIS: a guest-powered USB port-0 PHY with no device attached shows SE0, so USBC+0x10 line-state bits 7:6 read 0; all other USBC bits stay invalid; requires guarded exploration\n"
                "--diagnostic-usb-unpowered-dplus N: BRACKETING DIAGNOSTIC (run 0 and 1), not a reset value: line-state D+ (USBC+0x10 bit 6) of a USB port whose PHY the guest powered down; requires guarded exploration\n"
                "--diagnostic-htotal-branch-join: DIAGNOSTIC ONLY, not hardware behavior: execute both original outcomes of the HTotal comparison at 80501BFC separately, mark every differing RAM cell/register unknown, resume at the join 80501758\n"
                "--candidate-ge-bitblt: opt-in plain same-format RGB565/ARGB8888 0040 copies; requires GE queues; stretch/format C/alpha remain guarded\n"
               "--candidate-ge-rect-fill: opt-in known plain RGB565/ARGB8888 rectangle writes; requires GE queues, mapped RAM and known controls; format C/blending/BitBlt unsupported\n"
               "--candidate-tsp: queue count/full/empty derived from reset and actual pending descriptors; unsupported TS DMA never completes\n"
               "--exploratory-tsp-cpu: actual ORBIS32 execution with explicit hypothetical reset mapping/PC and 1:1 scheduling\n"
               "--defer-platform-identity: SHA-verified original cache stores keep unknown 0198/019C observations invalid; first consumer stops\n"
               "--exploratory-pm-ac-on: external PM boundary models documented ACON cause ff only; no PM51 readiness or input events supplied\n"
               "--exploratory-pm51-spi-startup: original PM CODE shares flash, XDATA shares RIU; hypothetical concurrent reset/SPI/RAM wiring; required unknown status stops PM before decisions\n"
               "--exploratory-pm-power-good: explicit aggregate powered-input hypothesis at PM 0EC2 bits0..2; individual detectors unverified, other bits remain invalid; requires PM startup\n"
               "--candidate-pm51-uart: R8051 baud/configuration SFR subset; no unmodeled UART completion or input supplied; requires PM startup\n"
               "--explore-pm-nonresume-branch: DIAGNOSTIC symbolic !=8 predicate only; PM activity byte stays invalid; no cold-boot/hardware verification claim\n"
               "--pm51-dispatch-divider N: hypothetical MIPS dispatches per PM machine tick (1..4096; default 32); not a measured clock\n"
               "--pm51-history FILE: bounded PM instruction/validity and bus history; requires original PM startup experiment\n"
               "--exploratory-sar-fullscale: 3300mV inputs/reference for SAR0/1; original power/free-run controls and period trigger conversion, board wiring unverified\n"
               "--exploratory-ir-dma-reset: original descriptor command commits an empty IR capture ring; 82h reset/index coupling unverified, no keys supplied\n"
               "--exploratory-ir-capture-present: separate bit7 capture-activity hypothesis derived from actual ring state, no external keys supplied\n"
               "--exploratory-scart-id-grounded: separate ADC2/3 external0mV fixtures; original conversion timing/power required, actual board wiring unverified\n"
               "--candidate-pm-ir-fifo: documented EMPTY/key, FIFO_CLR and read-pulse; repeat-latch timing and reset occupancy stay unknown\n"
               "--exploratory-pm-ir-reset-empty: separate fresh-empty FIFO reset hypothesis, requires candidate FIFO/guarded mode; no keys injected\n"
               "--exploratory-gop-reset-windows: original top-bank reset pulse clears GWIN enable bits only; exact reset scope unverified\n"
               "--exploratory-scart-grounded: explicit grounded FB input and hypothetical ADC zero code after original config; exact Titania4 transfer/pin mapping unverified\n"
               "--graphics-probes: trace original GOP window-programming entries and report valid guest register descriptors; does not fabricate pixels\n"
               "--exploratory-scmi-format-byte N: hypothetical initial main/sub bank12 format low byte; upper fields remain unknown\n"
               "--exploratory-fetch-revision-byte N: explicit unverified immutable BF003C05 byte; not a validated Titania4 stepping\n"
               "--candidate-vd-mcu-program: shared MCU upload/readback ports; current original payload is DVB-T; no synthetic acknowledgment\n"
                 "--candidate-avd-mcu-program: original analog-video MCU programming/readback port; no ready signaling\n"
                 "--exploratory-avd-shared-run: candidate XDATA/shared-register and private RAM wiring, standard Timer0 and explicit scheduling; no mailbox reset value\n"
                 "--diagnostic-avd-symbolic-code-tables: conservative uploaded-ROM data lookup masks; no selector or timing value supplied; requires shared AVD execution\n"
                 "--exploratory-avd-host-request-reset-empty: UNVERIFIED nonretained request-latch hypothesis; actual guest MCU hold clears only command bits 0C; no payload, bank, ready value or completion generated\n"
                 "--diagnostic-avd-deferred-startup-writes: separately retain unresolved original startup configuration writes; assume no asynchronous reset/clock disruption during bounded execution; no readback/status inferred\n"
                 "--avd-dispatch-divider N --max-avd-instructions N: explicit virtual-cycle ratio and original-instruction diagnostic budget\n"
                 "--candidate-audio-idma: observed 24-bit addressed upload/readback protocol, atomic functional timing; no DSP ISA/ready replies\n"
                 "--exploratory-audio-no-ack: constrain only original ack!=E3 predicate; byte remains INVALID, guest executes finite timeout; not a hardware reset-value/ready model\n"
                 "--exploratory-avd-mcu-startup: bounded original AVD execution under standard 8052 reset hypothesis; unknown XDATA/SFR stops\n"
                 "--exploratory-avd-suspended-boundary: suspend only AVD on an unknown transaction; no side effect or reply, other CPUs remain guarded\n"
               "--exploratory-vd-mcu-startup: execute original upload with standard 8052 reset/RAM hypothesis; all XDATA stops unresolved\n"
               "--defer-vd-sfr8e-write: separate diagnostic write-effect deferral; SFR identity/effects unknown and reads forbidden\n"
               "--defer-vd-reset-key-writes: defer only original AA/55/AA/55 XDATA 1080..1083 writes; effects unverified, no ready response\n"
               "--exploratory-vd-shared-run: original MCU on verified mailbox aliases; provisional divider4, private XRAM and INT0/Timer0 wiring; unknown data consumption stops\n"
               "--exploratory-vd-suspended-boundary: explicit stalled-MCU experiment; unresolved transaction has no effect/reply; MIPS continues only until unknown inputs\n"
                "--osd-probes: original ZUI state, graphics context, font renderer and window-creation entry traces\n"
                "--zui-flow-probes: hash-verified, read-only ZUI state/queue observations; no input or CPU control changes\n"
                "--exploratory-ci-detect-high: external CI detect bit2 high fixture; board polarity unverified, no module or acknowledgment\n"
               "--exploratory-analog-disconnected: external no-analog-input fixture supplies NORMAL Euro/Australia detection status only; positive patterns/timing unsupported\n"
               "--exploratory-avd-no-hsync: separate disconnected AFEC bit14 hypothesis; no READY value, other status/force modes/latency unresolved\n"
               "--exploratory-vif-chip-revision-byte N: separate unverified immutable revision byte at3D9D, not fetch3C05 or a ready status\n"
               "--exploratory-vif-zero-input-settle-cycles N: opt-in ideal noiseless IF equilibrium after explicit virtual cycles; no measured antenna noise, VGA or lock response\n"
               "--exploratory-pq-sram-idle: unvalidated initial idle state; original Y/C coefficient writes store real payloads with provisional deterministic latency\n"
               "--exploratory-pq-disabled-ports: separate unvalidated reset fixture with Y/C write-enable bits initially cleared\n"
               "--or1k-trace-first-visits: trace each original auxiliary PC once; every fetch still compared with immutable uploaded code and counted\n"
               "--max-or1k-instructions N: diagnostic auxiliary dispatch budget (default 10000000)\n"
               "--or1k-dispatch-divider N: EXPLORATORY MIPS-dispatches per auxiliary instruction (default 1); physical ratio unmeasured\n"
               "--exploratory-tsp-reset-low: HYPOTHETICAL engine control bit1 active-low reset coupling; no initial count assigned\n"
               "--candidate-gop-palette: guest color/index and original write-pulse palette uploads; no invented reset colors\n"
               "--exploratory-ge-reset: HYPOTHETICAL disabled engine/virtual queue, initially empty 16-entry stages with provisional clocks/latency\n"
               "--exploratory-semaphore-idle: HYPOTHETICAL initially unowned resources; no PM51/AEON ownership asserted\n"
               "--exploratory-gamma-enable 0|1: HYPOTHETICAL initial bank-10 gamma enable bit\n"
               "--window-preview-selftest FILE.bmp: presenter test with a labelled SYNTHETIC buffer (not guest output); saves the window content and exits\n"
               "--watch-pc-regs HEX,HEX: READ-ONLY probe; records registers and 8 stack words at the first 6 and last 18 executions of each original PC (summary watched_pc_hits)\n"
               "--dump-guest-range HEXADDR+HEXLEN --dump-prefix PREFIX: READ-ONLY; after the run write that guest physical range to PREFIX_<addr>.bin (repeatable)\n"
               "--offscreen-inspector HEXADDR:WIDTHxHEIGHT:PITCH (+ --preview-capture-offscreen FILE.bmp): DIAGNOSTIC read-only second view of a guest surface that is not a GOP window; press O in the window to toggle; never panel output\n"
               "--gop-frame-history-at HEX,HEX: also sample the GOP history whenever one of these original PCs executes (with --gop-frame-history)\n"
               "--ir-keymap FILE: function/raw-byte table (FUNCTION RAW [evidence]); only mapped functions can be delivered as emulated PM IR frames\n"
               "--ir-script DISPATCH:FUNCTION,...: deterministic scripted remote presses (FUNCTION or raw=0xNN), delivered to the emulated PM IR FIFO at/after that dispatch\n"
               "--exploratory-pm-ir-rpt-latch: HYPOTHESIS: IR RPT (3DAD bit0) latches the repeat flag of the entry just popped; requires --candidate-pm-ir-fifo\n"
               "--gop-frame-history DIR: DIAGNOSTIC: write a BMP of the GOP preview into DIR (plus index.json) each time its content changes (max 128); directory must exist\n"
               "--exploratory-gop-black-colorkey: HYPOTHESIS (unverified, needs --exploratory-video-plane): opaque pure-black guest pixels are treated as see-through (GOP transparent-colour key on black), so the video-plane colour shows behind them; presentation only\n"
               "--exploratory-video-plane black|blue|setting: EXPLORATORY presentation only: show a flat colour (instead of the neutral checker) where no guest graphics pixel exists (no GOP window, or ARGB1555 alpha bit clear); the emulator has no video plane. setting = the guest Blue Screen menu setting decides blue/black (original settings byte, this firmware image only). Not decoded video, not verified panel output; never changes guest state\n"
               "--preview-capture FILE.bmp / --preview-close-after-ms N: windowed mode only; save the window client area after the guest run stopped, then close (automation)\n"
               "--export-gop-frame FILE.json: after the run, decode the guest memory of every enabled+latched GOP window (format 6 assumed ARGB1555) to FILE_bankB_winW.bmp plus metadata; debug export only, no composition, never an authentic-frame claim\n"
               "--save-state FILE [--save-state-at DISPATCH] [--save-state-stop] [--save-state-max-defer N]: write a full-machine snapshot (CPU, RAM, flash overlay, every device, provenance) at the first safe instruction boundary at/after DISPATCH; versioned, per-section and overall SHA-256, committed atomically through a temporary file (docs/save-state.md)\n"
               "--load-state FILE: resume from a snapshot taken under the same hardware-affecting options (checksums, schema version, firmware digest and option fingerprint are verified before anything is applied)\n"
               "--load-state-allow-extra-options: also accept a snapshot whose recorded options are a strict subset of the current ones; the extra options are reported as snapshot_option_delta (use for labelled hypotheses added after the checkpoint)\n"
               "--state-digest: add per-section SHA-256 digests of the complete machine state to the summary (equivalence tests)\n"
               "--profile FILE: opt-in statistical profiler (instruction-pointer histogram of the emulation thread); never changes guest state\n"
               "--watch-guest-access HEXADDR+HEXLEN: READ-ONLY observation; the summary lists every instruction (pc, ra, count, first/last value) that loads from or stores to that guest physical range; repeatable\n"
               "--summary FILE --coverage FILE --max-instructions N\n"
               "--tsp-firmware-dump FILE: bounded private dump of original guest TSP_SetBase input; requires --candidate-tsp\n"
               "--break-pc PC --pause-on-mmio PHYSICAL_ADDRESS\n"
               "Diagnostic: --diagnostic-stage2 --symbolic-init "
               "--candidate-bdma\n"
               "Optional diagnostic: --diagnostic-init-prefix "
               "--candidate-uart\n"
               "Research peripherals: --candidate-miu --gpio-i2c\n"
               "Electrical fault fixture: --i2c-sda-stuck (first bus SDA held low)\n"
               "--window-smoke-test opens the native blank window briefly\n"
               "Unimplemented features/options fail explicitly; see "
               "docs/STATUS.md.\n";
        return 0;
      } else
        throw std::runtime_error("Unsupported option/feature: " + a);
    }
    if (!previewSelftest.empty()) {
      // Presenter self-test with a SYNTHETIC buffer (labelled as such; never guest output): proves the window draws a published
      // frame pixel-exactly and shows uncovered pixels as the checker. Used only by tests/window_preview_regression.py.
      auto store = std::make_shared<manta::GopPreviewStore>();
      manta::GopPreviewFrame f;
      f.width = 64; f.height = 32; f.bgra.assign(64 * 32, 0); f.covered.assign(64 * 32, 0);
      for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 32; ++x) { f.bgra[y * 64 + x] = 0xffff0000u; f.covered[y * 64 + x] = 1; }
      for (unsigned y = 16; y < 32; ++y) for (unsigned x = 32; x < 64; ++x) { f.bgra[y * 64 + x] = 0xff0000ffu; f.covered[y * 64 + x] = 1; }
      f.label = "SELFTEST - synthetic buffer for presenter testing, NOT guest output";
      f.description = "selftest 64x32";
      store->publish(std::move(f)); store->setFinished();
      manta::PreviewWindowOptions opt; opt.capturePath = previewSelftest; opt.closeAfterFinishMs = 100;
      opt.injectSelftestKeys = {77, 83, 27, 13, 38, 40, 37, 39, 80, 73, 79, 88};   // M S Esc Enter Up Down Left Right P I O X(unmapped)
      manta::nativeWindow("", false, store, opt);
      return 0;
    }
    if (windowSmoke) {
      if (headless) throw std::runtime_error("Window smoke test cannot be headless");
      manta::nativeWindow("", true);
      return 0; // tests frontend creation, independent of guest stop code 2
    }
    if (firmware.empty()) {
      if (headless)
        throw std::runtime_error("--firmware required in headless mode");
      firmware = manta::pickFirmware();
      if (firmware.empty())
        return 1;
    }
    // Debug exports must never truncate the selected stock image, including
    // an alias reached through a Windows hard link or symbolic link.
    for(const auto& output:{o.trace,o.summary,o.exportGopFrame,o.gopFrameHistory,o.coverage,o.tspFirmwareDump,o.pm51History,nvram}){
      if(output.empty())continue;
      std::error_code ec;
      bool alias=std::filesystem::equivalent(firmware,output,ec);
      if(alias || std::filesystem::weakly_canonical(firmware)==std::filesystem::weakly_canonical(output))
        throw std::runtime_error("output/NVRAM path aliases immutable firmware input");
    }
    auto flash = manta::Flash::loadOriginal(firmware);
    if (!nvram.empty() && std::filesystem::exists(nvram))
      flash.loadOverlay(nvram);
    // SPI programming is not connected yet. No pretend guest settings are
    // saved.
    if (!headless) { o.previewStore = std::make_shared<manta::GopPreviewStore>(); o.remoteQueue = o.previewStore->remote; }
    if (!headless && o.irKeyMap.raw.empty())   // default evidence-backed keymap, when present next to the working directory
      for (const char* candidate : {"configs/input/ir-keymap.tsv", "outputs/manta-emulator/configs/input/ir-keymap.tsv"}) {
        std::ifstream f(candidate);
        if (!f) continue;
        std::stringstream ss; ss << f.rdbuf();
        auto map = manta::IrKeyMap::parse(ss.str());
        o.irKeyMap = map;
        break;
      }
    o.stateFingerprint = stateFingerprint(args, o.stateCanonical);
    auto previewStore = o.previewStore;
    manta::Msd309Machine machine(std::move(flash), o);
    if (headless) return machine.run();
    // Create the native display immediately. Guest initialization may run for
    // many seconds; the OS message loop must remain responsive throughout.
    auto guest = std::async(std::launch::async, [&machine, previewStore] {
      int rc;
      try { rc = machine.run(); } catch (...) { previewStore->setFinished(); throw; }
      previewStore->setFinished();
      return rc;
    });
    manta::nativeWindow("", false, previewStore, previewOptions);
    return guest.get();
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    return 1;
  }
}
int main(int argc, char **argv) {
  std::vector<std::string> args;
#ifdef _WIN32
  // Preserve Polish filenames independently of the console ANSI code page.
  int count = 0;
  auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!wideArgs)
    return 1;
  for (int i = 0; i < count; ++i) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, s.data(), bytes, nullptr, nullptr);
    s.pop_back();
    args.push_back(std::move(s));
  }
  LocalFree(wideArgs);
  (void)argc;
  (void)argv;
#else
  for (int i = 0; i < argc; ++i)
    args.emplace_back(argv[i]);
#endif
  return appMain(args);
}
