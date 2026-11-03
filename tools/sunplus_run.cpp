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
#include "sunplus_gpu.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace manta;

static constexpr uint32_t REG_BASE = 0x1FFE0000, REG_SIZE = 0x20000, UART_DATA = REG_BASE + 0x900, UART_STAT = REG_BASE + 0x904;

struct Hyp { uint32_t value, mask; std::string note; };
struct Stop { std::string why; };
struct HostCall { uint32_t fn, a0, a1; uint64_t at; bool done = false; };   // EXPLORATORY host-initiated call of a firmware function from the idle thread

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
  std::vector<uint8_t> flash; uint32_t spiId9f = 0x001740EF, spiId90 = 0x000017EF; int lastVec = -1; uint32_t pendReg = 0, pendBit = 0; int pendLeft = 0; uint64_t lastKickBlocks = 0, lastKeyBlocks = 0; std::deque<uint32_t> pendingKeys; std::deque<char> rxq; std::vector<std::pair<uint64_t, std::string>> uartIn; std::vector<uint8_t> bestScreen; uint64_t bestNz = 0, bestKick = 0; std::map<uint32_t, std::pair<uint32_t, uint64_t>> thr; std::map<uint32_t, uint32_t> thrPrev; SunplusGpu gpu; bool gpuOn = false; uint32_t gpuLastWp = 0; uint64_t gpuKicks = 0; int uartPcLog = 0; bool irqClearReq = false; uint64_t irqInjected = 0; std::string trigText; bool trigSeen = false; std::vector<uint32_t> snap; uint32_t spLo = 0, spHi = 0; std::deque<uint32_t> spRing; size_t hotN = 0; std::map<uint32_t, uint64_t> hot; uint32_t sfDefault = 4; uint64_t sfLast = 0; uint32_t sfFn = 0, sfReg = 0; uint32_t sfExpect[4] = {0,0,0,0}; uint64_t sfUses = 0; uint32_t consecAddr = 0xFFFFFFFF; uint64_t consecCount = 0; std::map<uint32_t, uint64_t> rbReads; uint64_t idleWakes = 0; uint64_t appBlocks = 0; uint32_t appFirst = 0, appLast = 0; uint32_t appBase = 0x809e2400, appEnd = 0x83000000; uint64_t spiReads = 0, spiIds = 0, spiOther = 0; std::map<uint32_t, uint64_t> spiOps;
  uint32_t lastBlock() const { return ring.empty() ? 0 : ring.back(); }
};

static uint32_t regValue(M& m, uint32_t off, unsigned size) {
  const uint32_t base = off & ~3u, addr = REG_BASE + base;
  uint32_t val;
  const uint64_t consec = (base == m.consecAddr) ? ++m.consecCount : (m.consecAddr = base, m.consecCount = 1);   // consecutive reads of the same register (no other register access in between)
  if (m.sfFn && addr == m.sfReg) { if (m.blocks - m.sfLast < 3000) val = m.sfExpect[0] | (m.sfExpect[1] << 4) | (m.sfExpect[2] << 8) | (m.sfExpect[3] << 12); else val = m.sfDefault * 0x1111u; }
  else if (m.pendLeft > 0 && base == m.pendReg) { val = m.pendBit; m.pendLeft = 0; m.irqClearReq = true; }   // EXPLORATORY injected interrupt source (pending register returns the vector's bit for a few reads)
  else if (!m.flash.empty() && base >= 0xB00 && base < 0xB40) { val = m.written.count(base) ? m.written[base] : 0; if (base == 0xB08) val &= ~0x80u; }   // SPI flash controller: command done instantly (busy bit clear)
  else if (addr == UART_DATA && !m.rxq.empty()) { val = uint8_t(m.rxq.front()); m.rxq.pop_front(); }   // EXPLORATORY UART receive (serial monitor input, --uart-in)
  else if (addr == UART_STAT) val = 0x41 | (m.rxq.empty() ? 0u : 2u);                        // bit0 TX ready + bit6 TX idle (modelled from the firmware's own polls)
  else if (auto h = m.hyp.find(addr); h != m.hyp.end()) { val = h->second.value & h->second.mask; ++m.hypUsed[addr]; }
  else if (m.readback && m.written.count(base)) {
    val = m.written[base]; ++m.readbackUsed;
    uint64_t& rc = m.rbReads[base]; ++rc; if (consec >= m.pollLimit) {   // the firmware keeps re-reading a register it wrote itself and nothing changes it: report like an unknown-register poll
      char t[200]; std::snprintf(t, sizeof t, "stuck polling %08x (%llu reads of the unknown-zero value) first near block %08x now block %08x", addr | 0xA0000000u, (unsigned long long)rc, m.lastBlock(), m.lastBlock());
      m.stop = t; throw Stop{t};
    }
  }
  else if (m.unknownZero) {
    auto it = m.zeroReads.find(base);
    if (it == m.zeroReads.end()) { it = m.zeroReads.emplace(base, std::make_pair(m.lastBlock(), uint64_t(0))).first; m.zeroOrder.push_back(base); }
    val = 0;
    ++it->second.second; if (consec >= m.pollLimit) {
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
  try {
    const uint64_t v = regValue(m, uint32_t(off), size);
    static const char* rt = std::getenv("SUNPLUS_READ_TRACE");   // debugging aid: SUNPLUS_READ_TRACE=lo:hi logs every register read between those block counts
    if (rt) { static const uint64_t lo = std::strtoull(rt, nullptr, 0), hi = std::strchr(rt, ':') ? std::strtoull(std::strchr(rt, ':') + 1, nullptr, 0) : 0; if (m.blocks >= lo && m.blocks <= hi) std::printf("rd %08x = %08x size %u block %08x blocks=%llu\n", REG_BASE + uint32_t(off), uint32_t(v), size, m.lastBlock(), (unsigned long long)m.blocks); }
    return v;
  }
  catch (const Stop&) { uc_emu_stop(m.eng.handle); return 0; }
}
static void mmioWrite(uc_engine*, uint64_t off, unsigned size, uint64_t value, void* user) {
  M& m = *static_cast<M*>(user);
  const uint32_t addr = REG_BASE + uint32_t(off);
  if (addr == UART_DATA) { m.uart.push_back(char(value & 0xff)); if (std::getenv("SUNPLUS_UART_PC") && m.uartPcLog < 12 && (value & 0xff) == uint8_t(std::getenv("SUNPLUS_UART_PC")[0])) { ++m.uartPcLog; std::printf("uart '%c' from block %08x ra=%08x sp=%08x", char(value & 0xff), m.lastBlock(), m.eng.reg(UC_MIPS_REG_RA), m.eng.reg(UC_MIPS_REG_SP)); std::printf(" ring:"); { size_t n = m.ring.size(); for (size_t k = n > 24 ? n - 24 : 0; k < n; ++k) std::printf(" %08x", m.ring[k]); } std::printf("\n"); } return; }   // SUNPLUS_UART_PC=c: log where character c is printed from
  const uint32_t mask = size >= 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1), v = uint32_t(value) & mask, base = uint32_t(off) & ~3u;
  m.rbReads[base] = 0; m.consecAddr = 0xFFFFFFFF;
  if (size == 4) m.written[base] = v;
  else { const unsigned sh = (off & 3) * 8; m.written[base] = (m.written[base] & ~(mask << sh)) | (v << sh); }
  if ((uint32_t(off) == 0xA1D4) && std::getenv("SUNPLUS_CLUT_TRACE")) std::printf("clut idx %02x : %08x %08x %08x (pc %08x)\n", m.written.count(0xA1C8) ? m.written[0xA1C8] & 0xff : 0xff, m.written.count(0xA1CC) ? m.written[0xA1CC] : 0, m.written.count(0xA1D0) ? m.written[0xA1D0] : 0, uint32_t(value), m.lastBlock());   // debugging aid: 256-entry table loaded via 0xBFFEA1C8 (index) / 1CC / 1D0 / 1D4
  if (uint32_t(off) == 0x3590 && (std::getenv("SUNPLUS_GPU_TRACE") || m.gpuOn)) {   // debugging aid: dump the SUNPLUS GPU command words (reg<<24|data; 0xA0000000|n = execute n) between the previous and the new FIFO write pointer
    const uint32_t base = m.written.count(0x358C) ? m.written[0x358C] << 10 : 0, wp = uint32_t(v) & 0xffff; ++m.gpuKicks;
    if (std::getenv("SUNPLUS_GPU_TRACE")) { if (std::getenv("SUNPLUS_KICK_TIME")) std::fprintf(stderr, "kicktime %llu blocks %llu\n", (unsigned long long)m.gpuKicks, (unsigned long long)m.blocks); std::printf("gpu kick %llu: fifo base %08x wp %04x (prev %04x):", (unsigned long long)m.gpuKicks, base, wp, m.gpuLastWp); }
    for (uint32_t a = m.gpuLastWp; a != wp && base; a = (a + 4) & 0xffff) { uint32_t w = 0; m.eng.read(base + (a & 0x7fff), &w, 4);   /* ring is 0x8000 bytes; bit 15 of the pointers is the lap flag */ if (std::getenv("SUNPLUS_GPU_TRACE")) std::printf(" %08x", w); if (m.gpuOn) m.gpu.push(w); if (((a - m.gpuLastWp) & 0xffff) > 0x1000) break; }
    std::printf("\n"); m.gpuLastWp = wp; m.lastKickBlocks = m.blocks;
    if (const char* bs = std::getenv("SUNPLUS_BEST_SCREEN")) {   // debugging aid: SUNPLUS_BEST_SCREEN=addr,w,h keeps the buffer state with the most non-zero pixels seen after any GPU kick (saved by SUNPLUS_BEST_FILE at exit)
      unsigned long ba = 0, bw = 0, bh = 0; if (std::sscanf(bs, "%lx,%lu,%lu", &ba, &bw, &bh) == 3 && ba + bw * bh <= m.ram.size()) {
        uint64_t nz = 0; for (size_t q = 0; q < bw * bh; ++q) nz += m.ram[ba + q] != 0;
        if (nz >= m.bestNz) { m.bestNz = nz; m.bestKick = m.gpuKicks; m.bestScreen.assign(m.ram.begin() + ba, m.ram.begin() + ba + bw * bh); } } }
    if (const char* sk = std::getenv("SUNPLUS_STOP_AT_KICK")) if (m.gpuKicks >= std::strtoull(sk, nullptr, 0)) { m.stop = "stop at gpu kick (debug)"; uc_emu_stop(m.eng.handle); }
  }
  if (!m.flash.empty() && uint32_t(off) == 0xB08) {                // SPI command: [opcode:8][flags:8]; address in 0B0C (low 16) / 0B10 (bits 16..23); data back in 0B14 (low 16) / 0B18 (high 16)
    const uint32_t op = (v >> 8) & 0xff; ++m.spiOps[op];
    auto setData = [&](uint32_t w) { m.written[0xB14] = w & 0xffff; m.written[0xB18] = w >> 16; };
    if (op == 0x9f) { setData(m.spiId9f); ++m.spiIds; }
    else if (op == 0x90) { setData(m.spiId90); ++m.spiIds; }
    else if (op == 0x03) {
      const uint32_t a = ((m.written[0xB10] & 0xff) << 16) | (m.written[0xB0C] & 0xffff);
      uint32_t w = 0xFFFFFFFFu; if (a + 4 <= m.flash.size()) std::memcpy(&w, m.flash.data() + a, 4);
      setData(w); ++m.spiReads;
      static const char* st = std::getenv("SUNPLUS_SPI_TRACE");   // debugging aid: SUNPLUS_SPI_TRACE=lo:hi logs flash reads between those block counts
      if (st) { static const uint64_t lo = std::strtoull(st, nullptr, 0), hi = std::strchr(st, ':') ? std::strtoull(std::strchr(st, ':') + 1, nullptr, 0) : 0; if (m.blocks >= lo && m.blocks <= hi) std::printf("spi read %06x = %08x block %08x blocks=%llu\n", a, w, m.lastBlock(), (unsigned long long)m.blocks); }
    } else if (op == 0x05) setData(0);                               // read status register: not busy, not write-protected
    else ++m.spiOther;                                               // write enable / program / erase etc. are accepted and ignored (read-only flash)
  }
  auto key = std::make_pair(addr, v);
  auto it = m.writes.find(key);
  if (it == m.writes.end()) { m.writes[key] = {m.lastBlock(), 1}; m.writeOrder.push_back(addr); m.writeOrder.push_back(v); } else ++it->second.second;
}
static uint64_t watchFrom() { static const uint64_t v = std::getenv("SUNPLUS_WATCH_FROM") ? std::strtoull(std::getenv("SUNPLUS_WATCH_FROM"), nullptr, 0) : 0; return v; }   // debugging aid: ignore watch hits before this block count
static void onBlock(uc_engine*, uint64_t addr, uint32_t size, void* user) {
  M& m = *static_cast<M*>(user);
  m.ring.push_back(uint32_t(addr)); if (m.ring.size() > 600) m.ring.pop_front();
  ++m.blocks;
  if (!m.uartIn.empty() && m.blocks >= m.uartIn.front().first) { for (char c : m.uartIn.front().second) m.rxq.push_back(c); m.uartIn.erase(m.uartIn.begin()); }
#ifdef _WIN32
  { static const uint64_t hcStart = std::getenv("SUNPLUS_HEAPCHECK") ? std::strtoull(std::getenv("SUNPLUS_HEAPCHECK"), nullptr, 0) : 0; static bool hcDone = false;   // debugging aid: find the first block after which the host heap is corrupt
    if (hcStart && !hcDone && m.blocks >= hcStart && !HeapValidate(GetProcessHeap(), 0, nullptr)) { hcDone = true; std::printf("HEAP CORRUPT detected at block %llu (block %08x)\n", (unsigned long long)m.blocks, uint32_t(addr)); std::fflush(stdout); } }
#endif
  if (m.irqClearReq) { m.irqClearReq = false; m.eng.setIrqLines(0); }   // deassert outside the MMIO callback (writing CP0 state mid-instruction crashed DSP cores)
  if (m.spHi) { uint32_t sp = m.eng.reg(UC_MIPS_REG_SP); if (sp >= m.spLo && sp < m.spHi) { m.spRing.push_back(uint32_t(addr)); if (m.spRing.size() > 400) m.spRing.pop_front(); } }
  if (m.hotN) ++m.hot[uint32_t(addr)];
  { static const bool tr = std::getenv("SUNPLUS_THREADS") != nullptr; if (tr) { const uint32_t sp = m.eng.reg(UC_MIPS_REG_SP); if (sp >= 0x80000000 && sp < 0x88000000 && addr >= 0x80000000) { auto& e = m.thr[sp >> 14]; e.first = uint32_t(addr); e.second = m.blocks; } } }   // debugging aid: last block executed per 16KB stack bucket (= per thread)
  if (m.sfFn && uint32_t(addr) == m.sfFn) { m.sfExpect[m.eng.reg(UC_MIPS_REG_A0) & 3] = m.eng.reg(UC_MIPS_REG_A1) & 0xf; ++m.sfUses; m.sfLast = m.blocks; }   // EXPLORATORY state-follow: the wait function's expected state is what the status register reports
  if (addr >= m.appBase && addr < m.appEnd) { if (!m.appBlocks++) m.appFirst = uint32_t(addr); m.appLast = uint32_t(addr); }
  if (!m.watch.empty()) { auto w = m.watch.find(uint32_t(addr)); if (w != m.watch.end() && w->second > 0 && m.blocks >= watchFrom()) { --w->second; char t[320]; std::snprintf(t, sizeof t, "watch %08x blocks=%llu a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x v1=%08x s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x ra=%08x sp=%08x gp=%08x", uint32_t(addr), (unsigned long long)m.blocks, m.eng.reg(UC_MIPS_REG_A0), m.eng.reg(UC_MIPS_REG_A1), m.eng.reg(UC_MIPS_REG_A2), m.eng.reg(UC_MIPS_REG_A3), m.eng.reg(UC_MIPS_REG_V0), m.eng.reg(UC_MIPS_REG_V1), m.eng.reg(UC_MIPS_REG_S0), m.eng.reg(UC_MIPS_REG_S1), m.eng.reg(UC_MIPS_REG_S2), m.eng.reg(UC_MIPS_REG_S3), m.eng.reg(UC_MIPS_REG_S4), m.eng.reg(UC_MIPS_REG_RA), m.eng.reg(UC_MIPS_REG_SP), m.eng.reg(UC_MIPS_REG_GP)); std::string line = t; if (std::getenv("SUNPLUS_WATCH_RING")) { line += " ring:"; size_t n = m.ring.size(); for (size_t k = n > 16 ? n - 16 : 0; k + 1 < n; ++k) { char b[12]; std::snprintf(b, sizeof b, " %08x", m.ring[k]); line += b; } } m.watchLog.push_back(line); } }
  if (m.cpi) m.eng.tickCount((size / 4) * m.cpi);          // CP0 Count advances with executed instructions; the patched engine raises the timer interrupt
  { static const uint32_t stopPc = std::getenv("SUNPLUS_STOP_AT_PC") ? uint32_t(std::strtoul(std::getenv("SUNPLUS_STOP_AT_PC"), nullptr, 0)) : 0; if (stopPc && uint32_t(addr) == stopPc && m.blocks >= watchFrom()) { m.stop = "stop at pc (debug)"; uc_emu_stop(m.eng.handle); } }   // debugging aid: with SUNPLUS_WATCH_FROM
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

static M* gCrashM = nullptr;
#ifdef _WIN32
static LONG WINAPI crashFilter(EXCEPTION_POINTERS*) {   // host crash inside the engine: show the last guest blocks that were executed
  if (gCrashM) { std::printf("HOST CRASH after %llu blocks; last guest blocks:", (unsigned long long)gCrashM->blocks); size_t n = gCrashM->ring.size(); for (size_t k = n > 40 ? n - 40 : 0; k < n; ++k) std::printf(" %08x", gCrashM->ring[k]); std::printf("\n"); std::fflush(stdout); }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif
int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: sunplus_run KERNEL.bin [--hyp FILE] [--readback] [--unknown-zero] [--map-iop] [--max-blocks N] [--poll-limit N] [--cpi N] [--ram-mb N]\n"); return 2; }
  for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--cpu-fmt") Engine::cpuModel = UC_CPU_MIPS32_M14K;
  for (int i = 2; i + 1 < argc; ++i) if (std::string(argv[i]) == "--cpu") {   // EXPLORATORY: the firmware executes MIPS DSP ASE instructions (dpaq_sa.l.w, extr_rs.w)
    std::string n = argv[i + 1];
    if (n == "24kec") Engine::cpuModel = UC_CPU_MIPS32_24KEC; else if (n == "74kf") Engine::cpuModel = UC_CPU_MIPS32_74KF; else if (n == "34kf") Engine::cpuModel = UC_CPU_MIPS32_34KF; else if (n == "24kc") Engine::cpuModel = UC_CPU_MIPS32_24KC;
  }   // EXPLORATORY: fixed-mapping MMU core (user addresses -> physical +0x40000000 when ERL=0)
  std::vector<std::pair<uint32_t, uint32_t>> iopSets;   // EXPLORATORY: words preset in the IOP data window (offset=value)
  uint32_t coverLo = 0, coverHi = 0; std::vector<std::pair<uint32_t, uint32_t>> dumps; std::vector<uint32_t> peeks; std::vector<HostCall> calls; std::set<int> irqExcl; bool irqAuto = false; std::vector<int> irqVecs; int irqVec = -1; uint32_t irqMask = 0;   // EXPLORATORY: external interrupt lines IP2..IP6 (bit0 = IP2) asserted at every idle wake-up
  M m; gCrashM = &m;
#ifdef _WIN32
  SetUnhandledExceptionFilter(crashFilter);
#endif
  std::string cmdFile; uint32_t keyFn = 0x80cee834; uint64_t rtAfter = 0, cmdLinesDone = 0; uint64_t maxIdle = 2000000; bool fmt = false; uint32_t xip = 0; std::string uartOut; std::string saveRam; uint32_t ramMb = 128; bool mapIop = false; uint32_t loadAt = 0x400; size_t nwrites = 60;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--readback") m.readback = true;
    else if (a == "--unknown-zero") m.unknownZero = true;
    else if (a == "--map-iop") mapIop = true;
    else if (a == "--uart-out") uartOut = argv[++i];
    else if (a == "--flash") m.flash = load(argv[++i]);
    else if (a == "--cpu-fmt") fmt = true;
    else if (a == "--cpu") ++i;
    else if (a == "--irq-auto") irqAuto = true;
    else if (a == "--irq-exclude") { std::string s = argv[++i]; size_t p0 = 0; while (p0 <= s.size()) { size_t p1 = s.find(',', p0); if (p1 == std::string::npos) p1 = s.size(); irqExcl.insert(int(std::strtol(s.substr(p0, p1 - p0).c_str(), nullptr, 0))); p0 = p1 + 1; } }
    else if (a == "--sp-ring") { m.spLo = uint32_t(std::strtoul(argv[++i], nullptr, 0)); m.spHi = uint32_t(std::strtoul(argv[++i], nullptr, 0)); }
    else if (a == "--cover") { coverLo = uint32_t(std::strtoul(argv[++i], nullptr, 0)); coverHi = uint32_t(std::strtoul(argv[++i], nullptr, 0)); m.hotN = m.hotN ? m.hotN : 1; }
    else if (a == "--hot") m.hotN = std::strtoul(argv[++i], nullptr, 0);
    else if (a == "--state-default") m.sfDefault = uint32_t(std::strtoul(argv[++i], nullptr, 0)) & 0xf;
    else if (a == "--state-follow") { std::string s = argv[++i]; auto c = s.find(':'); m.sfFn = uint32_t(std::strtoul(s.c_str(), nullptr, 0)); m.sfReg = uint32_t(std::strtoul(s.c_str() + c + 1, nullptr, 0)) & 0x1FFFFFFF; }
    else if (a == "--dump") { uint32_t ad = uint32_t(std::strtoul(argv[++i], nullptr, 0)); uint32_t ln = uint32_t(std::strtoul(argv[++i], nullptr, 0)); dumps.push_back({ad, ln}); }
    else if (a == "--save-ram") saveRam = argv[++i];
    else if (a == "--uart-in") { std::string t = argv[++i]; size_t at = t.rfind('@'); uint64_t blk = at == std::string::npos ? 0 : std::strtoull(t.c_str() + at + 1, nullptr, 0); std::string txt = at == std::string::npos ? t : t.substr(0, at); for (char& c : txt) if (c == '|') c = '\r'; m.uartIn.push_back({blk, txt}); }   // EXPLORATORY: feed TEXT@BLOCKS to the UART receiver ('|' = Enter)
    else if (a == "--cmd-file") cmdFile = argv[++i];   // EXPLORATORY: poll FILE for lines "key HEX" (host call of the key function) and "shot PATH" (dump OSD buffer + palette)
    else if (a == "--key-fn") keyFn = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--realtime-after") rtAfter = std::strtoull(argv[++i], nullptr, 0);   // sleep ~10 ms per idle wake-up (one timer tick) from this wake-up on
    else if (a == "--gpu") m.gpuOn = true;   // EXPLORATORY: execute SUNPLUS GPU FIFO commands (tools/sunplus_gpu.hpp)
    else if (a == "--peek") peeks.push_back(uint32_t(std::strtoul(argv[++i], nullptr, 0)));
    else if (a == "--call") { std::string s = argv[++i]; HostCall c{}; auto p1 = s.find(':'), p2 = s.find(':', p1 + 1), at = s.find('@'); c.fn = uint32_t(std::strtoul(s.c_str(), nullptr, 0)); c.a0 = uint32_t(std::strtoul(s.c_str() + p1 + 1, nullptr, 0)); c.a1 = uint32_t(std::strtoul(s.c_str() + p2 + 1, nullptr, 0)); c.at = std::strtoull(s.c_str() + at + 1, nullptr, 0); calls.push_back(c); }
    else if (a == "--snap-after") m.trigText = argv[++i];
    else if (a == "--irq-vec") { std::string s = argv[++i]; size_t p0 = 0; while (p0 <= s.size()) { size_t p1 = s.find(',', p0); if (p1 == std::string::npos) p1 = s.size(); irqVecs.push_back(int(std::strtol(s.substr(p0, p1 - p0).c_str(), nullptr, 0))); p0 = p1 + 1; } irqVec = irqVecs[0]; }
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
  m.gpu.ram = m.ram.data(); m.gpu.ramSize = ramBytes;
  if (xip && !m.flash.empty()) { size_t sz = (m.flash.size() + 0xfff) & ~size_t(0xfff); m.flash.resize(sz, 0xFF); m.eng.mapShared(xip, sz, m.flash.data(), UC_PROT_READ | UC_PROT_EXEC); }   // EXPLORATORY: SPI flash mapped for execute-in-place
  if (mapIop) m.eng.map(0x1FFC0000, 0x20000);                // EXPLORATORY: IOP data window read by 'Get IOP Data' (0x200 bytes, contents unknown -> zeros)
  if (mapIop) for (auto& [off, val] : iopSets) m.eng.write(0x1FFC0000 + off, &val, 4);
  check(uc_mmio_map(m.eng.handle, REG_BASE, REG_SIZE, mmioRead, &m, mmioWrite, &m));
  uc_hook h1, h2;
  check(uc_hook_add(m.eng.handle, &h1, UC_HOOK_BLOCK, reinterpret_cast<void*>(onBlock), &m, 1, 0));
  check(uc_hook_add(m.eng.handle, &h2, UC_HOOK_INTR, reinterpret_cast<void*>(onIntr), &m, 1, 0));
  m.eng.reg(UC_MIPS_REG_CP0_STATUS, 0x01000000);   // EXPLORATORY handoff state: ERL/BEV clear, Status.MX (DSP ASE enable) set                       // EXPLORATORY: boot ROM leaves ERL/BEV clear
  m.eng.reg(UC_MIPS_REG_S6, 0xBFFE8000);                      // stage-1 stub: lui s6,0xbffe ; ori s6,s6,0x8000
  uc_err e = m.eng.run(0x80000400, 0);
  // `wait` (the eCos idle thread) halts the engine. EXPLORATORY idle model: advance CP0 Count to Compare so the timer interrupt fires, then resume.
  uint64_t idles = 0; const uint64_t maxIdles = maxIdle;
  while (e == UC_ERR_OK && m.stop.empty() && idles < maxIdles) {
    const uint32_t cmp = m.eng.reg(UC_MIPS_REG_CP0_COMPARE), cnt = m.eng.reg(UC_MIPS_REG_CP0_COUNT);
    uint32_t dist = cmp - cnt; if (!dist) dist = 1;
    if (!m.trigText.empty() && m.snap.empty() && m.uart.find(m.trigText) != std::string::npos) m.snap.assign(m.ring.begin(), m.ring.end());
    m.eng.tickCount(dist); ++idles;
    if (const char* di = std::getenv("SUNPLUS_DUMP_IDLES")) {   // debugging aid: SUNPLUS_DUMP_IDLES=n1,n2,.. with SUNPLUS_BEST_SCREEN=addr,w,h and SUNPLUS_DUMP_PREFIX writes the OSD buffer at those idle wake-ups
      for (const char* q = di; *q;) { char* e2 = nullptr; const uint64_t n = std::strtoull(q, &e2, 0); if (n == idles) { unsigned long ba = 0, bw = 0, bh = 0; const char* bs = std::getenv("SUNPLUS_BEST_SCREEN"); const char* pf = std::getenv("SUNPLUS_DUMP_PREFIX"); if (bs && pf && std::sscanf(bs, "%lx,%lu,%lu", &ba, &bw, &bh) == 3 && ba + bw * bh <= m.ram.size()) { std::ofstream o(std::string(pf) + std::to_string(n) + ".bin", std::ios::binary); o.write(reinterpret_cast<const char*>(m.ram.data() + ba), std::streamsize(bw * bh));
            if (const char* pa = std::getenv("SUNPLUS_PALETTE_ADDR")) { const unsigned long pp = std::strtoul(pa, nullptr, 16) & 0x1fffffff; std::ofstream op(std::string(pf) + std::to_string(n) + ".pal", std::ios::binary); if (pp + 1024 <= m.ram.size()) op.write(reinterpret_cast<const char*>(m.ram.data() + pp), 1024); } } } if (!e2 || e2 == q) break; q = *e2 == ',' ? e2 + 1 : e2; } }
    if (const char* fe = std::getenv("SUNPLUS_FLUSH_TB_IDLES")) { const uint64_t k = std::strtoull(fe, nullptr, 0); if (k && idles % k == 0) uc_ctl_flush_tb(m.eng.handle); }   // debugging aid for the host-side translation crash
    if (rtAfter && idles >= rtAfter) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!cmdFile.empty() && idles % 4 == 0) {   // command file: new lines are executed in order
      std::ifstream cf(cmdFile); std::string line; uint64_t ln = 0;
      while (std::getline(cf, line)) { if (ln++ < cmdLinesDone) continue; cmdLinesDone = ln;
        while (!line.empty() && (line.back() == 13 || line.back() == 32)) line.pop_back();
        if (line.rfind("key ", 0) == 0) { m.pendingKeys.push_back(uint32_t(std::strtoul(line.c_str() + 4, nullptr, 0))); }
        else if (line.rfind("shot ", 0) == 0) { const std::string path = line.substr(5); unsigned long ba = 0, bw = 0, bh = 0; const char* bs = std::getenv("SUNPLUS_BEST_SCREEN"); const char* pa = std::getenv("SUNPLUS_PALETTE_ADDR");
          if (bs && std::sscanf(bs, "%lx,%lu,%lu", &ba, &bw, &bh) == 3 && ba + bw * bh <= m.ram.size()) { { std::ofstream o(path, std::ios::binary); o.write(reinterpret_cast<const char*>(m.ram.data() + ba), std::streamsize(bw * bh)); }
            if (pa) { const unsigned long pp = std::strtoul(pa, nullptr, 16) & 0x1fffffff; std::ofstream o(path + ".pal", std::ios::binary); if (pp + 1024 <= m.ram.size()) o.write(reinterpret_cast<const char*>(m.ram.data() + pp), 1024); }
            { std::ofstream o(path + ".done", std::ios::binary); o << idles; } } } }
    }
    static const uint64_t keyQuiet = std::getenv("SUNPLUS_KEY_QUIET") ? std::strtoull(std::getenv("SUNPLUS_KEY_QUIET"), nullptr, 0) : 400000;
    if (!m.pendingKeys.empty() && m.blocks - m.lastKickBlocks > keyQuiet && m.blocks - m.lastKeyBlocks > keyQuiet) {   // keys are injected only when the firmware has been quiet (no GPU drawing) and not too soon after the previous key: the host call borrows the idle thread and must not block on a lock another thread holds
      HostCall hc{}; hc.fn = keyFn; hc.a0 = m.pendingKeys.front(); hc.a1 = 0; hc.at = idles; calls.push_back(hc); m.pendingKeys.pop_front(); m.lastKeyBlocks = m.blocks; }
    for (auto& c : calls) if (!c.done && idles == c.at) {   // EXPLORATORY host call: run the firmware function to completion on a scratch stack with interrupts masked, then resume the idle loop
      c.done = true;
      uint32_t saved[40]; const int ids[] = {UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_V0, UC_MIPS_REG_V1, UC_MIPS_REG_RA, UC_MIPS_REG_SP, UC_MIPS_REG_PC, UC_MIPS_REG_CP0_STATUS, UC_MIPS_REG_S0, UC_MIPS_REG_S1, UC_MIPS_REG_S2, UC_MIPS_REG_S3, UC_MIPS_REG_S4, UC_MIPS_REG_S5, UC_MIPS_REG_S6, UC_MIPS_REG_S7, UC_MIPS_REG_FP, UC_MIPS_REG_T0, UC_MIPS_REG_T1, UC_MIPS_REG_T2, UC_MIPS_REG_T3, UC_MIPS_REG_T4, UC_MIPS_REG_T5, UC_MIPS_REG_T6, UC_MIPS_REG_T7, UC_MIPS_REG_T8, UC_MIPS_REG_T9, UC_MIPS_REG_GP};
      const int nids = int(sizeof ids / sizeof ids[0]);
      for (int k = 0; k < nids; ++k) saved[k] = m.eng.reg(ids[k]);
      const uint32_t pc = saved[8], status = saved[9];
      m.eng.reg(UC_MIPS_REG_A0, c.a0); m.eng.reg(UC_MIPS_REG_A1, c.a1);
      m.eng.reg(UC_MIPS_REG_RA, pc); m.eng.reg(UC_MIPS_REG_SP, 0x87F80000);
      m.eng.reg(UC_MIPS_REG_CP0_STATUS, status & ~1u);
      const uint64_t b0 = m.blocks;
      uc_err ce = uc_emu_start(m.eng.handle, c.fn, pc, 0, 5000000);
      std::printf("host call %08x(%08x,%08x) at idle wake %llu: %s, %llu blocks, v0=%08x\n", c.fn, c.a0, c.a1, (unsigned long long)idles, ce == UC_ERR_OK ? "ok" : uc_strerror(ce), (unsigned long long)(m.blocks - b0), m.eng.reg(UC_MIPS_REG_V0));
      std::fflush(stdout);   // drivers (menu_viewer, tour recorders) wait for this line to know a queued key was delivered
      for (int k = 0; k < nids; ++k) m.eng.reg(ids[k], saved[k]);
      if (!m.stop.empty()) break;
    }
    if (irqMask) m.eng.setIrqLines(irqMask);
    if (irqAuto) {   // EXPLORATORY: raise (round robin) only vectors whose mask bit the firmware has set
      auto maskOf = [&](int v, uint32_t& reg, uint32_t& bit) { if (v >= 0x40 && v <= 0x47) { reg = 0x49c; bit = v - 0x40; return true; } if (v >= 0x48 && v <= 0x5b) { reg = 0x4a0; bit = v - 0x40; return true; } if (v >= 0x5c && v <= 0x5f) { reg = 0x4a4; bit = v - 0x40; return true; } if (v >= 0x21 && v <= 0x30) { reg = 0x4a8; bit = v - 0x20; return true; } if (v >= 0x31 && v <= 0x3f) { reg = 0x4ac; bit = v - 0x20; return true; } return false; };
      irqVecs.clear();
      for (int v = 0x21; v <= 0x5f; ++v) { uint32_t r, bt; if (!irqExcl.count(v) && maskOf(v, r, bt) && m.written.count(r) && ((m.written[r] >> bt) & 1)) irqVecs.push_back(v); }
      irqVec = irqVecs.empty() ? -1 : 0;
    }
    if (irqVec >= 0 && !irqVecs.empty()) {   // vector -> (CPU line, pending register, bit) from the kernel's dispatcher and unmask tables
      irqVec = irqVecs[idles % irqVecs.size()];
      int line; uint32_t reg, bit;
      if (irqVec >= 0x40 && irqVec <= 0x47) { line = 0; reg = 0x4cc; bit = irqVec - 0x40; }
      else if (irqVec >= 0x48 && irqVec <= 0x5b) { line = 1; reg = 0x4d0; bit = irqVec - 0x40; }
      else if (irqVec >= 0x5c && irqVec <= 0x5f) { line = 2; reg = 0x4d4; bit = irqVec - 0x40; }
      else if (irqVec >= 0x21 && irqVec <= 0x30) { line = 3; reg = 0x4d8; bit = irqVec - 0x20; }
      else { line = 4; reg = 0x4dc; bit = irqVec - 0x20; }
      m.lastVec = irqVec; m.pendReg = reg; m.pendBit = 1u << bit; m.pendLeft = 4; ++m.irqInjected;
      m.eng.setIrqLines(1u << line);
    }
    e = m.eng.run(m.eng.reg(UC_MIPS_REG_PC), 0);
  }
  if (!saveRam.empty()) {   // debugging aid: GPRs, PC, HI/LO, Status/Count/Compare then all RAM, so a host-side Unicorn crash can be replayed standalone
    std::ofstream o(saveRam, std::ios::binary); uint32_t r[38]; for (int k = 0; k < 32; ++k) r[k] = m.eng.reg(UC_MIPS_REG_0 + k);
    r[32] = m.eng.reg(UC_MIPS_REG_PC); r[33] = m.eng.reg(UC_MIPS_REG_HI); r[34] = m.eng.reg(UC_MIPS_REG_LO); r[35] = m.eng.reg(UC_MIPS_REG_CP0_STATUS); r[36] = m.eng.reg(UC_MIPS_REG_CP0_COUNT); r[37] = m.eng.reg(UC_MIPS_REG_CP0_COMPARE);
    o.write(reinterpret_cast<char*>(r), sizeof r); o.write(reinterpret_cast<char*>(m.ram.data()), std::streamsize(m.ram.size()));
  }
  for (uint32_t pk : peeks) { uint32_t v = 0; m.eng.read(pk & 0x1FFFFFFF, &v, 4); std::printf("peek %08x = %08x\n", pk, v); }
  for (auto& [ad, ln] : dumps) { std::vector<uint8_t> buf(ln); m.eng.read(ad & 0x1FFFFFFF, buf.data(), ln); std::printf("dump %08x:", ad); for (uint32_t k = 0; k < ln; ++k) std::printf("%02x", buf[k]); std::printf("\n"); }
  if (m.hotN) { std::vector<std::pair<uint64_t, uint32_t>> v; for (auto& [a, n] : m.hot) v.push_back({n, a}); std::sort(v.rbegin(), v.rend()); std::printf("hot blocks:"); for (size_t k = 0; k < v.size() && k < m.hotN; ++k) std::printf(" %08x:%llu", v[k].second, (unsigned long long)v[k].first); std::printf("\n"); }
  if (coverHi) { std::printf("covered blocks %08x..%08x:", coverLo, coverHi); for (auto& [a, n] : m.hot) if (a >= coverLo && a < coverHi) std::printf(" %08x:%llu", a, (unsigned long long)n); std::printf("\n"); }
  if (m.spHi) { std::printf("thread blocks (sp %08x..%08x), oldest..newest:", m.spLo, m.spHi); for (uint32_t x : m.spRing) std::printf(" %08x", x); std::printf("\n"); }
  if (!m.bestScreen.empty()) { if (const char* bf = std::getenv("SUNPLUS_BEST_FILE")) { std::ofstream o(bf, std::ios::binary); o.write(reinterpret_cast<const char*>(m.bestScreen.data()), std::streamsize(m.bestScreen.size())); } std::printf("best screen: %llu non-zero px after kick %llu\n", (unsigned long long)m.bestNz, (unsigned long long)m.bestKick); }
  if (const char* sh = std::getenv("SUNPLUS_SCREEN_HASH")) { unsigned long ba = 0, bw = 0, bh = 0; if (std::sscanf(sh, "%lx,%lu,%lu", &ba, &bw, &bh) == 3 && ba + bw * bh <= m.ram.size()) { uint64_t h = 1469598103934665603ull; uint64_t nz = 0; for (size_t q = 0; q < bw * bh; ++q) { h = (h ^ m.ram[ba + q]) * 1099511628211ull; nz += m.ram[ba + q] != 0; } std::printf("screen hash %016llx nz %llu kicks %llu\n", (unsigned long long)h, (unsigned long long)nz, (unsigned long long)m.gpuKicks); } }   // debugging aid
  if (m.gpuOn) std::printf("gpu: cmd0 %llu cmd3 %llu cmd5 %llu other(1,2,4) %llu/%llu/%llu unsupported %llu clipped px %llu\n", (unsigned long long)m.gpu.executed[0], (unsigned long long)m.gpu.executed[3], (unsigned long long)m.gpu.executed[5], (unsigned long long)m.gpu.executed[1], (unsigned long long)m.gpu.executed[2], (unsigned long long)m.gpu.executed[4], (unsigned long long)m.gpu.unsupported, (unsigned long long)m.gpu.clipped);
  if (!m.thr.empty()) { std::printf("threads (stack bucket -> last block, at blocks):\n"); for (auto& [k, v] : m.thr) std::printf("  sp %08x: %08x @%llu\n", k << 14, v.first, (unsigned long long)v.second); }
  std::printf("last injected vector: %#x (injections %llu)\n", m.lastVec, (unsigned long long)m.irqInjected);
  if (m.stop.rfind("stuck polling", 0) == 0 && m.lastBlock()) { uint8_t cb[96]; m.eng.read(m.lastBlock() & 0x1FFFFFFF, cb, sizeof cb); std::printf("stuck-code %08x:", m.lastBlock()); for (uint8_t x : cb) std::printf("%02x", x); std::printf("\n"); }
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
  if (!m.snap.empty()) { std::printf("snapshot at first idle after trigger (oldest..newest, %zu blocks):\n", m.snap.size()); for (size_t i = 0; i < m.snap.size(); ++i) std::printf("%08x%c", m.snap[i], (i % 12 == 11) ? '\n' : ' '); std::printf("\n"); }
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
