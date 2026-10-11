#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>
namespace manta {
class Flash {
  std::vector<uint8_t> stock_;
  std::map<uint32_t, std::array<uint8_t, 4096>> sectors_;
  bool wel_ = false;
  bool protected_ = false;
  void bounds(uint32_t address, size_t size) const;
  std::array<uint8_t, 4096> &sector(uint32_t index);

public:
  explicit Flash(std::vector<uint8_t> stock);
  static Flash loadOriginal(const std::filesystem::path &path);
  uint8_t read(uint32_t address) const;
  const std::vector<uint8_t> &stock() const { return stock_; }
  void writeEnable() { wel_ = true; }
  void protect(bool b) { protected_ = b; }
  void program(uint32_t address, std::span<const uint8_t> bytes);
  void erase4k(uint32_t address);
  void saveOverlay(const std::filesystem::path &path) const;
  void loadOverlay(const std::filesystem::path &path);
};
} // namespace manta
