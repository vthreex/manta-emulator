#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "storage/spi_nor.hpp"
namespace manta {
// Stock command/data/read strobes and FIFO-ready registers are corroborated
// by 8053C19C..C250 and 8053C574..C910. Only observed read transactions are
// implemented. Flash status protection bits and unsupported opcodes stop.
class SpiIsp {
  bool clockConfigured_ = false, enabled_ = false, trigger_ = false, readPending_ = false;
  uint64_t commandReady_ = 0, writeReady_ = 0, readReady_ = 0;
  uint32_t input_ = 0, inputKnown_ = 0;

public:
  SpiNor nor;
  std::function<void(const std::string &, uint32_t)> busEvent;
  std::string error;
  std::function<uint8_t(uint32_t)> flashByte;
  unsigned dataKnown() const { return inputKnown_; }
  bool dataReady(uint64_t tick) const { return readPending_ && tick >= readReady_; }
  void endTransaction() {
    if (nor.selected() && busEvent) busEvent("spi_cs", 1);
    nor.deselect(); readPending_ = false;
  }
  void transmit(uint8_t b, uint64_t tick) {
    if (!nor.selected()) {
      nor.select();
      if (busEvent) busEvent("spi_cs", 0);
    }
    nor.transmit(b);
    if (busEvent) busEvent("spi_tx", b);
    writeReady_ = commandReady_ = tick + 64; // 8 MSB-first clocks, selected divide-by-8
  }
  void write(unsigned offset, uint16_t value, uint64_t tick) {
    if (offset == 0x18) {
      clockConfigured_ = value == 0x40;
      return;
    }
    if (offset == 0) {
      enabled_ = value == 0xaaaa && clockConfigured_;
      if (value == 0xaaaa && !clockConfigured_)
        error = "SPI enable requires supported guest-programmed clock divider";
      endTransaction();
      commandReady_ = tick + 64;
      return;
    }
    if (offset == 0xa8) {
      trigger_ = value == 0x3333;
      if (!trigger_) endTransaction();
      return;
    }
    if (offset == 0x20 && (value & 1)) {
      endTransaction();
      return;
    }
    if (!enabled_) {
      error = "SPI transaction while ISP engine disabled";
      return;
    }
    if (offset == 4) {
      if (tick < commandReady_) { error = "SPI command FIFO overrun"; return; }
      endTransaction();
      transmit(uint8_t(value), tick);
    } else if (offset == 0x10 && trigger_) {
      if (tick < writeReady_) { error = "SPI transmit FIFO overrun"; return; }
      transmit(uint8_t(value), tick);
    } else if (offset == 0x30 && (value & 1)) {
      if (readPending_) { error = "SPI receive FIFO overrun"; return; }
      nor.flashByte = flashByte;
      auto sample = nor.receive();
      if (!nor.error.empty()) { error = nor.error; return; }
      input_ = sample.value; inputKnown_ = sample.known;
      if (busEvent) busEvent(sample.known ? "spi_rx_sample" : "spi_rx_unknown", input_);
      readPending_ = true;
      readReady_ = (tick > writeReady_ ? tick : writeReady_) + 64;
    }
  }
  std::optional<uint32_t> read(unsigned offset, uint32_t consumed, uint64_t tick) {
    if (!enabled_)
      return std::nullopt;
    if (offset == 0x5c && consumed == 1)
      return tick >= commandReady_;
    if (offset == 0x58 && consumed == 1)
      return tick >= writeReady_;
    if (offset == 0x54 && consumed == 1)
      return readPending_ && tick >= readReady_;
    if (offset == 0x14 && readPending_ && tick >= readReady_ && consumed &&
        ((consumed & inputKnown_) == consumed || (!inputKnown_ && nor.unknownInput == SpiUnknownInput::Deferred))) {
      readPending_ = false;
      return 0xa500 | input_;
    }
    return std::nullopt;
  }
};
} // namespace manta
