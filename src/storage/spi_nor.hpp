#pragma once
#include "snapshot/archive.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
namespace manta {
enum class NorModel { W25Q64BV, MX25L6405D };
// Undocumented output is neither a manufacturer ID nor proven high impedance.
// Only explicit electrical experiments resolve it to an assumed level.
enum class SpiUnknownInput { Stop, Deferred, AssumedPullUp, AssumedPullDown };
struct SpiSample { uint8_t value = 0xa5, known = 0; };
class SpiNor {
  friend void ioClass(snap::Ar&, SpiNor&);
  std::vector<uint8_t> packet_;
  unsigned outputIndex_ = 0;
  bool selected_ = false, wel_ = false, asleep_ = false;
  uint64_t busyUntil_ = 0;
  uint8_t sr_ = 0;          // SRP0 SEC TB BP2 BP1 BP0 (bits 7..2) as last written by the guest
  bool srKnown_ = false;
  // Protected array range for the guest-written block-protect bits. Only the patterns the original
  // firmware has been observed to need are modeled; anything else stops instead of guessing.
  bool protectedAt(uint32_t address, uint32_t size) {
    unsigned bp = (sr_ >> 2) & 7;
    if (bp == 0) return false;
    if (bp == 7) return true;                       // all blocks protected for every TB setting
    if (sr_ & 0x40) {                               // SEC=1 (4 KiB granularity table) is not modeled
      error = "NOR SEC block-protect pattern not modeled: status " + std::to_string(sr_);
      return true;
    }
    // SEC=0 table of the Winbond 64 Mbit family (chip marking on the board is unverified): BP2:0 = 1..6 protect
    // 64 KiB << (BP-1) at the top (TB=0) or the bottom (TB=1) of the 8 MiB array.
    uint32_t length = 0x10000u << (bp - 1);
    uint32_t begin = (sr_ & 0x20) ? 0 : 0x800000u - length, end = begin + length;
    return address < end && uint64_t(address) + size > begin;
  }
  // Rising CS edge: page program (02) and erase (20/52/D8) take effect only now. A real device
  // silently ignores them without WEL or while busy; that is counted, never reported as success.
  void commit() {
    if (!writeSupport || !selected_ || packet_.empty() || asleep_ || !error.empty()) return;
    auto cmd = packet_[0];
    // Write-class opcodes the original firmware has not been observed to use stay guarded, never ignored.
    for (uint8_t unsupported : {0x11, 0x31, 0x42, 0x44, 0x50, 0x60, 0xc7}) {
      if (cmd == unsupported) { error = "unsupported SPI write-class opcode " + std::to_string(cmd); return; }
    }
    if (cmd == 0x01) {   // Write Status Register: observed in the original boot (06 then 01 BC)
      if (packet_.size() < 2 || packet_.size() > 3) { error = "unsupported WRSR framing"; return; }
      if (busy() || !wel_) { ++ignoredWrites; return; }
      // DIAGNOSTIC: WP# level is not modeled (the polarity of the ISP register controlling it is unestablished), so SRP0 never blocks.
      sr_ = packet_[1] & 0xfc;
      srKnown_ = true;
      ++statusWrites;
      busyUntil_ = now + statusWriteTicks;
      wel_ = false;
      return;
    }
    bool program = cmd == 0x02 && packet_.size() >= 5;
    bool erase = (cmd == 0x20 || cmd == 0x52 || cmd == 0xd8) && packet_.size() == 4;
    if (!program && !erase) return;
    if (busy() || !wel_) { ++ignoredWrites; return; }
    if (!srKnown_) { error = "NOR program/erase with unknown block-protect state"; return; }
    uint32_t address = ((uint32_t(packet_[1]) << 16) | (uint32_t(packet_[2]) << 8) | packet_[3]) & 0x7fffff;
    if (program) {
      if (!flashProgram) { error = "NOR program without backing flash"; return; }
      if (protectedAt(address, uint32_t(packet_.size() - 4))) {
        if (error.empty()) { ++blockedByProtection; wel_ = false; }
        return;
      }
      size_t count = packet_.size() - 4;
      if (count > 256) { error = "NOR page-program payload exceeds 256 bytes"; return; }
      // Documented behavior: addressing wraps inside the 256-byte page.
      uint32_t page = address & ~0xffu, offset = address & 0xffu;
      std::vector<uint8_t> bytes(packet_.begin() + 4, packet_.end());
      for (size_t i = 0; i < count;) {
        size_t run = std::min(count - i, size_t(256 - offset));
        norViolations += flashProgram(page + offset, std::span<const uint8_t>(bytes).subspan(i, run));
        i += run;
        offset = 0;
      }
      programmedBytes += count;
      ++programs;
      busyUntil_ = now + programTicks;
    } else {
      if (!flashErase) { error = "NOR erase without backing flash"; return; }
      uint32_t size = cmd == 0x20 ? 4096 : cmd == 0x52 ? 32768 : 65536;
      if (protectedAt(address & ~(size - 1), size)) {
        if (error.empty()) { ++blockedByProtection; wel_ = false; }
        return;
      }
      flashErase(address & ~(size - 1), size);
      ++erases;
      busyUntil_ = now + (cmd == 0x20 ? erase4kTicks : cmd == 0x52 ? erase32kTicks : erase64kTicks);
    }
    wel_ = false;
  }

public:
  NorModel model = NorModel::W25Q64BV;
  SpiUnknownInput unknownInput = SpiUnknownInput::Stop;
  int statusHypothesis = -1;   // --exploratory-spi-status 0xNN: HYPOTHETICAL value of the status bits the model cannot know (SRP/BP/QE); BUSY/WEL stay modelled
  std::function<uint8_t(uint32_t)> flashByte;
  // Program/erase effects are delegated to the owner (copy-on-write flash). programmed bytes can only
  // clear bits; the callback returns how many bytes asked for a 0->1 change.
  std::function<size_t(uint32_t, std::span<const uint8_t>)> flashProgram;
  std::function<void(uint32_t, uint32_t)> flashErase;
  std::string error;
  unsigned undocumentedSamples = 0;
  // DIAGNOSTIC virtual-cycle (312 MHz domain used elsewhere) busy intervals. Typical figures of the
  // Winbond W25Q64 family datasheet: page program 0.7 ms, 4 KiB erase ~30 ms, 32 KiB ~120 ms, 64 KiB ~150 ms.
  // The chip marking on the board is unverified and the firmware only polls status bit 0.
  uint64_t now = 0;
  uint64_t programTicks = 218400, erase4kTicks = 9360000, erase32kTicks = 37440000, erase64kTicks = 46800000;
  // Opt-in (--candidate-spi-nor-write). Off: program/erase/WRSR are ignored exactly as before, so the strict boot is unchanged.
  bool writeSupport = false;
  uint64_t statusWriteTicks = 3120000;   // ~10 ms typical status-register write time
  uint64_t statusWrites = 0, blockedByProtection = 0;
  uint64_t programs = 0, erases = 0, ignoredWrites = 0, norViolations = 0, programmedBytes = 0;
  bool busy() const { return writeSupport && now < busyUntil_; }
  bool writeEnabled() const { return wel_; }
  bool statusKnown() const { return srKnown_; }
  uint8_t statusBits() const { return sr_; }
  void reset() { selected_ = false; wel_ = false; asleep_ = false; busyUntil_ = 0; srKnown_ = false; sr_ = 0; packet_.clear(); outputIndex_ = 0; error.clear(); }
  const char *name() const { return model == NorModel::W25Q64BV ? "w25q64bv" : "mx25l6405d"; }
  bool selected() const { return selected_; }
  const auto &packet() const { return packet_; }
  uint8_t manufacturer() const { return model == NorModel::W25Q64BV ? 0xef : 0xc2; }
  void select() { selected_ = true; packet_.clear(); outputIndex_ = 0; }
  void deselect() {
    commit();
    // These one-byte commands take effect on CS rising, not on opcode arrival.
    if (selected_ && packet_.size() == 1) {
      if (packet_[0] == 6 && !asleep_ && !(writeSupport && busy())) wel_ = true;
      if (packet_[0] == 4 && !asleep_ && !(writeSupport && busy())) wel_ = false;
      if (packet_[0] == 0xb9) asleep_ = true;
      if (packet_[0] == 0xab) asleep_ = false;
    }
    selected_ = false; packet_.clear(); outputIndex_ = 0;
  }
  void transmit(uint8_t b) {
    if (!selected_) { error = "SPI transmit without asserted CS"; return; }
    if (packet_.size() >= 260) { error = "unsupported SPI packet length"; return; }
    packet_.push_back(b);
  }
  SpiSample receive() {
    if (!selected_ || packet_.empty()) { error = "SPI receive without opcode/CS"; return {}; }
    auto cmd = packet_[0];
    if (asleep_ && cmd != 0xab) { error = "SPI command while NOR in deep power-down"; return {}; }
    // While a program/erase is in progress only status reads are accepted by the device.
    if (writeSupport && busy() && cmd != 5) { error = "SPI read command while NOR busy"; return {}; }
    SpiSample out;
    if (cmd == 5 && packet_.size() == 1) {
      // Persistent protection/SRP/QE bits cannot be inferred from array bytes; only BUSY and WEL are known.
      out = {uint8_t(0xa4 | (wel_ ? 2 : 0) | (writeSupport && busy() ? 1 : 0)), 3};
      if (statusHypothesis >= 0) out = {uint8_t((uint8_t(statusHypothesis) & ~3u) | (wel_ ? 2 : 0) | (writeSupport && busy() ? 1 : 0)), 255};
    } else if (cmd == 0x9f && packet_.size() == 1 && outputIndex_ < 3) {
      std::array<uint8_t, 3> id = {manufacturer(), uint8_t(model == NorModel::W25Q64BV ? 0x40 : 0x20), 0x17};
      out = {id[outputIndex_], 255};
    } else if (cmd == 0x90 && packet_.size() == 4 && packet_[3] <= 1) {
      out = {uint8_t(((outputIndex_ + packet_[3]) & 1) ? 0x16 : manufacturer()), 255};
    } else if (cmd == 0xab && packet_.size() == 4) {
      asleep_ = false; out = {0x16, 255};
    } else if ((cmd == 3 && packet_.size() == 4) || (cmd == 0x0b && packet_.size() == 5)) {
      if (!flashByte) { error = "SPI array read without backing NOR"; return {}; }
      uint32_t a = (uint32_t(packet_[1]) << 16) | (uint32_t(packet_[2]) << 8) | packet_[3];
      // Documented continuous array read wraps at the device's upper boundary.
      out = {flashByte((a + outputIndex_) & 0x7fffff), 255};
    } else if (cmd == 0xcf && packet_.size() == 4) {
      // Generic MStar ReadREMS4 probe, NOT REMS2. CF is undocumented for both
      // configured chips. Do not import a different Macronix part's response.
      ++undocumentedSamples;
      if (unknownInput == SpiUnknownInput::Stop) {
        error = "undocumented SPI opcode 0xcf for " + std::string(name()) + "; input electrical state unknown";
        return {};
      }
      if (unknownInput == SpiUnknownInput::AssumedPullUp) out = {255, 255};
      if (unknownInput == SpiUnknownInput::AssumedPullDown) out = {0, 255};
      // Deferred leaves known=0; the board must guard the exact guest cache.
    } else {
      error = "unsupported SPI command/framing for " + std::string(name()) + " opcode " + std::to_string(cmd);
      return {};
    }
    ++outputIndex_;
    return out;
  }
};
} // namespace manta
