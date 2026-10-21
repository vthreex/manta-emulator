#pragma once
// Non-owning text argument for event(): string literals are recognised (no copy, no allocation) and everything else is treated as a borrowed
// view that is valid only for the duration of the call.
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>
namespace manta {
struct EvText {
  const char* p;
  size_t n;
  bool literal;   // static storage duration: may be kept without copying
  template <size_t N> EvText(const char (&s)[N]) : p(s), n(N - 1), literal(true) {}
  template <size_t N> EvText(char (&s)[N]) : p(s), n(std::strlen(s)), literal(false) {}   // writable buffer: never a literal
  EvText(const char* s) : p(s), n(std::strlen(s)), literal(false) {}
  EvText(const std::string& s) : p(s.data()), n(s.size()), literal(false) {}
  EvText(std::string_view s) : p(s.data()), n(s.size()), literal(false) {}
  std::string_view view() const { return {p, n}; }
  bool operator==(std::string_view o) const { return view() == o; }
  bool operator!=(std::string_view o) const { return view() != o; }
  std::string str() const { return std::string(p, n); }
};
inline std::ostream& operator<<(std::ostream& os, const EvText& t) { return os.write(t.p, std::streamsize(t.n)); }
// Event text kept for later formatting: literals by pointer, anything else copied.
struct EvStore {
  const char* p = nullptr;
  uint32_t n = 0;
  std::string own;
  void set(const EvText& t) {
    if (t.literal) { p = t.p; n = uint32_t(t.n); }
    else { own.assign(t.p, t.n); p = nullptr; n = 0; }
  }
  std::string_view view() const { return p ? std::string_view(p, n) : std::string_view(own); }
};
}  // namespace manta
