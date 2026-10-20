#pragma once
// Opt-in statistical profiler (Windows): samples the instruction pointer of the emulation thread from a helper thread and writes a histogram.
// It never alters guest state; the sampled thread is suspended for microseconds per sample.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
namespace manta {
class Sampler {
  std::atomic<bool> run_{false};
  std::thread worker_;
  void* target_ = nullptr;
public:
  void start(unsigned intervalMicroseconds);
  void stopAndWrite(const std::filesystem::path& path);
  ~Sampler();
};
}
