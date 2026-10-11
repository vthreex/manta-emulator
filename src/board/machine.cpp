#include "board/machine.hpp"
#include "storage/sha256.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
namespace manta {
static std::string hex(uint64_t n) {
  std::ostringstream s;
  s << "0x" << std::hex << n;
  return s.str();
}
Msd309Machine::Msd309Machine(Flash f, Options o) : flash_(std::move(f)), options_(o) {
  if (o.cp0Timer)
    cpu_.tickCount(0); // reject unavailable engine extension at launch
  riu_.symbolicInit = o.symbolicInit;
  riu_.candidateBdma = o.candidateBdma;
  riu_.gpioI2c = o.gpioI2c;
  riu_.candidateMiu = o.candidateMiu;
  riu_.virtualRtc = o.virtualRtc;
  riu_.candidateUtmi = o.candidateUtmi;
  riu_.candidateSpi = o.candidateSpi;
  if (o.candidateMiu)
    riu_.resetMiuModel();
  if (o.i2cSdaStuck) {
    if (!o.gpioI2c)
      throw std::runtime_error("I2C fault fixture requires --gpio-i2c");
    riu_.holdI2cSda(true);
  }
  riu_.dramTest = [this](uint32_t a, size_t n, uint16_t pattern) {
    std::vector<uint8_t> expected(65536), actual(65536);
    for (size_t i = 0; i < expected.size(); ++i)
      expected[i] = uint8_t(pattern >> (8 * (i & 1)));
    for (size_t i = 0; i < n; i += expected.size()) {
      auto size = std::min(expected.size(), n - i);
      cpu_.write(a + uint32_t(i), expected.data(), size);
      cpu_.read(a + uint32_t(i), actual.data(), size);
      if (!std::equal(expected.begin(), expected.begin() + size, actual.begin()))
        return false;
    }
    return true;
  };
  if (o.candidateUart)
    riu_.enableCandidateUart();
  riu_.flashRead = [this](uint32_t a, size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i)
      b[i] = flash_.read(a + uint32_t(i));
    return b;
  };
  riu_.event = [this](auto &k, auto a, auto n, auto v, auto &d) { event(k, a, n, v, d); };
  if (!o.trace.empty()) {
    trace_.open(o.trace);
    if (!trace_)
      throw std::runtime_error("Cannot open trace");
  }
  if (!o.replay.empty()) {
    replay_.open(o.replay);
    if (!replay_)
      throw std::runtime_error("Cannot open replay");
  }
  cpu_.map(0, o.candidateMiu ? 0x10000000 : 0x08000000);
  cpu_.map(0x1fc00000, 0x400000, UC_PROT_READ | UC_PROT_EXEC);
  auto aperture = riu_.flashRead(0, 0x400000);
  cpu_.write(0x1fc00000, aperture.data(), aperture.size());
  check(uc_mmio_map(cpu_.handle, 0x1f000000, 0x400000, mmioRead, this, mmioWrite, this));
  if (o.stage2)
    cpu_.write(0x01000000, flash_.stock().data() + 0x2c60, 0x5960);
  uc_hook h;
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_CODE, reinterpret_cast<void *>(codeHook), this, 1, 0));
  // Unicorn 2.1.4 memory-write hooks leak MIPS delay state when a store is
  // the delay slot of JR and its target is another branch. See engine
  // regression. MMIO remains fully observed through mmio_map; count DRAM stores
  // at dispatch. Generic read hooks have the same problem for loads in delay
  // slots. Use instruction observations for coverage/access counts; retain
  // invalid hooks.
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_MEM_UNMAPPED | UC_HOOK_MEM_PROT,
                    reinterpret_cast<void *>(invalidHook), this, 1, 0));
  check(uc_hook_add(cpu_.handle, &h, UC_HOOK_INTR, reinterpret_cast<void *>(interruptHook), this, 1,
                    0));
}
void Msd309Machine::stop(std::string why, uint32_t a) {
  if (stopped_)
    return;
  stopped_ = true;
  blockingPc_ = cpu_.reg(UC_MIPS_REG_PC);
  blockingAddress_ = a;
  reason_ = std::move(why);
  uc_emu_stop(cpu_.handle);
}
void Msd309Machine::event(const std::string &k, uint32_t a, unsigned n, uint32_t v,
                          const std::string &d) {
  std::ostringstream s;
  s << "{\"tick\":" << dispatches_ << ",\"pc\":\"" << hex(cpu_.reg(UC_MIPS_REG_PC))
    << "\",\"kind\":\"" << k << "\",\"address\":\"" << hex(a) << "\",\"width\":" << n
    << ",\"value\":\"" << hex(v) << "\",\"ra\":\"" << hex(cpu_.reg(UC_MIPS_REG_RA))
    << "\",\"sp\":\"" << hex(cpu_.reg(UC_MIPS_REG_SP)) << "\",\"detail\":\"" << d << "\"}";
  auto line = s.str();
  if ((k == "read" || k == "write") && a >= 0x1f203e00 && a < 0x1f204000)
    ++gopEvents_;
  if ((k == "read" || k == "write") && a >= 0x1f205000 && a < 0x1f205200)
    ++geEvents_;
  if (k == "read" || k == "write" || k == "blocked_read" || k == "deferred_read") {
    recentMmio_.push_back(line);
    if (recentMmio_.size() > 256)
      recentMmio_.pop_front();
  }
  if (trace_)
    trace_ << line << '\n';
  if (replay_.is_open()) {
    std::string expected;
    if (!std::getline(replay_, expected) || expected != line)
      stop("deterministic replay mismatch", a);
  }
  if (k == "read" || k == "write" || k == "blocked_read")
    ++mmio_;
  if (k == "write")
    ++writeEpoch_;
  if (k == "read") {
    auto key = (uint64_t(cpu_.reg(UC_MIPS_REG_PC)) << 32) | a;
    auto &poll = polls_[key];
    if (poll.epoch != writeEpoch_ || poll.value != v)
      poll.count = 0;
    poll.epoch = writeEpoch_;
    poll.value = v;
    ++poll.count;
    maxPoll_ = std::max(maxPoll_, poll.count);
    if (poll.count >= 1024 && (poll.count & (poll.count - 1)) == 0)
      event("poll_warning", a, n, v,
            "unchanged peripheral reads without intervening guest MMIO write: " +
                std::to_string(poll.count));
  }
  if (options_.pauseMmio && a == options_.pauseMmio)
    stop("MMIO breakpoint", a);
}
void Msd309Machine::codeHook(uc_engine *, uint64_t a, uint32_t, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  ++m.dispatches_;
  if (m.options_.cp0Timer && (m.dispatches_ & 1) == 0)
    m.cpu_.tickCount(1);
  if (m.options_.cp0Timer && a == 0x803b0180) {
    ++m.timerEntries_;
    m.event("exception_vector", uint32_t(a), 0, 0,
            "original guest exception vector entered; QEMU performs architectural exception entry");
  }
  if (a == 0x80539ce4) {
    m.gopInitializerEntered_ = true;
    m.event("gop_initializer", uint32_t(a), 0, 0,
            "stock MDrv_GOP_Init entry identified by original diagnostic string and disassembly");
  }
  m.coverage_.insert(uint32_t(a));
  ++m.pcHits_[uint32_t(a)];
  m.history_.push_back(uint32_t(a));
  if (m.history_.size() > 64)
    m.history_.pop_front();
  if (a == 0x81000000)
    m.stage2Reached_ = true;
  if (a == 0x81000480 || a == 0x81004f08)
    m.decompressReached_ = true;
  if (a == 0x803b0600)
    m.runtimeReached_ = true;
  uint32_t w = 0;
  m.cpu_.read(uint32_t(a) & 0x1fffffff, &w, 4);
  // Runtime history is bounded; retain thousands of instructions without a
  // 90-million-line trace. RA/SP are call context observations, not an unwind.
  if (a >= 0x803b0180 && a < 0x806b8ce0) {
    m.recentInstructions_.push_back(
        {m.dispatches_, uint32_t(a), w, m.cpu_.reg(UC_MIPS_REG_RA), m.cpu_.reg(UC_MIPS_REG_SP)});
    if (m.recentInstructions_.size() > 4096)
      m.recentInstructions_.pop_front();
  }
  if (m.options_.deferDiagnosticReads) {
    auto why = m.unknownObservations_.observe(
        uint32_t(a), w, [&](unsigned i) { return m.cpu_.reg(UC_MIPS_REG_0 + int(i)); });
    if (!why.empty()) {
      m.event("unknown_observation_use", uint32_t(a), 4, w, why);
      m.stop(why);
      return;
    }
  }
  auto op = w >> 26;
  if ((op >= 32 && op <= 38) || (op >= 40 && op <= 43) || (op == 46)) {
    auto rs = (w >> 21) & 31;
    auto address =
        (m.cpu_.reg(UC_MIPS_REG_0 + int(rs)) + uint32_t(int32_t(int16_t(w & 65535)))) & 0x1fffffff;
    if (op < 40 && address >= 0x1fc00000 && address < 0x20000000)
      ++m.flashReads_;
    if (op >= 40 && address < 0x08000000)
      ++m.dramWrites_;
  }
  if (m.options_.traceInstructions)
    m.event("instruction", uint32_t(a), 4, w,
            "guest instruction dispatch; engine executes unchanged bytes");
  if (a == 0x81000000 || a == 0x81000480 || a == 0x81004f08 || a == 0x803b0600)
    m.event("checkpoint", uint32_t(a), 0, 0, "guest execution entered milestone address");
  if (a == 0x803b0600) {
    std::vector<uint8_t> b(3167072);
    m.cpu_.read(0x003b3980, b.data(), b.size());
    m.guestDecompressedSha_ = sha256(b);
    m.event("guest_inflate_hash", 0x003b3980, unsigned(b.size()), 0,
            "sha256=" + m.guestDecompressedSha_);
    if (m.guestDecompressedSha_ !=
        "149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236")
      m.stop("guest decompression hash mismatch");
  }
  if (m.preparingPrefix_ && a == 0xbfc01a74)
    m.stop("diagnostic prefix boundary before MIU ready poll");
  if (m.options_.breakpoint && a == m.options_.breakpoint)
    m.stop("PC breakpoint");
}
void Msd309Machine::memoryHook(uc_engine *, uc_mem_type type, uint64_t a, int, int64_t, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  if (type == UC_MEM_READ && a >= 0x1fc00000 && a < 0x20000000)
    ++m.flashReads_;
  if (type == UC_MEM_WRITE && a < 0x08000000)
    ++m.dramWrites_;
}
bool Msd309Machine::invalidHook(uc_engine *, uc_mem_type t, uint64_t a, int n, int64_t v, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  m.event("memory_fault", uint32_t(a), unsigned(n), uint32_t(v),
          "engine memory fault type " + std::to_string(t));
  m.stop("unmapped/protected guest access", uint32_t(a));
  return false;
}
void Msd309Machine::interruptHook(uc_engine *, uint32_t n, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  auto last = m.history_.empty() ? 0 : m.history_.back();
  uint32_t words[4]{};
  m.cpu_.read(last & 0x1fffffff, words, 16);
  m.event("cpu_exception", last, 0, n,
          "engine exception boundary; instruction=" + hex(words[0]) + " next=" + hex(words[1]) +
              " v0=" + hex(m.cpu_.reg(UC_MIPS_REG_2)) + " ra=" + hex(m.cpu_.reg(UC_MIPS_REG_RA)) +
              " status=" + hex(m.cpu_.reg(UC_MIPS_REG_CP0_STATUS)));
  m.stop("CPU exception " + std::to_string(n), last);
}
uint64_t Msd309Machine::mmioRead(uc_engine *, uint64_t off, unsigned n, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  try {
    auto a = 0x1f000000 + uint32_t(off);
    auto pc = m.cpu_.reg(UC_MIPS_REG_PC);
    // Stock boot switches between cached and uncached ROM aliases. The RMW
    // proof address is an instruction identity; traces retain the real PC.
    if ((pc & 0x1fffffff) >= 0x1fc00000 && (pc & 0x1fffffff) < 0x1fc02c60)
      pc = (pc & 0x1fffffff) | 0xa0000000;
    if (m.options_.symbolicInit) {
      auto defined = m.proveImmediateRmw(pc, a, n);
      if (defined)
        m.riu_.prepareRmw(a, defined);
    }
    if (m.options_.deferDiagnosticReads && m.riu_.knownBits(a, n) != 0xffff &&
        m.proveDiagnosticCache(pc, a, n)) {
      m.unknownObservations_.seed(3);
      ++m.deferredReads_;
      m.event("deferred_read", a, n, 0xa5a5,
              "INVALID unknown diagnostic observation; original guest may cache it, first "
              "consumption stops");
      return 0xa5a5;
    }
    auto mask = m.cpu_.reg(
        pc == 0x8055b578 || pc == 0x8055b584 || pc == 0x8053bc08 ? UC_MIPS_REG_7 : UC_MIPS_REG_3);
    if (pc == 0x8060a780 || pc == 0x8060a784) {
      auto bit = m.cpu_.reg(UC_MIPS_REG_7) & 31;
      auto low = m.cpu_.reg(UC_MIPS_REG_9) & 0x1fffffff;
      mask = (uint32_t(1) << bit) >> (a == low ? 0 : 16);
    }
    if (pc == 0x8053bbdc || pc == 0x8053bc84) {
      uint8_t b = 0;
      m.cpu_.read((m.cpu_.reg(UC_MIPS_REG_3) + 4) & 0x1fffffff, &b, 1);
      mask = b;
    }
    if (pc == 0x8053bcb0 || pc == 0x8053bd18 || pc == 0x8053bd80 || pc == 0x8053bde4) {
      uint8_t b = 0;
      auto base = m.cpu_.reg(pc == 0x8053bcb0   ? UC_MIPS_REG_8
                             : pc == 0x8053bd18 ? UC_MIPS_REG_5
                                                : UC_MIPS_REG_6);
      m.cpu_.read((base + (pc == 0x8053bde4 ? 0x14 : 4)) & 0x1fffffff, &b, 1);
      mask = b;
    }
    if (n == 1 && (a == 0x1f207578 || a == 0x1f207478)) {
      uint32_t instructions[2]{};
      m.cpu_.read(pc & 0x1fffffff, instructions, sizeof(instructions));
      // LBU v0 followed by ANDI v0,v0,2: no other calibration bits consumed.
      if ((instructions[0] & 0xfc1f0000) == 0x90020000 && instructions[1] == 0x30420002)
        mask = 2;
    }
    if (n == 2 && a >= 0x1f001000 && a < 0x1f001100) {
      uint32_t instructions[2]{};
      m.cpu_.read(pc & 0x1fffffff, instructions, sizeof(instructions));
      if ((instructions[0] & 0xfc1f0000) == 0x94020000 && instructions[1] == 0x30420001)
        mask = 1;
    }
    auto v = m.riu_.read(a, n, pc, mask, m.dispatches_);
    if (!m.riu_.stopReason.empty())
      m.stop(m.riu_.stopReason, a);
    return v;
  } catch (const std::exception &e) {
    m.stop(e.what(), 0x1f000000 + uint32_t(off));
    return 0xdeadbeef;
  }
}
bool Msd309Machine::proveDiagnosticCache(uint32_t pc, uint32_t a, unsigned n) {
  if (n != 2 || !((pc == 0x80469898 && a == 0x1f200a38) || (pc == 0x804698ac && a == 0x1f200a3c)))
    return false;
  // Byte identity of both complete paths, including the JAL delay-slot store.
  constexpr uint32_t words[] = {0x94430000, 0x36100a3c, 0x3c02806c, 0x00902021, 0xa443914a,
                                0x94830000, 0x3c02806c, 0x0c101182, 0xa443910e};
  uint32_t actual[9]{};
  cpu_.read(0x00469898, actual, sizeof(actual));
  return std::equal(std::begin(words), std::end(words), std::begin(actual)) &&
         cpu_.reg(UC_MIPS_REG_4) == (pc == 0x80469898 ? 0xbf000000 : 0xbf200a3c);
}
// Prove a straight-line immediate AND/OR RMW before allowing unknown reset
// bits through it. Reject branches, calls, escaping stores, loads and other
// uses of the sampled value. This is bit provenance, not a status response.
uint32_t Msd309Machine::proveImmediateRmw(uint32_t pc, uint32_t address, unsigned width) {
  if ((pc < 0x803b0180 || pc >= 0x806b8ce0) && (pc < 0xbfc00000 || pc >= 0xbfc02c60))
    return 0;
  uint32_t w;
  cpu_.read(pc & 0x1fffffff, &w, 4);
  auto op = w >> 26;
  if (op != 36 && op != 37 && op != 35)
    return 0;
  if ((op == 36 ? 1u : op == 37 ? 2u : 4u) != width)
    return 0;
  auto sampled = (w >> 16) & 31;
  uint32_t dependent[32]{};
  uint32_t values[32]{};
  for (unsigned r = 0; r < 32; ++r)
    values[r] = cpu_.reg(UC_MIPS_REG_0 + int(r));
  dependent[sampled] = width == 4 ? 0xffffffff : (uint32_t(1) << (width * 8)) - 1;
  bool changed = false;
  for (unsigned i = 1; i <= 12; ++i) {
    cpu_.read((pc + 4 * i) & 0x1fffffff, &w, 4);
    op = w >> 26;
    auto rs = (w >> 21) & 31, rt = (w >> 16) & 31;
    if (op == 12 || op == 13) {
      if (rt == 0)
        return 0;
      dependent[rt] = op == 12 ? dependent[rs] & (w & 65535) : dependent[rs] & ~(w & 65535);
      values[rt] = op == 12 ? values[rs] & (w & 65535) : values[rs] | (w & 65535);
      changed = true;
    } else if (op == 0 && w == 0)
      continue;
    else if (op == 15 && rt != 0) {
      dependent[rt] = 0;
      values[rt] = (w & 65535) << 16;
    } else if (op == 9 && !dependent[rs] && rt != 0) {
      dependent[rt] = 0;
      values[rt] = values[rs] + uint32_t(int32_t(int16_t(w)));
    } else if (op == 0 && ((w & 63) == 36 || (w & 63) == 37)) {
      auto rd = (w >> 11) & 31;
      if (rd == 0 || (dependent[rs] && dependent[rt]))
        return 0;
      auto known = dependent[rs] ? values[rt] : values[rs];
      auto dep = dependent[rs] | dependent[rt];
      dependent[rd] = (w & 63) == 36 ? dep & known : dep & ~known;
      values[rd] = (w & 63) == 36 ? values[rs] & values[rt] : values[rs] | values[rt];
      changed = true;
    } else if (op == 40 || op == 41 || op == 43) {
      auto target = (values[rs] + uint32_t(int32_t(int16_t(w)))) & 0x1fffffff;
      auto n = op == 40 ? 1u : op == 41 ? 2u : 4u;
      if (target != address || n != width || !changed || dependent[rs] || rs == sampled)
        return 0;
      auto full = width == 4 ? 0xffffffff : (uint32_t(1) << (width * 8)) - 1;
      return full & ~dependent[rt];
    } else
      return 0;
  }
  return 0;
}
void Msd309Machine::mmioWrite(uc_engine *, uint64_t off, unsigned n, uint64_t v, void *p) {
  auto &m = *static_cast<Msd309Machine *>(p);
  try {
    auto a = 0x1f000000 + uint32_t(off);
    auto pc = m.cpu_.reg(UC_MIPS_REG_PC);
    if ((pc & 0x1fffffff) >= 0x1fc00000 && (pc & 0x1fffffff) < 0x1fc02c60)
      pc = (pc & 0x1fffffff) | 0xa0000000;
    m.riu_.write(a, n, uint32_t(v), pc, m.cpu_.reg(UC_MIPS_REG_3), m.dispatches_);
    if (!m.riu_.stopReason.empty())
      m.stop(m.riu_.stopReason, a);
  } catch (const std::exception &e) {
    m.stop(e.what(), 0x1f000000 + uint32_t(off));
  }
}
int Msd309Machine::run() {
  if (options_.initPrefix) {
    if (!options_.stage2 || !options_.symbolicInit)
      throw std::runtime_error("init-prefix requires diagnostic-stage2 and symbolic-init");
    preparingPrefix_ = true;
    cpu_.run(0xbfc00000, 100000);
    preparingPrefix_ = false;
    if (reason_ != "diagnostic prefix boundary before MIU ready poll")
      throw std::runtime_error("Early prefix did not reach boundary: " + reason_);
    stopped_ = false;
    reason_.clear();
    blockingPc_ = blockingAddress_ = 0;
    event("checkpoint", 0x81000000, 0, 0,
          "DIAGNOSTIC host entry transfer after real early register writes; "
          "MIU completion and stage copy bypassed");
  }
  auto e = cpu_.run(options_.stage2 ? 0x81000000 : 0xbfc00000, options_.limit);
  if (!stopped_) {
    blockingPc_ = cpu_.reg(UC_MIPS_REG_PC);
    reason_ = e == UC_ERR_OK ? "instruction budget exhausted" : uc_strerror(e);
  }
  if (replay_.is_open()) {
    std::string line;
    if (std::getline(replay_, line))
      reason_ = "replay has remaining events";
  }
  if (!options_.coverage.empty()) {
    std::ofstream f(options_.coverage);
    for (auto pc : coverage_)
      f << hex(pc) << '\n';
  }
  auto s = summary();
  if (!options_.summary.empty()) {
    std::ofstream f(options_.summary);
    f << s << '\n';
  }
  std::cout << s << "\nUART: " << riu_.uart << '\n';
  return 2; // Research execution is incomplete even when a diagnostic
            // checkpoint is reached.
}
std::string Msd309Machine::summary() const {
  std::ostringstream s;
  s << "{\n  \"mode\": \""
    << (options_.stage2 ? "diagnostic second-stage; reset initialization bypassed"
                        : "firmware from reset flash")
    << "\",\n  \"symbolic_init\": " << (options_.symbolicInit ? "true" : "false")
    << ",\n  \"candidate_bdma\": " << (options_.candidateBdma ? "true" : "false")
    << ",\n  \"diagnostic_init_prefix\": " << (options_.initPrefix ? "true" : "false")
    << ",\n  \"candidate_uart\": " << (options_.candidateUart ? "true" : "false")
    << ",\n  \"gpio_i2c\": " << (options_.gpioI2c ? "true" : "false")
    << ",\n  \"candidate_miu\": " << (options_.candidateMiu ? "true" : "false")
    << ",\n  \"deferred_diagnostic_reads\": " << deferredReads_
    << ",\n  \"candidate_spi\": " << (options_.candidateSpi ? "true" : "false")
    << ",\n  \"candidate_utmi\": " << (options_.candidateUtmi ? "true" : "false")
    << ",\n  \"virtual_rtc\": " << (options_.virtualRtc ? "true" : "false")
    << ",\n  \"cp0_timer\": " << (options_.cp0Timer ? "true" : "false")
    << ",\n  \"guest_exception_vector_entries\": " << timerEntries_
    << ",\n  \"defer_diagnostic_reads_enabled\": "
    << (options_.deferDiagnosticReads ? "true" : "false")
    << ",\n  \"i2c_sda_stuck_fixture\": " << (options_.i2cSdaStuck ? "true" : "false")
    << ",\n  \"max_unchanged_mmio_poll\": " << maxPoll_ << ",\n  \"guest_decompressed_sha256\": \""
    << guestDecompressedSha_ << "\""
    << ",\n  \"instruction_dispatches\": " << dispatches_
    << ",\n  \"unique_pcs\": " << coverage_.size() << ",\n  \"mmio_events\": " << mmio_
    << ",\n  \"flash_load_dispatches\": " << flashReads_
    << ",\n  \"dram_store_dispatches\": " << dramWrites_ << ",\n  \"stop_pc\": \""
    << hex(blockingPc_) << "\",\n  \"stop_address\": \"" << hex(blockingAddress_)
    << "\",\n  \"reason\": \"" << reason_
    << "\",\n  \"second_stage_reached\": " << (stage2Reached_ ? "true" : "false")
    << ",\n  \"guest_inflate_entered\": " << (decompressReached_ ? "true" : "false")
    << ",\n  \"runtime_entry_reached\": " << (runtimeReached_ ? "true" : "false")
    << ",\n  \"osd_demonstrated\": false,\n  \"last_pcs\": [";
  bool first = true;
  for (auto pc : history_) {
    if (!first)
      s << ',';
    first = false;
    s << '"' << hex(pc) << '"';
  }
  s << "],\n  \"registers\": [";
  for (int i = 0; i < 32; ++i) {
    if (i)
      s << ',';
    s << '"' << hex(cpu_.reg(UC_MIPS_REG_0 + i)) << '"';
  }
  s << "],\n  \"stack_words\": [";
  auto sp = cpu_.reg(UC_MIPS_REG_SP) & 0x1fffffff;
  if (sp < 0x08000000 - 64) {
    uint32_t words[16]{};
    cpu_.read(sp, words, sizeof(words));
    for (int i = 0; i < 16; ++i) {
      if (i)
        s << ',';
      s << '"' << hex(words[i]) << '"';
    }
  }
  s << "],\n  \"hot_pcs\": [";
  std::vector<std::pair<uint32_t, uint64_t>> hot(pcHits_.begin(), pcHits_.end());
  std::sort(hot.begin(), hot.end(), [](auto &a, auto &b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  for (size_t i = 0; i < std::min(size_t(10), hot.size()); ++i) {
    if (i)
      s << ',';
    s << "{\"pc\":\"" << hex(hot[i].first) << "\",\"dispatches\":" << hot[i].second << '}';
  }
#ifdef UC_MIPS_MANTA_TIMER_API
  s << "],\n  \"cp0_count\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_COUNT))
    << "\",\n  \"cp0_compare\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_COMPARE))
    << "\",\n  \"cp0_cause\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_CAUSE)) << "\",\n  \"cp0_epc\":\""
    << hex(cpu_.reg(UC_MIPS_REG_CP0_EPC)) << "\"";
#else
  s << "]";
#endif
  uint32_t softwareTick = 0;
  cpu_.read(0x006e561c, &softwareTick, 4);
  s << ",\n  \"guest_software_tick\": " << softwareTick
    << ",\n  \"gop_initializer_entered\": " << (gopInitializerEntered_ ? "true" : "false")
    << ",\n  \"gop_aperture_events\": " << gopEvents_
    << ",\n  \"ge_candidate_aperture_events\": " << geEvents_;
  s << ",\n  \"cp0_status\":\"" << hex(cpu_.reg(UC_MIPS_REG_CP0_STATUS))
    << "\",\n  \"invalid_observation_bytes\": [";
  first = true;
  for (auto [address, origin] : unknownObservations_.bytes()) {
    if (!first)
      s << ',';
    first = false;
    s << "{\"address\":\"" << hex(address) << "\",\"origin\":\"" << hex(origin) << "\"}";
  }
  s << "],\n  \"recent_instructions\": [";
  first = true;
  for (auto &i : recentInstructions_) {
    if (!first)
      s << ',';
    first = false;
    s << "{\"tick\":" << i.tick << ",\"pc\":\"" << hex(i.pc) << "\",\"word\":\"" << hex(i.word)
      << "\",\"ra\":\"" << hex(i.ra) << "\",\"sp\":\"" << hex(i.sp) << "\"}";
  }
  s << "],\n  \"recent_mmio\": [";
  first = true;
  for (auto &line : recentMmio_) {
    if (!first)
      s << ',';
    first = false;
    s << line;
  }
  s << "]\n}";
  return s.str();
}
} // namespace manta
