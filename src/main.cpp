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
