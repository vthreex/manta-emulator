#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace manta {
enum class NorModel { W25Q64BV, MX25L6405D };
// Undocumented output is neither a manufacturer ID nor proven high impedance.
// Only explicit electrical experiments resolve it to an assumed level.
enum class SpiUnknownInput { Stop, Deferred, AssumedPullUp, AssumedPullDown };
struct SpiSample { uint8_t value = 0xa5, known = 0; };
class SpiNor {
  std::vector<uint8_t> packet_;
  unsigned outputIndex_ = 0;
  bool selected_ = false, wel_ = false, asleep_ = false;
public:
  NorModel model = NorModel::W25Q64BV;
  SpiUnknownInput unknownInput = SpiUnknownInput::Stop;
  std::function<uint8_t(uint32_t)> flashByte;
  std::string error;
  unsigned undocumentedSamples = 0;
  const char *name() const { return model == NorModel::W25Q64BV ? "w25q64bv" : "mx25l6405d"; }
  bool selected() const { return selected_; }
  const auto &packet() const { return packet_; }
  uint8_t manufacturer() const { return model == NorModel::W25Q64BV ? 0xef : 0xc2; }
  void select() { selected_ = true; packet_.clear(); outputIndex_ = 0; }
  void deselect() {
    // These one-byte commands take effect on CS rising, not on opcode arrival.
    if (selected_ && packet_.size() == 1) {
      if (packet_[0] == 6 && !asleep_) wel_ = true;
      if (packet_[0] == 4 && !asleep_) wel_ = false;
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
    SpiSample out;
    if (cmd == 5 && packet_.size() == 1) {
      // Persistent protection/SRP/QE bits cannot be inferred from array bytes.
      out = {uint8_t(0xa4 | (wel_ ? 2 : 0)), 3};
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
