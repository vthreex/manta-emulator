#pragma once
#include "snapshot/archive.hpp"
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
  friend void ioClass(snap::Ar&, SpiIsp&);
  bool clockConfigured_ = false, enabled_ = false, trigger_ = false, readPending_ = false;
  uint64_t commandReady_ = 0, writeReady_ = 0, readReady_ = 0;
  uint32_t input_ = 0, inputKnown_ = 0;
  // MST786 ISP table (bank 08, index*4 = MIPS offset): 04 SPI_COMMAND (write starts the operation),
  // 08/0C ADDRESS1-3, 10 WDATA, 14 RDATA, 20 SPI_CE_CLR. Commands that carry an address phase send the three
  // address bytes from ADDRESS1-3 automatically; the original page-program routine relies on exactly this.
  uint32_t address_ = 0;
  uint64_t lastTick_ = 0;
  // Command-mode (write support) timing: the one-entry WDATA buffer (SPI_WR_DATA_RDY) is free until a byte is queued and
  // drains when the shifter starts it; the shifter is busy through the opcode/address phase and every queued byte.
  uint64_t shiftEnd_ = 0;
  bool commandSession_ = false;
  bool soleWren_ = false;   // the selected transaction is exactly one WREN (06) byte sent through SPI_COMMAND
  static bool addressCommand(uint8_t c) { return c == 0x02 || c == 0x20 || c == 0x52 || c == 0xd8; }

public:
  SpiNor nor;
  std::function<void(const std::string &, uint32_t)> busEvent;
  std::string error;
  std::function<uint8_t(uint32_t)> flashByte;
  unsigned dataKnown() const { return inputKnown_; }
  bool pending() const { return readPending_; }
  bool dataReady(uint64_t tick) const { return readPending_ && tick >= readReady_; }
  void endTransaction() {
    if (nor.selected() && busEvent) busEvent("spi_cs", 1);
    nor.now = nor.writeSupport && shiftEnd_ > lastTick_ ? shiftEnd_ : lastTick_; nor.deselect(); readPending_ = false; commandSession_ = false; soleWren_ = false;
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
    lastTick_ = tick; nor.now = tick;
    if (nor.writeSupport && offset == 8) { address_ = (address_ & 0xff0000u) | value; return; }
    if (nor.writeSupport && offset == 0xc) { address_ = (address_ & 0xffffu) | (uint32_t(value & 0xff) << 16); return; }
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
      // The original block-erase routine 8053CE50 sends WREN through SPI_COMMAND, then 3333 here and D8 + three address bytes through WDATA:
      // D8 can only start a new command if the WREN transaction ended (CS rise, which also latches WEL) before the data-mode session.
      if (value == 0x3333 && soleWren_ && nor.selected()) endTransaction();
      soleWren_ = false;
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
      soleWren_ = nor.writeSupport && uint8_t(value) == 0x06;
      if (nor.writeSupport && uint8_t(value) == 0x01) {   // WRSR: status byte follows through WDATA
        commandSession_ = true;
        shiftEnd_ = tick + 64;
        writeReady_ = tick;
      }
      if (nor.writeSupport && addressCommand(uint8_t(value))) {
        nor.transmit(uint8_t(address_ >> 16));
        nor.transmit(uint8_t(address_ >> 8));
        nor.transmit(uint8_t(address_));
        if (busEvent) busEvent("spi_addr", address_ & 0xffffff);
        writeReady_ = commandReady_ = tick + 64 * 4;
        shiftEnd_ = tick + 64 * 4;
        if (value == 0x02) writeReady_ = tick;   // buffer is free while the opcode and address are shifted out
        commandSession_ = value == 0x02;   // page program continues with WDATA bytes until CS is released
      }
    } else if (offset == 0x10 && nor.writeSupport && commandSession_ && !trigger_) {
      if (tick < writeReady_) { error = "SPI transmit FIFO overrun"; return; }
      uint64_t start = tick > shiftEnd_ ? tick : shiftEnd_;
      nor.transmit(uint8_t(value));
      if (busEvent) busEvent("spi_tx", uint8_t(value));
      shiftEnd_ = start + 64;
      writeReady_ = start;           // buffer empties when the byte starts shifting
      commandReady_ = shiftEnd_;
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
