#pragma once
#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
namespace manta {
struct Options {
  bool stage2 = false, initPrefix = false, symbolicInit = false, candidateBdma = false,
       candidateUart = false, traceInstructions = false;
  uint64_t limit = 1000000;
  uint32_t breakpoint = 0, pauseMmio = 0;
  std::filesystem::path trace, replay, summary, coverage;
};
class Msd309Machine {
  Engine cpu_;
  Flash flash_;
  Riu riu_;
  Options options_;
  std::ofstream trace_;
  std::ifstream replay_;
  std::set<uint32_t> coverage_;
  std::deque<uint32_t> history_;
  uint64_t dispatches_ = 0, mmio_ = 0, flashReads_ = 0, dramWrites_ = 0;
  bool stopped_ = false;
  uint32_t blockingPc_ = 0, blockingAddress_ = 0;
  std::string reason_;
  bool stage2Reached_ = false, decompressReached_ = false, runtimeReached_ = false;
  std::string guestDecompressedSha_;
  bool preparingPrefix_ = false;
  void stop(std::string why, uint32_t address = 0);
  void event(const std::string &kind, uint32_t address, unsigned width, uint32_t value,
             const std::string &detail);
  static void codeHook(uc_engine *, uint64_t, uint32_t, void *);
  static void memoryHook(uc_engine *, uc_mem_type, uint64_t, int, int64_t, void *);
  static bool invalidHook(uc_engine *, uc_mem_type, uint64_t, int, int64_t, void *);
  static void interruptHook(uc_engine *, uint32_t, void *);
  static uint64_t mmioRead(uc_engine *, uint64_t, unsigned, void *);
  static void mmioWrite(uc_engine *, uint64_t, unsigned, uint64_t, void *);

public:
  Msd309Machine(Flash flash, Options options);
  int run();
  std::string summary() const;
  const std::string &uart() const { return riu_.uart; }
};
} // namespace manta
