#include "board/machine.hpp"
#include "ui/native_window.hpp"
#include <filesystem>
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
static int appMain(const std::vector<std::string> &args) {
  try {
    manta::Options o;
    std::filesystem::path firmware, nvram;
    bool headless = false, windowSmoke = false;
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
      else if (a == "--opaque-read-policy")o.opaqueReadPolicy=value(i);
      else if (a == "--opaque-policy-wait-ms")o.opaquePolicyWaitMs=std::stoull(value(i),nullptr,0);
      else if (a == "--or1k-no-qmem-trace")o.noOrQmemTrace=true;
      else if (a == "--or1k-idle-poll-log-changes")o.orIdlePollChanges=true;
      else if (a == "--or1k-register-log-changes")o.orRegisterLogChanges=true;
      else if (a == "--exploratory-gop-vsync-latch")o.exploratoryGopVsyncLatch=true;
      else if (a == "--exploratory-scart-grounded")o.exploratoryScartGrounded=true;
      else if (a == "--graphics-probes")o.graphicsProbes=true;
      else if (a == "--osd-probes")o.osdProbes=true;
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
    for(const auto& output:{o.trace,o.summary,o.coverage,o.tspFirmwareDump,o.pm51History,nvram}){
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
    manta::Msd309Machine machine(std::move(flash), o);
    if (headless) return machine.run();
    // Create the native display immediately. Guest initialization may run for
    // many seconds; the OS message loop must remain responsive throughout.
    auto guest = std::async(std::launch::async, [&machine] { return machine.run(); });
    manta::nativeWindow("", false);
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
