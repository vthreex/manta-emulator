#include "storage/flash.hpp"
#include "storage/sha256.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace manta {
static std::vector<uint8_t> readFile(const std::filesystem::path &p) {
  if (std::filesystem::file_size(p) > 0x900000)
    throw std::runtime_error("Input exceeds bounded flash/overlay size");
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot open input");
  return {std::istreambuf_iterator<char>(f), {}};
}
Flash::Flash(std::vector<uint8_t> b) : stock_(std::move(b)) {
  if (stock_.size() != 0x800000)
    throw std::runtime_error("Flash must be exactly 8 MiB");
}
Flash Flash::loadOriginal(const std::filesystem::path &p) {
  auto b = readFile(p);
  auto hash = sha256(b);
  if (b.size() != 0x800010 ||
      hash != "d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9")
    throw std::runtime_error("Firmware SHA256/size mismatch; this board model is sample-specific");
  b.resize(0x800000);
  return Flash(std::move(b));
}
Flash Flash::loadUnverified(const std::filesystem::path &p) {
  auto b = readFile(p);
  if (b.size() == 0x800010) b.resize(0x800000);
  if (b.size() > 0x800000 || b.size() < 0x1000)
    throw std::runtime_error("Unverified firmware must be between 4 KiB and 8 MiB (or 8 MiB + 16 bytes)");
  b.resize(0x800000, 0xff);
  return Flash(std::move(b));
}
void Flash::bounds(uint32_t a, size_t n) const {
  if (a > stock_.size() || n > stock_.size() - a)
    throw std::out_of_range("Flash bounds");
}
uint8_t Flash::read(uint32_t a) const {
  bounds(a, 1);
  auto i = sectors_.find(a / 4096);
  return i == sectors_.end() ? stock_[a] : i->second[a % 4096];
}
std::array<uint8_t, 4096> &Flash::sector(uint32_t n) {
  auto [it, new_] = sectors_.try_emplace(n);
  if (new_)
    std::copy_n(stock_.begin() + size_t(n) * 4096, 4096, it->second.begin());
  return it->second;
}
void Flash::program(uint32_t a, std::span<const uint8_t> b) {
  bounds(a, b.size());
  if (!wel_ || protected_)
    throw std::runtime_error("Flash write disabled/protected");
  if (b.empty() || b.size() > 256 || (a / 256) != (uint64_t(a) + b.size() - 1) / 256)
    throw std::runtime_error("Program must fit one 256-byte page");
  for (size_t i = 0; i < b.size(); ++i)
    if ((read(a + uint32_t(i)) & b[i]) != b[i])
      throw std::runtime_error("NOR programming cannot change 0 to 1");
  for (size_t i = 0; i < b.size(); ++i)
    sector((a + uint32_t(i)) / 4096)[(a + i) % 4096] &= b[i];
  wel_ = false;
}
void Flash::erase4k(uint32_t a) {
  bounds(a, 4096);
  if (a % 4096 || !wel_ || protected_)
    throw std::runtime_error("Invalid/disabled erase");
  sector(a / 4096).fill(255);
  wel_ = false;
}
size_t Flash::programAnd(uint32_t a, std::span<const uint8_t> b) {
  bounds(a, b.size());
  size_t violations = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    auto old = read(a + uint32_t(i));
    violations += (old & b[i]) != b[i];
    auto value = uint8_t(old & b[i]);
    if (value != old) sector((a + uint32_t(i)) / 4096)[(a + i) % 4096] = value;
  }
  return violations;
}
void Flash::eraseRange(uint32_t a, uint32_t size) {
  if ((size != 4096 && size != 32768 && size != 65536) || a % size) throw std::runtime_error("Invalid erase range");
  bounds(a, size);
  for (uint32_t s = a / 4096; s < (a + size) / 4096; ++s) sector(s).fill(255);
}
std::string Flash::overlayDigest() const {
  std::vector<uint8_t> b;
  for (auto &[n, s] : sectors_) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(n >> (8 * i)));
    b.insert(b.end(), s.begin(), s.end());
  }
  return sha256(b);
}
// The stock image is only set by the constructor and never modified, so its digest is computed once (it is asked for on every composed preview frame).
std::string Flash::stockDigest() const { if (stockDigest_.empty()) stockDigest_ = sha256(stock_); return stockDigest_; }
static void u32(std::vector<uint8_t> &b, uint32_t x) {
  for (int i = 0; i < 4; ++i)
    b.push_back(uint8_t(x >> (8 * i)));
}
static uint32_t get32(std::span<const uint8_t> b, size_t i) {
  if (i + 4 > b.size())
    throw std::runtime_error("Truncated overlay");
  return uint32_t(b[i]) | uint32_t(b[i + 1]) << 8 | uint32_t(b[i + 2]) << 16 |
         uint32_t(b[i + 3]) << 24;
}
void Flash::saveOverlay(const std::filesystem::path &p) const {
  std::vector<uint8_t> b = {'M', 'A', 'N', 'T', 'A', 'C', 'O', 'W'};
  auto hash = sha256(stock_);
  b.insert(b.end(), hash.begin(), hash.end());
  u32(b, uint32_t(sectors_.size()));
  for (auto &[n, s] : sectors_) {
    u32(b, n);
    b.insert(b.end(), s.begin(), s.end());
  }
  hash = sha256(b);
  b.insert(b.end(), hash.begin(), hash.end());
  auto tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    f.flush();
    if (!f)
      throw std::runtime_error("Overlay save failed");
  }
#ifdef _WIN32
  if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Atomic overlay replacement failed");
#else
  std::filesystem::rename(tmp, p);
#endif
}
void Flash::loadOverlay(const std::filesystem::path &p) {
  auto b = readFile(p);
  if (b.size() < 140 || b.size() > 0x900000)
    throw std::runtime_error("Invalid overlay size");
  if (std::string(b.begin(), b.begin() + 8) != "MANTACOW" ||
      std::string(b.begin() + 8, b.begin() + 72) != sha256(stock_))
    throw std::runtime_error("Overlay does not match stock image");
  auto body = std::span(b).first(b.size() - 64);
  if (sha256(body) != std::string(b.end() - 64, b.end()))
    throw std::runtime_error("Overlay integrity mismatch");
  uint32_t count = get32(b, 72);
  if (count > 2048 || b.size() != 140 + size_t(count) * 4100)
    throw std::runtime_error("Invalid overlay records");
  std::map<uint32_t, std::array<uint8_t, 4096>> pending;
  size_t off = 76;
  for (uint32_t i = 0; i < count; ++i) {
    auto n = get32(b, off);
    off += 4;
    if (n >= 2048 || pending.contains(n))
      throw std::runtime_error("Duplicate/out-of-range overlay sector");
    std::copy_n(b.begin() + off, 4096, pending[n].begin());
    off += 4096;
  }
  sectors_ = std::move(pending);
  wel_ = false;
}
} // namespace manta
