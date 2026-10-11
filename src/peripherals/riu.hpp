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
#include "video/gamma.hpp"
#include "video/scaler_timing.hpp"
namespace manta {
struct RiuByte {
  uint8_t value = 0xa5, known = 0;
};
class Riu {
  std::map<uint32_t, RiuByte> bytes_;
  uint64_t bdmaComplete_ = 0;
  bool bdmaPending_ = false;
  std::map<uint32_t, uint32_t> pendingRmw_;
  std::map<uint32_t, uint32_t> consumedRead_;
  std::optional<uint32_t> opaqueRead_;
  bool uartEnabled_ = false;
  GpioI2c gpio_;
  Rtc rtc_[2];
  UtmiCalibration utmi_[2];
  SpiIsp spi_;
  GammaRam gamma_;
  ScalerTiming scaler_;
  std::optional<bool> lastOutputSync_;
  uint64_t outputSyncReads_=0, outputSyncTransitions_=0;
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
  void prepareMaskedRead(uint32_t a, uint32_t consumed) { consumedRead_[a] = consumed; }
  void prepareOpaqueRead(uint32_t a) { opaqueRead_=a; }
  uint32_t knownBits(uint32_t a, unsigned size) const;
  uint32_t controlByteKey(uint32_t a) const { return storageKey(a); }
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
  uint32_t read(uint32_t a, unsigned size, uint32_t pc, uint32_t mask, uint64_t tick);
  void write(uint32_t a, unsigned size, uint32_t value, uint32_t pc, uint32_t mask, uint64_t tick);
  static uint32_t crcMsb(const std::vector<uint8_t> &data, uint32_t polynomial, uint32_t seed);
};
} // namespace manta
