#pragma once
// Deterministic binary archive used by the full-machine save state.
//
// One visitor function per stateful class serves BOTH directions: Ar::saving() appends the value to the buffer, loading overwrites the
// value from the buffer. This keeps the two paths identical by construction. Containers are written with explicit element counts;
// unordered containers are written in sorted key order so the byte stream is reproducible. std::function members, pointers, file
// handles and references are never part of an archive: they are rebuilt by the machine constructor.
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
namespace manta {
namespace snap {
struct SnapshotError : std::runtime_error { using std::runtime_error::runtime_error; };
// Thrown when part of the machine state cannot be serialized faithfully (for example a queued GE operation holding a closure).
struct SnapshotUnsupported : SnapshotError { using SnapshotError::SnapshotError; };

class Ar {
  std::vector<uint8_t>* out_ = nullptr;
  const uint8_t* in_ = nullptr;
  size_t size_ = 0, pos_ = 0;

public:
  explicit Ar(std::vector<uint8_t>& out) : out_(&out) {}
  Ar(const uint8_t* data, size_t size) : in_(data), size_(size) {}
  bool saving() const { return out_ != nullptr; }
  bool loading() const { return out_ == nullptr; }
  size_t position() const { return saving() ? out_->size() : pos_; }
  size_t remaining() const { return saving() ? 0 : size_ - pos_; }
  void raw(void* p, size_t n) {
    if (saving()) {
      auto* b = static_cast<const uint8_t*>(p);
      out_->insert(out_->end(), b, b + n);
    } else {
      if (n > size_ - pos_) throw SnapshotError("snapshot section truncated");
      if (n) std::memcpy(p, in_ + pos_, n);
      pos_ += n;
    }
  }
  // Structure marker: a mismatch while loading means the writer and reader disagree about the layout.
  void tag(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* c = name; *c; ++c) h = (h ^ uint8_t(*c)) * 16777619u;
    uint32_t v = h;
    raw(&v, 4);
    if (loading() && v != h) throw SnapshotError(std::string("snapshot structure mismatch at ") + name);
  }
  uint64_t count(uint64_t n) {   // element count with a sanity bound while loading
    raw(&n, 8);
    if (loading() && n > (uint64_t(1) << 32)) throw SnapshotError("snapshot element count implausible");
    return n;
  }
  template <class T, class... R> Ar& operator()(T& v, R&... rest);
};

template <class T> struct is_std_array : std::false_type {};
template <class T, size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
template <class T> struct is_vector : std::false_type {};
template <class T> struct is_vector<std::vector<T>> : std::true_type {};
template <class T> struct is_deque : std::false_type {};
template <class T> struct is_deque<std::deque<T>> : std::true_type {};
template <class T> struct is_set : std::false_type {};
template <class T> struct is_set<std::set<T>> : std::true_type {};
template <class T> struct is_map : std::false_type {};
template <class K, class V> struct is_map<std::map<K, V>> : std::true_type {};
template <class T> struct is_umap : std::false_type {};
template <class K, class V> struct is_umap<std::unordered_map<K, V>> : std::true_type {};
template <class T> struct is_optional : std::false_type {};
template <class T> struct is_optional<std::optional<T>> : std::true_type {};
template <class T> struct is_pair : std::false_type {};
template <class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};
template <class T> struct is_bitset : std::false_type {};
template <size_t N> struct is_bitset<std::bitset<N>> : std::true_type {};

template <class T> void io(Ar& a, T& v);

// Class types are handled by overloads of ioClass(Ar&, T&) found through argument-dependent lookup (namespace manta).
template <class T> void io(Ar& a, T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    uint8_t b = v;
    a.raw(&b, 1);
    if (a.loading()) v = b != 0;
  } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
    a.raw(&v, sizeof(T));
  } else if constexpr (std::is_same_v<T, std::string>) {
    uint64_t n = a.count(v.size());
    if (a.loading()) {
      if (n > a.remaining()) throw SnapshotError("snapshot string truncated");
      v.assign(size_t(n), '\0');
    }
    a.raw(v.data(), size_t(n));
  } else if constexpr (is_std_array<T>::value) {
    using E = typename T::value_type;
    if constexpr (std::is_arithmetic_v<E> && !std::is_same_v<E, bool>) a.raw(v.data(), sizeof(E) * v.size());
    else for (auto& e : v) io(a, e);
  } else if constexpr (is_vector<T>::value) {
    using E = typename T::value_type;
    uint64_t n = a.count(v.size());
    if (a.loading()) {
      if (n > a.remaining() + 1) throw SnapshotError("snapshot vector longer than remaining data");
      v.clear();
      v.resize(size_t(n));
    }
    if constexpr (std::is_arithmetic_v<E> && !std::is_same_v<E, bool>) a.raw(v.data(), sizeof(E) * size_t(n));
    else for (auto& e : v) io(a, e);
  } else if constexpr (is_deque<T>::value) {
    uint64_t n = a.count(v.size());
    if (a.loading()) {
      if (n > a.remaining() + 1) throw SnapshotError("snapshot deque longer than remaining data");
      v.clear();
      v.resize(size_t(n));
    }
    for (auto& e : v) io(a, e);
  } else if constexpr (is_set<T>::value) {
    uint64_t n = a.count(v.size());
    if (a.saving()) {
      for (auto e : v) io(a, e);
    } else {
      v.clear();
      for (uint64_t i = 0; i < n; ++i) { typename T::value_type e{}; io(a, e); v.insert(std::move(e)); }
    }
  } else if constexpr (is_map<T>::value) {
    uint64_t n = a.count(v.size());
    if (a.saving()) {
      for (auto& [k, val] : v) { auto kk = k; io(a, kk); io(a, val); }
    } else {
      v.clear();
      for (uint64_t i = 0; i < n; ++i) { typename T::key_type k{}; typename T::mapped_type val{}; io(a, k); io(a, val); v.emplace(std::move(k), std::move(val)); }
    }
  } else if constexpr (is_umap<T>::value) {
    uint64_t n = a.count(v.size());
    if (a.saving()) {
      std::vector<typename T::key_type> keys;
      keys.reserve(v.size());
      for (auto& kv : v) keys.push_back(kv.first);
      std::sort(keys.begin(), keys.end());
      for (auto k : keys) { io(a, k); io(a, v.at(k)); }
    } else {
      v.clear();
      for (uint64_t i = 0; i < n; ++i) { typename T::key_type k{}; typename T::mapped_type val{}; io(a, k); io(a, val); v.emplace(std::move(k), std::move(val)); }
    }
  } else if constexpr (is_optional<T>::value) {
    bool has = v.has_value();
    io(a, has);
    if (a.loading()) {
      if (has) { typename T::value_type e{}; io(a, e); v = std::move(e); } else v.reset();
    } else if (has) io(a, *v);
  } else if constexpr (is_pair<T>::value) {
    io(a, v.first);
    io(a, v.second);
  } else if constexpr (is_bitset<T>::value) {
    std::vector<uint8_t> bytes((v.size() + 7) / 8);
    if (a.saving()) for (size_t i = 0; i < v.size(); ++i) if (v[i]) bytes[i / 8] |= uint8_t(1u << (i % 8));
    a.raw(bytes.data(), bytes.size());
    if (a.loading()) for (size_t i = 0; i < v.size(); ++i) v[i] = (bytes[i / 8] >> (i % 8)) & 1;
  } else {
    ioClass(a, v);
  }
}
template <class T, class... R> Ar& Ar::operator()(T& v, R&... rest) {
  io(*this, v);
  if constexpr (sizeof...(rest) > 0) (*this)(rest...);
  return *this;
}
} // namespace snap
using snap::Ar;
} // namespace manta
