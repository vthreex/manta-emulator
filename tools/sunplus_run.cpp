// Sunplus SPHE8600 (MIPS32r2 LE, eCos) bring-up runner for the Cultraview CV9202 firmware (Shivaki STV-24LED14 / Manta LED4202). EXPLORATORY.
//   sunplus_run KERNEL.bin [--hyp FILE] [--readback] [--unknown-zero] [--map-iop] [--max-blocks N] [--poll-limit N] [--cpi N] [--ram-mb N]
//
// KERNEL.bin is the decompressed kernel blob (flash LZMA stream). It is placed at physical 0x400 (derived: the application image then starts at
// 0x400 + kernel size and every call inside the image lands on a function entry) and entered at 0x80000400 with Status=0 and $s6=0xBFFE8000, as the
// stage-1 stub would leave them. That hand-off is a LABELLED shortcut; the boot ROM and stage 1 are not run.
//
// Strict by default. The register window 0x1FFE0000..0x1FFFFFFF is unknown hardware. The only modelled device is what the firmware's own code
// polls: UART TX (data 0xBFFE0900, status 0xBFFE0904 = 0x41). Any other register READ stops the run with a report unless:
//   --hyp FILE       lines "ADDR VALUE [MASK] # why"; every use is counted and listed in the report (labelled hypothesis)
//   --readback       (hypothesis) registers read back the last value the firmware wrote
//   --unknown-zero   (EXPLORATORY) every other register read returns 0; each distinct address is listed in the report
// The register window uses uc_mmio_map callbacks (memory hooks mis-handle stores in branch delay slots in this Unicorn).
#include "cpu/engine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace manta;

static constexpr uint32_t REG_BASE = 0x1FFE0000, REG_SIZE = 0x20000, UART_DATA = REG_BASE + 0x900, UART_STAT = REG_BASE + 0x904;

struct Hyp { uint32_t value, mask; std::string note; };
struct Stop { std::string why; };

struct M {
  Engine eng;
  std::vector<uint8_t> ram;
  std::string uart;
  std::map<uint32_t, uint32_t> written;                       // register offset -> last word written
  std::map<std::pair<uint32_t, uint32_t>, std::pair<uint32_t, uint64_t>> writes;   // (addr,value) -> (block, count)
  std::vector<uint32_t> writeOrder;
  std::map<uint32_t, Hyp> hyp; std::map<uint32_t, Hyp> orHyp; std::map<uint32_t, Hyp> clrHyp; std::map<uint32_t, uint64_t> hypUsed; uint64_t readbackUsed = 0;
  std::map<uint32_t, std::pair<uint32_t, uint64_t>> zeroReads; std::vector<uint32_t> zeroOrder;
  bool readback = false, unknownZero = false; uint64_t pollLimit = 300000, maxBlocks = 0, blocks = 0; uint32_t cpi = 1;
  std::deque<uint32_t> ring; std::string stop; std::map<uint32_t, int> watch; std::vector<std::string> watchLog;
  std::vector<uint8_t> flash; uint32_t spiId9f = 0x001740EF, spiId90 = 0x000017EF; uint32_t pendReg = 0, pendBit = 0; int pendLeft = 0; uint64_t irqInjected = 0; uint64_t idleWakes = 0; uint64_t appBlocks = 0; uint32_t appFirst = 0, appLast = 0; uint32_t appBase = 0x809e2400, appEnd = 0x83000000; uint64_t spiReads = 0, spiIds = 0, spiOther = 0; std::map<uint32_t, uint64_t> spiOps;
  uint32_t lastBlock() const { return ring.empty() ? 0 : ring.back(); }
};

static uint32_t regValue(M& m, uint32_t off, unsigned size) {
  const uint32_t base = off & ~3u, addr = REG_BASE + base;
  uint32_t val;
  if (m.pendLeft > 0 && base == m.pendReg) { val = m.pendBit; m.pendLeft = 0; m.eng.setIrqLines(0); }   // EXPLORATORY injected interrupt source (pending register returns the vector's bit for a few reads)
  else if (!m.flash.empty() && base >= 0xB00 && base < 0xB40) { val = m.written.count(base) ? m.written[base] : 0; if (base == 0xB08) val &= ~0x80u; }   // SPI flash controller: command done instantly (busy bit clear)
  else if (addr == UART_STAT) val = 0x41;                        // bit0 TX ready + bit6 TX idle (modelled from the firmware's own polls)
  else if (auto h = m.hyp.find(addr); h != m.hyp.end()) { val = h->second.value & h->second.mask; ++m.hypUsed[addr]; }
  else if (m.readback && m.written.count(base)) { val = m.written[base]; ++m.readbackUsed; }
  else if (m.unknownZero) {
    auto it = m.zeroReads.find(base);
    if (it == m.zeroReads.end()) { it = m.zeroReads.emplace(base, std::make_pair(m.lastBlock(), uint64_t(0))).first; m.zeroOrder.push_back(base); }
    val = 0;
    if (++it->second.second >= m.pollLimit) {
      char b[200]; std::snprintf(b, sizeof b, "stuck polling %08x (%llu reads of the unknown-zero value) first near block %08x now block %08x", addr | 0xA0000000u, (unsigned long long)it->second.second, it->second.first, m.lastBlock());
      m.stop = b; throw Stop{b};
    }
  } else {
    char b[160]; std::snprintf(b, sizeof b, "unmodelled register READ %08x (size %u) near block %08x", addr | 0xA0000000u, size, m.lastBlock());
    m.stop = b; throw Stop{b};
  }
  if (auto c = m.clrHyp.find(addr); c != m.clrHyp.end()) { val &= ~c->second.value; ++m.hypUsed[addr | 2]; }   // labelled 'clear' hypothesis: self-clearing bits read as 0
  if (auto o = m.orHyp.find(addr); o != m.orHyp.end()) { val |= o->second.value; ++m.hypUsed[addr | 1]; }   // labelled 'or' hypothesis: these bits read as 1
  return (val >> ((off & 3) * 8)) & (size >= 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1));
}

static uint64_t mmioRead(uc_engine*, uint64_t off, unsigned size, void* user) {
  M& m = *static_cast<M*>(user);
  try { return regValue(m, uint32_t(off), size); }
  catch (const Stop&) { uc_emu_stop(m.eng.handle); return 0; }
}
static void mmioWrite(uc_engine*, uint64_t off, unsigned size, uint64_t value, void* user) {
  M& m = *static_cast<M*>(user);
  const uint32_t addr = REG_BASE + uint32_t(off);
  if (addr == UART_DATA) { m.uart.push_back(char(value & 0xff)); return; }
  const uint32_t mask = size >= 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1), v = uint32_t(value) & mask, base = uint32_t(off) & ~3u;
  if (size == 4) m.written[base] = v;
  else { const unsigned sh = (off & 3) * 8; m.written[base] = (m.written[base] & ~(mask << sh)) | (v << sh); }
  if (!m.flash.empty() && uint32_t(off) == 0xB08) {                // SPI command: [opcode:8][flags:8]; address in 0B0C (low 16) / 0B10 (bits 16..23); data back in 0B14 (low 16) / 0B18 (high 16)
    const uint32_t op = (v >> 8) & 0xff; ++m.spiOps[op];
    auto setData = [&](uint32_t w) { m.written[0xB14] = w & 0xffff; m.written[0xB18] = w >> 16; };
    if (op == 0x9f) { setData(m.spiId9f); ++m.spiIds; }
    else if (op == 0x90) { setData(m.spiId90); ++m.spiIds; }
    else if (op == 0x03) {
      const uint32_t a = ((m.written[0xB10] & 0xff) << 16) | (m.written[0xB0C] & 0xffff);
      uint32_t w = 0xFFFFFFFFu; if (a + 4 <= m.flash.size()) std::memcpy(&w, m.flash.data() + a, 4);
      setData(w); ++m.spiReads;
    } else if (op == 0x05) setData(0);                               // read status register: not busy, not write-protected
    else ++m.spiOther;                                               // write enable / program / erase etc. are accepted and ignored (read-only flash)
  }
  auto key = std::make_pair(addr, v);
  auto it = m.writes.find(key);
  if (it == m.writes.end()) { m.writes[key] = {m.lastBlock(), 1}; m.writeOrder.push_back(addr); m.writeOrder.push_back(v); } else ++it->second.second;
}
static void onBlock(uc_engine*, uint64_t addr, uint32_t size, void* user) {
  M& m = *static_cast<M*>(user);
  m.ring.push_back(uint32_t(addr)); if (m.ring.size() > 64) m.ring.pop_front();
  ++m.blocks;
  if (addr >= m.appBase && addr < m.appEnd) { if (!m.appBlocks++) m.appFirst = uint32_t(addr); m.appLast = uint32_t(addr); }
  if (!m.watch.empty()) { auto w = m.watch.find(uint32_t(addr)); if (w != m.watch.end() && w->second > 0) { --w->second; char t[320]; std::snprintf(t, sizeof t, "watch %08x blocks=%llu a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x v1=%08x s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x ra=%08x sp=%08x", uint32_t(addr), (unsigned long long)m.blocks, m.eng.reg(UC_MIPS_REG_A0), m.eng.reg(UC_MIPS_REG_A1), m.eng.reg(UC_MIPS_REG_A2), m.eng.reg(UC_MIPS_REG_A3), m.eng.reg(UC_MIPS_REG_V0), m.eng.reg(UC_MIPS_REG_V1), m.eng.reg(UC_MIPS_REG_S0), m.eng.reg(UC_MIPS_REG_S1), m.eng.reg(UC_MIPS_REG_S2), m.eng.reg(UC_MIPS_REG_S3), m.eng.reg(UC_MIPS_REG_S4), m.eng.reg(UC_MIPS_REG_RA), m.eng.reg(UC_MIPS_REG_SP)); m.watchLog.push_back(t); } }
  if (m.cpi) m.eng.tickCount((size / 4) * m.cpi);          // CP0 Count advances with executed instructions; the patched engine raises the timer interrupt
  if (m.maxBlocks && m.blocks >= m.maxBlocks) { m.stop = "block budget"; uc_emu_stop(m.eng.handle); }
  if (addr < 0x400 && m.blocks > 10) { char b[64]; std::snprintf(b, sizeof b, "execution at low address %08x", uint32_t(addr)); m.stop = b; uc_emu_stop(m.eng.handle); }
}
static void onIntr(uc_engine*, uint32_t intno, void* user) {
  M& m = *static_cast<M*>(user);
  char b[160]; std::snprintf(b, sizeof b, "CPU exception %u at pc %08x status %08x", intno, m.eng.reg(UC_MIPS_REG_PC), m.eng.reg(UC_MIPS_REG_CP0_STATUS));
  m.stop = b; uc_emu_stop(m.eng.handle);
}

static std::vector<uint8_t> load(const char* p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { std::fprintf(stderr, "cannot read %s\n", p); std::exit(2); }
  return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: sunplus_run KERNEL.bin [--hyp FILE] [--readback] [--unknown-zero] [--map-iop] [--max-blocks N] [--poll-limit N] [--cpi N] [--ram-mb N]\n"); return 2; }
  for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--cpu-fmt") Engine::cpuModel = UC_CPU_MIPS32_M14K;   // EXPLORATORY: fixed-mapping MMU core (user addresses -> physical +0x40000000 when ERL=0)
  std::vector<std::pair<uint32_t, uint32_t>> iopSets;   // EXPLORATORY: words preset in the IOP data window (offset=value)
  int irqVec = -1; uint32_t irqMask = 0;   // EXPLORATORY: external interrupt lines IP2..IP6 (bit0 = IP2) asserted at every idle wake-up
  M m; uint64_t maxIdle = 2000000; bool fmt = false; uint32_t xip = 0; std::string uartOut; uint32_t ramMb = 128; bool mapIop = false; uint32_t loadAt = 0x400; size_t nwrites = 60;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--readback") m.readback = true;
    else if (a == "--unknown-zero") m.unknownZero = true;
    else if (a == "--map-iop") mapIop = true;
    else if (a == "--uart-out") uartOut = argv[++i];
    else if (a == "--flash") m.flash = load(argv[++i]);
    else if (a == "--cpu-fmt") fmt = true;
    else if (a == "--irq-vec") irqVec = int(std::strtol(argv[++i], nullptr, 0));
    else if (a == "--irq-mask") irqMask = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--app-base") m.appBase = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--app-end") m.appEnd = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--iop-set") { std::string s = argv[++i]; auto eq = s.find('='); iopSets.push_back({uint32_t(std::strtoul(s.c_str(), nullptr, 0)), uint32_t(std::strtoul(s.c_str() + eq + 1, nullptr, 0))}); }
    else if (a == "--max-idle") maxIdle = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--flash-xip") xip = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--spi-id9f") m.spiId9f = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--spi-id90") m.spiId90 = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--max-blocks") m.maxBlocks = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--poll-limit") m.pollLimit = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--cpi") m.cpi = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--ram-mb") ramMb = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--watch") m.watch[uint32_t(std::strtoul(argv[++i], nullptr, 0))] = 400;
    else if (a == "--writes") nwrites = std::strtoul(argv[++i], nullptr, 0);
    else if (a == "--hyp") {
      std::ifstream f(argv[++i]); std::string line;
      while (std::getline(f, line)) {
        std::string note; auto h = line.find('#'); if (h != std::string::npos) { note = line.substr(h + 1); line = line.substr(0, h); }
        std::istringstream ss(line); std::string A, V, K; if (!(ss >> A >> V)) continue; ss >> K;
        if (V == "clear") { m.clrHyp[uint32_t(std::strtoul(A.c_str(), nullptr, 0)) & 0x1FFFFFFF] = Hyp{uint32_t(std::strtoul(K.c_str(), nullptr, 0)), 0, note}; continue; }
        if (V == "or") { m.orHyp[uint32_t(std::strtoul(A.c_str(), nullptr, 0)) & 0x1FFFFFFF] = Hyp{uint32_t(std::strtoul(K.c_str(), nullptr, 0)), 0, note}; continue; }
        Hyp y{uint32_t(std::strtoul(V.c_str(), nullptr, 0)), K.empty() ? 0xFFFFFFFFu : uint32_t(std::strtoul(K.c_str(), nullptr, 0)), note};
        m.hyp[uint32_t(std::strtoul(A.c_str(), nullptr, 0)) & 0x1FFFFFFF] = y;
      }
    }
  }
  const auto kernel = load(argv[1]);
  const size_t ramBytes = size_t(ramMb) << 20;
  m.ram.assign(ramBytes, 0);
  std::memcpy(m.ram.data() + loadAt, kernel.data(), std::min(kernel.size(), ramBytes - loadAt));
  m.eng.mapShared(0, ramBytes, m.ram.data());
  m.eng.setBacking(m.ram.data(), ramBytes);
  if (xip && !m.flash.empty()) { size_t sz = (m.flash.size() + 0xfff) & ~size_t(0xfff); m.flash.resize(sz, 0xFF); m.eng.mapShared(xip, sz, m.flash.data(), UC_PROT_READ | UC_PROT_EXEC); }   // EXPLORATORY: SPI flash mapped for execute-in-place
  if (mapIop) m.eng.map(0x1FFC0000, 0x20000);                // EXPLORATORY: IOP data window read by 'Get IOP Data' (0x200 bytes, contents unknown -> zeros)
  if (mapIop) for (auto& [off, val] : iopSets) m.eng.write(0x1FFC0000 + off, &val, 4);
  check(uc_mmio_map(m.eng.handle, REG_BASE, REG_SIZE, mmioRead, &m, mmioWrite, &m));
  uc_hook h1, h2;
  check(uc_hook_add(m.eng.handle, &h1, UC_HOOK_BLOCK, reinterpret_cast<void*>(onBlock), &m, 1, 0));
  check(uc_hook_add(m.eng.handle, &h2, UC_HOOK_INTR, reinterpret_cast<void*>(onIntr), &m, 1, 0));
  m.eng.reg(UC_MIPS_REG_CP0_STATUS, 0);                       // EXPLORATORY: boot ROM leaves ERL/BEV clear
  m.eng.reg(UC_MIPS_REG_S6, 0xBFFE8000);                      // stage-1 stub: lui s6,0xbffe ; ori s6,s6,0x8000
  uc_err e = m.eng.run(0x80000400, 0);
  // `wait` (the eCos idle thread) halts the engine. EXPLORATORY idle model: advance CP0 Count to Compare so the timer interrupt fires, then resume.
  uint64_t idles = 0; const uint64_t maxIdles = maxIdle;
  while (e == UC_ERR_OK && m.stop.empty() && idles < maxIdles) {
    const uint32_t cmp = m.eng.reg(UC_MIPS_REG_CP0_COMPARE), cnt = m.eng.reg(UC_MIPS_REG_CP0_COUNT);
    uint32_t dist = cmp - cnt; if (!dist) dist = 1;
    m.eng.tickCount(dist); ++idles;
    if (irqMask) m.eng.setIrqLines(irqMask);
    if (irqVec >= 0) {   // vector -> (CPU line, pending register, bit) from the kernel's dispatcher and unmask tables
      int line; uint32_t reg, bit;
      if (irqVec >= 0x40 && irqVec <= 0x47) { line = 0; reg = 0x4cc; bit = irqVec - 0x40; }
      else if (irqVec >= 0x48 && irqVec <= 0x5b) { line = 1; reg = 0x4d0; bit = irqVec - 0x40; }
      else if (irqVec >= 0x5c && irqVec <= 0x5f) { line = 2; reg = 0x4d4; bit = irqVec - 0x40; }
      else if (irqVec >= 0x21 && irqVec <= 0x30) { line = 3; reg = 0x4d8; bit = irqVec - 0x20; }
      else { line = 4; reg = 0x4dc; bit = irqVec - 0x20; }
      m.pendReg = reg; m.pendBit = 1u << bit; m.pendLeft = 4; ++m.irqInjected;
      m.eng.setIrqLines(1u << line);
    }
    e = m.eng.run(m.eng.reg(UC_MIPS_REG_PC), 0);
  }
  m.idleWakes = idles;
  if (irqVec >= 0) m.eng.setIrqLines(0);
  if (m.stop.empty()) m.stop = e == UC_ERR_OK ? "ended" : std::string("unicorn: ") + uc_strerror(e);
  std::printf("stop: %s\npc=%08x blocks=%llu\n", m.stop.c_str(), m.eng.reg(UC_MIPS_REG_PC), (unsigned long long)m.blocks);
  if (!uartOut.empty()) { std::ofstream f(uartOut, std::ios::binary); f << m.uart; }
  std::string u; for (char c : m.uart) { if (c == '\r') u += "\\r"; else if (c == '\n') u += "\\n"; else u += c; }
  std::printf("UART: %s\n", u.size() > 900 ? ("..." + u.substr(u.size() - 900)).c_str() : u.c_str());
  std::printf("regs: ra=%08x sp=%08x v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x s0=%08x gp=%08x count=%08x compare=%08x status=%08x\n", m.eng.reg(UC_MIPS_REG_RA), m.eng.reg(UC_MIPS_REG_SP), m.eng.reg(UC_MIPS_REG_V0), m.eng.reg(UC_MIPS_REG_V1), m.eng.reg(UC_MIPS_REG_A0), m.eng.reg(UC_MIPS_REG_A1), m.eng.reg(UC_MIPS_REG_A2), m.eng.reg(UC_MIPS_REG_S0), m.eng.reg(UC_MIPS_REG_GP), m.eng.reg(UC_MIPS_REG_CP0_COUNT), m.eng.reg(UC_MIPS_REG_CP0_COMPARE), m.eng.reg(UC_MIPS_REG_CP0_STATUS));
  std::printf("last blocks:"); for (size_t i = m.ring.size() > 16 ? m.ring.size() - 16 : 0; i < m.ring.size(); ++i) std::printf(" %08x", m.ring[i]); std::printf("\n");
  std::printf("idle wakes (Count advanced to Compare after `wait`): %llu\n", (unsigned long long)m.idleWakes);
  std::printf("application-range blocks executed (>= %08x): %llu, first %08x last %08x\n", m.appBase, (unsigned long long)m.appBlocks, m.appFirst, m.appLast);
  if (!m.flash.empty()) {
    std::printf("SPI flash (modelled device, %zu bytes): word reads %llu, ID reads %llu, other commands %llu; opcodes:", m.flash.size(), (unsigned long long)m.spiReads, (unsigned long long)m.spiIds, (unsigned long long)m.spiOther);
    for (auto& [op, n] : m.spiOps) std::printf(" %02x:%llu", op, (unsigned long long)n);
    std::printf("\n");
  }
  for (auto& s : m.watchLog) std::printf("%s\n", s.c_str());
  std::printf("hypotheses used:\n");
  for (auto& [a, y] : m.hyp) std::printf("  %08x = %08x  used %llu  %s\n", a | 0xA0000000u, y.value, (unsigned long long)m.hypUsed[a], y.note.c_str());
  for (auto& [a, y] : m.orHyp) std::printf("  %08x |= %08x (bits read as 1 on top of the register)  used %llu  %s\n", a | 0xA0000000u, y.value, (unsigned long long)m.hypUsed[a | 1], y.note.c_str());
  for (auto& [a, y] : m.clrHyp) std::printf("  %08x &= ~%08x (self-clearing bits read as 0)  used %llu  %s\n", a | 0xA0000000u, y.value, (unsigned long long)m.hypUsed[a | 2], y.note.c_str());
  if (m.readbackUsed) std::printf("  readback of firmware-written registers: %llu reads\n", (unsigned long long)m.readbackUsed);
  if (!m.zeroOrder.empty()) {
    std::printf("EXPLORATORY unknown-zero reads (%zu distinct registers):\n", m.zeroOrder.size());
    for (uint32_t b : m.zeroOrder) std::printf("  %08x read %llu times, first near block %08x\n", (REG_BASE + b) | 0xA0000000u, (unsigned long long)m.zeroReads[b].second, m.zeroReads[b].first);
  }
  std::printf("distinct register writes: %zu (first %zu):\n", m.writes.size(), nwrites);
  for (size_t i = 0, n = 0; i + 1 < m.writeOrder.size() && n < nwrites; i += 2, ++n) {
    auto& w = m.writes[{m.writeOrder[i], m.writeOrder[i + 1]}];
    std::printf("  near %08x: %08x <= %08x%s\n", w.first, m.writeOrder[i] | 0xA0000000u, m.writeOrder[i + 1], w.second > 1 ? ("  x" + std::to_string(w.second)).c_str() : "");
  }
  return 0;
}
