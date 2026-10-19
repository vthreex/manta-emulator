#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
// Host -> emulator input hand-off. The Windows UI thread only ever appends a FUNCTION NAME ("MENU", "SOURCE", ...) here; the emulator thread
// (the only thread that touches guest state) drains it and turns each function into the emulated IR receiver event. Nothing here knows
// original key codes: the mapping lives in an explicit, evidence-labelled table (IrKeyMap).
class RemoteQueue {
public:
  void push(std::string function) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(function));
    pending_ = true;
  }
  bool pending() const { return pending_; }
  std::vector<std::string> drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out(events_.begin(), events_.end());
    events_.clear();
    pending_ = false;
    return out;
  }
private:
  std::mutex mutex_;
  std::deque<std::string> events_;
  std::atomic<bool> pending_{false};
};

// Function name -> raw IR command byte as seen in the PM IR FIFO (logical IR_KEY 3DA9). Only entries present in the table can be delivered.
struct IrKeyMap {
  std::map<std::string, uint8_t> raw;
  std::map<std::string, std::string> evidence;
  static IrKeyMap parse(const std::string& text) {
    IrKeyMap m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
      auto first = line.find_first_not_of(" \t\r");
      if (first == std::string::npos || line[first] == '#') continue;
      std::istringstream row(line);
      std::string name, value, rest;
      if (!(row >> name >> value)) throw std::runtime_error("IR key map: expected FUNCTION RAWBYTE [evidence]");
      std::getline(row, rest);
      size_t used = 0;
      unsigned long v = std::stoul(value, &used, 0);
      if (used != value.size() || v > 255) throw std::runtime_error("IR key map: raw byte must be 0..255");
      if (m.raw.count(name)) throw std::runtime_error("IR key map: duplicate function " + name);
      m.raw[name] = uint8_t(v);
      auto start = rest.find_first_not_of(" \t");
      if (start != std::string::npos) m.evidence[name] = rest.substr(start);
    }
    return m;
  }
};

// Scripted (replayable) delivery: "DISPATCH:FUNCTION" pairs separated by commas or semicolons; FUNCTION may also be "raw=0xNN".
struct ScriptedKey { uint64_t dispatch; std::string function; };
inline std::vector<ScriptedKey> parseIrScript(const std::string& text) {
  std::vector<ScriptedKey> out;
  std::string token;
  std::istringstream in(text);
  while (std::getline(in, token, ',')) {
    std::istringstream sub(token);
    std::string piece;
    while (std::getline(sub, piece, ';')) {
      if (piece.find_first_not_of(" \t\r\n") == std::string::npos) continue;
      auto colon = piece.find(':');
      if (colon == std::string::npos) throw std::runtime_error("IR script entry needs DISPATCH:FUNCTION");
      size_t used = 0;
      auto d = std::stoull(piece.substr(0, colon), &used, 0);
      if (used != colon) throw std::runtime_error("IR script dispatch is not a number");
      auto name = piece.substr(colon + 1);
      auto b = name.find_first_not_of(" \t\r\n"), e = name.find_last_not_of(" \t\r\n");
      out.push_back({d, name.substr(b, e - b + 1)});
    }
  }
  for (size_t i = 1; i < out.size(); ++i)
    if (out[i].dispatch < out[i - 1].dispatch) throw std::runtime_error("IR script entries must be in dispatch order");
  return out;
}
}
