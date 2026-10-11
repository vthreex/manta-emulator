#include "board/machine.hpp"
#include "ui/native_window.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
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
      else if (a == "--max-instructions")
        o.limit = std::stoull(value(i), nullptr, 0);
      else if (a == "--record-trace")
        o.trace = utf8Path(value(i));
      else if (a == "--replay-trace")
        o.replay = utf8Path(value(i));
      else if (a == "--summary")
        o.summary = utf8Path(value(i));
      else if (a == "--coverage")
        o.coverage = utf8Path(value(i));
      else if (a == "--break-pc")
        o.breakpoint = uint32_t(std::stoul(value(i), nullptr, 0));
      else if (a == "--pause-on-mmio")
        o.pauseMmio = uint32_t(std::stoul(value(i), nullptr, 0));
      else if (a == "--help") {
        std::cout << "manta --firmware ORIGINAL.bin [--headless] [--nvram "
                     "overlay.cow]\n"
                     "--record-trace FILE --replay-trace FILE --trace-instructions\n"
                     "--summary FILE --coverage FILE --max-instructions N\n"
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
    auto code = machine.run();
    if (!headless)
      manta::nativeWindow(machine.summary(), windowSmoke);
    return code;
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
