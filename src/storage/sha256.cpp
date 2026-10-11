#include "storage/sha256.hpp"
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <vector>
namespace manta {
std::string sha256(std::span<const uint8_t> input) {
  constexpr std::array<uint32_t, 64> k = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  std::array<uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<uint8_t> b(input.begin(), input.end());
  uint64_t bits = uint64_t(b.size()) * 8;
  b.push_back(0x80);
  while (b.size() % 64 != 56)
    b.push_back(0);
  for (int i = 7; i >= 0; --i)
    b.push_back(uint8_t(bits >> (i * 8)));
  for (size_t off = 0; off < b.size(); off += 64) {
    std::array<uint32_t, 64> w{};
    for (size_t i = 0; i < 16; ++i)
      w[i] = uint32_t(b[off + i * 4]) << 24 | uint32_t(b[off + i * 4 + 1]) << 16 |
             uint32_t(b[off + i * 4 + 2]) << 8 | b[off + i * 4 + 3];
    for (size_t i = 16; i < 64; ++i) {
      auto x = w[i - 15], y = w[i - 2];
      w[i] = w[i - 16] + (std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3)) + w[i - 7] +
             (std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10));
    }
    auto [a, c, d, e, f, g, j, l] = h;
    for (size_t i = 0; i < 64; ++i) {
      uint32_t t1 = l + (std::rotr(f, 6) ^ std::rotr(f, 11) ^ std::rotr(f, 25)) +
                    ((f & g) ^ (~f & j)) + k[i] + w[i];
      uint32_t t2 =
          (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & c) ^ (a & d) ^ (c & d));
      l = j;
      j = g;
      g = f;
      f = e + t1;
      e = d;
      d = c;
      c = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += c;
    h[2] += d;
    h[3] += e;
    h[4] += f;
    h[5] += g;
    h[6] += j;
    h[7] += l;
  }
  std::ostringstream s;
  s << std::hex << std::setfill('0');
  for (auto v : h)
    s << std::setw(8) << v;
  return s.str();
}
} // namespace manta
