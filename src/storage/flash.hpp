#pragma once
#include "snapshot/archive.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>
namespace manta {
class Flash {
  friend void ioClass(snap::Ar&, Flash&);
  std::vector<uint8_t> stock_;
  mutable std::string stockDigest_;   // cache of sha256(stock_)
  std::map<uint32_t, std::array<uint8_t, 4096>> sectors_;
  bool wel_ = false;
  bool protected_ = false;
  void bounds(uint32_t address, size_t size) const;
  std::array<uint8_t, 4096> &sector(uint32_t index);

public:
  explicit Flash(std::vector<uint8_t> stock);
  static Flash loadOriginal(const std::filesystem::path &path);
  // EXPLORATORY (--unverified-firmware): any image up to 8 MiB (+16 byte trailer); shorter images are padded with 0xFF (erased flash) at the end. The bytes are never modified.
  // Everything else in the board model was derived from the stock image, so another firmware may stop on hardware this model does not know (strict mode).
  static Flash loadUnverified(const std::filesystem::path &path);
  uint8_t read(uint32_t address) const;
  const std::vector<uint8_t> &stock() const { return stock_; }
  void writeEnable() { wel_ = true; }
  void protect(bool b) { protected_ = b; }
  void program(uint32_t address, std::span<const uint8_t> bytes);
  void erase4k(uint32_t address);
  // Guest SPI NOR effects. WEL/busy/page-wrap are enforced by the NOR model, not here. Programming
  // can only clear bits (result = old AND new, as on real NOR); the number of 0->1 requests is
  // returned so the caller can report them. The stock image is never modified.
  size_t programAnd(uint32_t address, std::span<const uint8_t> bytes);
  void eraseRange(uint32_t address, uint32_t size);
  size_t overlaySectors() const { return sectors_.size(); }
  std::string overlayDigest() const;   // SHA-256 over (sector index, 4096 bytes) of every modified sector
  std::string stockDigest() const;
  void saveOverlay(const std::filesystem::path &path) const;
  void loadOverlay(const std::filesystem::path &path);
};
} // namespace manta
