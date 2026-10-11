#pragma once
#include "cpu/engine.hpp"
#include "peripherals/riu.hpp"
#include "storage/flash.hpp"
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_map>
namespace manta {
struct Options {
  bool stage2 = false, initPrefix = false, symbolicInit = false, candidateBdma = false,
       candidateUart = false, gpioI2c = false, candidateMiu = false, traceInstructions = false,
       i2cSdaStuck = false;
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
  std::unordered_map<uint32_t, uint64_t> pcHits_;
  struct Poll {
    uint32_t value = 0;
    uint64_t epoch = 0, count = 0;
  };
  std::unordered_map<uint64_t, Poll> polls_;
  uint64_t writeEpoch_ = 0, maxPoll_ = 0;
  std::deque<uint32_t> history_;
  uint64_t dispatches_ = 0, mmio_ = 0, flashReads_ = 0, dramWrites_ = 0;
  bool stopped_ = false;
  uint32_t blockingPc_ = 0, blockingAddress_ = 0;
  std::string reason_;
  bool stage2Reached_ = false, decompressReached_ = false, runtimeReached_ = false;
  std::string guestDecompressedSha_;
  bool preparingPrefix_ = false;
  uint32_t proveImmediateRmw(uint32_t pc, uint32_t address, unsigned width);
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
