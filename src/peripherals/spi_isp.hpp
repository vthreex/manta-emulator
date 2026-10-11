#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace manta {
// Stock command/data/read strobes and FIFO-ready registers are corroborated
// by 8053C19C..C250 and 8053C574..C910. Only observed read transactions are
// implemented. Flash status protection bits and unsupported opcodes stop.
class SpiIsp {
  bool clockConfigured_ = false, enabled_ = false, trigger_ = false, readPending_ = false;
  uint64_t commandReady_ = 0, writeReady_ = 0, readReady_ = 0;
  std::vector<uint8_t> packet_;
  unsigned readIndex_ = 0;
  uint32_t input_ = 0, inputKnown_ = 0;

public:
  std::string error;
  std::function<uint8_t(uint32_t)> flashByte;
  void write(unsigned offset, uint16_t value, uint64_t tick) {
    if (offset == 0x18) {
      clockConfigured_ = value == 0x40;
      return;
    }
    if (offset == 0) {
      enabled_ = value == 0xaaaa && clockConfigured_;
      if (value == 0xaaaa && !clockConfigured_)
        error = "SPI enable requires supported guest-programmed clock divider";
      packet_.clear();
      readPending_ = false;
      commandReady_ = tick + 64;
      return;
    }
    if (offset == 0xa8) {
      trigger_ = value == 0x3333;
      packet_.clear();
      readIndex_ = 0;
      return;
    }
    if (offset == 0x20 && (value & 1)) {
      packet_.clear();
      readIndex_ = 0;
      readPending_ = false;
      return;
    }
    if (!enabled_) {
      error = "SPI transaction while ISP engine disabled";
      return;
    }
    if (offset == 4) {
      packet_ = {uint8_t(value)};
      readIndex_ = 0;
      commandReady_ = tick + 64;
    } else if (offset == 0x10 && trigger_) {
      packet_.push_back(uint8_t(value));
      writeReady_ = tick + 64;
    } else if (offset == 0x30 && (value & 1)) {
      if (packet_.empty()) {
        error = "SPI read strobe without original command";
        return;
      }
      auto cmd = packet_[0];
      inputKnown_ = 0;
      input_ = 0xa5;
      if (cmd == 5) {
        input_ = 0xa4;
        inputKnown_ = 3;
      } // power-on idle WIP=0,WEL=0; protection bits unknown
      else if (cmd == 0x9f) {
        constexpr uint8_t id[] = {0xef, 0x40, 0x17};
        input_ = readIndex_ < 3 ? id[readIndex_] : 0xff;
        inputKnown_ = 255;
      } else if (cmd == 3 && packet_.size() == 4 && flashByte) {
        uint32_t address = (uint32_t(packet_[1]) << 16) | (uint32_t(packet_[2]) << 8) | packet_[3];
        if (address + readIndex_ >= 0x800000) {
          error = "SPI read exceeds 8 MiB NOR capacity";
          return;
        }
        input_ = flashByte(address + readIndex_);
        inputKnown_ = 255;
      } else {
        error = cmd == 0xcf
                    ? "unresolved SPI 0xcf REMS2 response"
                    : "unsupported SPI opcode " + std::to_string(cmd) + "; response is unknown";
        return;
      }
      ++readIndex_;
      readPending_ = true;
      readReady_ = tick + 64;
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
        (consumed & inputKnown_) == consumed) {
      readPending_ = false;
      return 0xa500 | input_;
    }
    return std::nullopt;
  }
};
} // namespace manta
