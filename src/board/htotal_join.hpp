#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace manta {
// DIAGNOSTIC ONLY, never part of strict or normal connected execution.
//
// The original HTotal comparison at 80501BFC branches on the unknown sample
// (a3) against known history (a1). tools/mips_join_proof.py shows that every
// path rejoins at 80501758, differs there only in v0/v1/a0 (dead at the join)
// and in two RAM cells (stability counter 806D83D4, accepted total 806AD960),
// and performs no MMIO, call or exception-capable operation.
//
// This class executes the ORIGINAL instruction words fetched from guest memory
// along each outcome, separately, in a private register/memory copy, and merges
// the end states conservatively: any register or RAM cell whose value is not
// identical across outcomes is reported as unknown. It does not choose the
// branch, does not touch the guest, and aborts (throws) if the guest words are
// not the verified ones or an unsupported instruction is met.
struct HtotalBranchJoin {
  static constexpr uint32_t branch = 0x80501bfc, join = 0x80501758;
  struct Word { uint32_t pc, word; };
  // Verified original words (tools/mips_join_proof.py, firmware SHA-256 d60645b5...).
  static constexpr std::array<Word, 15> expected{{
      {0x80501bfc, 0x14e5fed4}, {0x80501c00, 0x3c02806e}, {0x80501c04, 0x3c04806e},
      {0x80501c08, 0x908383d4}, {0x80501c0c, 0x2c620004}, {0x80501c10, 0x14400101},
      {0x80501c14, 0x24620001}, {0x80501c18, 0x3c13806b}, {0x80501c1c, 0x081405d6},
      {0x80501c20, 0xa667d960}, {0x80501750, 0xa04083d4}, {0x80501754, 0x3c13806b},
      {0x80502018, 0xa08283d4}, {0x8050201c, 0x081405d6}, {0x80502020, 0x3c13806b}}};
  // Registers that may legitimately differ at the join (dead there per the liveness proof).
  static constexpr std::array<unsigned, 3> deadAtJoin{2, 3, 4}; // v0, v1, a0
  using Regs = std::array<uint32_t, 32>;
  using Fetch = std::function<uint32_t(uint32_t pc)>;
  using Load8 = std::function<uint8_t(uint32_t address)>;
  struct Store { uint32_t address; unsigned width; uint32_t value; };
  struct Outcome { std::string name; Regs regs{}; std::vector<Store> stores; };
  struct Merge {
    std::vector<Outcome> outcomes;
    Regs common{};                       // register values identical in all outcomes
    std::vector<unsigned> differingRegisters;
    std::map<uint32_t, bool> unknownBytes; // byte address -> true when not identical across outcomes
  };
  // Guest words must be exactly the verified ones.
  static void verify(const Fetch& fetch) {
    for (auto [pc, word] : expected)
      if (fetch(pc) != word)
        throw std::runtime_error("HTotal join: guest instruction differs from the verified original");
  }
  // Execute one outcome. `matchOutcome` forces the private a3 copy equal to (true)
  // or different from (false) a1; the real guest register and its unknown tag are untouched.
  static Outcome execute(const std::string& name, bool matchOutcome, Regs regs, const Fetch& fetch,
                         const Load8& load8) {
    regs[7] = matchOutcome ? regs[5] : regs[5] ^ 1u;
    std::map<uint32_t, uint8_t> overlay;
    Outcome out;
    out.name = name;
    auto read8 = [&](uint32_t a) { auto it = overlay.find(a); return it == overlay.end() ? load8(a) : it->second; };
    auto step = [&](uint32_t pc) {
      uint32_t w = fetch(pc), op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, imm = w & 0xffff;
      int32_t sim = int16_t(imm);
      auto put = [&](unsigned r, uint32_t v) { if (r) regs[r] = v; };
      switch (op) {
      case 15: put(rt, imm << 16); break;                                   // LUI
      case 9: put(rt, regs[rs] + uint32_t(sim)); break;                     // ADDIU
      case 11: put(rt, regs[rs] < uint32_t(sim) ? 1u : 0u); break;          // SLTIU
      case 36: put(rt, read8((regs[rs] + uint32_t(sim)) & 0x1fffffff)); break; // LBU
      case 40:                                                              // SB
      case 41: {                                                            // SH
        unsigned width = op == 40 ? 1 : 2;
        uint32_t a = (regs[rs] + uint32_t(sim)) & 0x1fffffff, v = regs[rt];
        if (a >= 0x1f000000 || (width == 2 && (a & 1)))
          throw std::runtime_error("HTotal join: store outside aligned guest RAM");
        for (unsigned i = 0; i < width; ++i) overlay[a + i] = uint8_t(v >> (8 * i));
        out.stores.push_back({a, width, width == 1 ? (v & 0xff) : (v & 0xffff)});
        break;
      }
      case 0:
        if (w != 0) throw std::runtime_error("HTotal join: unsupported SPECIAL instruction");
        break;
      default: throw std::runtime_error("HTotal join: unsupported instruction in verified path");
      }
    };
    uint32_t pc = branch;
    for (unsigned guard = 0; guard < 32 && pc != join; ++guard) {
      uint32_t w = fetch(pc), op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
      if (op == 4 || op == 5) {                       // BEQ / BNE, delay slot always executes
        bool equal = regs[rs] == regs[rt];
        bool taken = op == 4 ? equal : !equal;
        uint32_t target = pc + 4 + uint32_t(int32_t(int16_t(w & 0xffff)) * 4);
        step(pc + 4);
        pc = taken ? target : pc + 8;
      } else if (op == 2) {                           // J
        uint32_t target = ((pc + 4) & 0xf0000000u) | ((w & 0x3ffffff) << 2);
        step(pc + 4);
        pc = target;
      } else { step(pc); pc += 4; }
    }
    if (pc != join) throw std::runtime_error("HTotal join: outcome did not reach the join");
    out.regs = regs;
    return out;
  }
  static Merge run(const Regs& entry, const Fetch& fetch, const Load8& load8) {
    verify(fetch);
    Merge m;
    m.outcomes.push_back(execute("mismatch", false, entry, fetch, load8));
    m.outcomes.push_back(execute("match", true, entry, fetch, load8));
    for (unsigned r = 1; r < 32; ++r) {
      bool same = true;
      for (auto& o : m.outcomes) same &= o.regs[r] == m.outcomes[0].regs[r];
      if (r == 7) same = true;                       // private a3 probe value, restored by the caller
      m.common[r] = m.outcomes[0].regs[r];
      if (!same) {
        bool allowed = false;
        for (auto d : deadAtJoin) allowed |= d == r;
        if (!allowed) throw std::runtime_error("HTotal join: live register differs between outcomes");
        m.differingRegisters.push_back(r);
      }
    }
    // A byte is determinate only if every outcome leaves the same final value there
    // (an outcome that does not write it leaves its entry value).
    std::map<uint32_t, bool> touched;
    for (auto& o : m.outcomes)
      for (auto& s : o.stores)
        for (unsigned i = 0; i < s.width; ++i) touched[s.address + i] = true;
    for (auto& entryKey : touched) {
      uint32_t address = entryKey.first;
      std::vector<int> finals;
      for (auto& o : m.outcomes) {
        int v = load8(address);
        for (auto& s : o.stores)
          for (unsigned i = 0; i < s.width; ++i)
            if (s.address + i == address) v = int(uint8_t(s.value >> (8 * i)));
        finals.push_back(v);
      }
      bool identical = true;
      for (auto f : finals) identical &= f == finals[0];
      m.unknownBytes[address] = !identical;
    }
    return m;
  }
};
}
