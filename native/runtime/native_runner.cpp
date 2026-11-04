// Native CPU for the existing machine: executes the statically recompiled firmware while every peripheral, the boot state (from a snapshot), the
// preview and the virtual remote stay in manta::Msd309Machine. Built only into manta_native_full (see the root CMakeLists.txt, target manta_native_full).
//
// What is NOT carried over from the interpreter-based machine: the per-instruction code hook (provenance tracking of unknown register bits, loop skipping,
// coverage, traces). Hardware reads that the machine would stop on still stop it (the stop is raised inside the MMIO functions), so use the lenient
// diagnostics (e.g. --diagnostic-riu-zero-unknown-reads) for runs that should continue past unknown registers.
#include "board/machine.hpp"
#include "rt.hpp"
#include "pages.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace manta;

// Friend of Msd309Machine: names the internals the native CPU needs.
struct NativeAccess {
  Msd309Machine& m;
  Engine& cpu;
  std::vector<uint8_t>& dram;
  Options& options;
  bool& stopped;
  uint64_t& dispatches;
  explicit NativeAccess(Msd309Machine& machine) : m(machine), cpu(machine.cpu_), dram(machine.dramBacking_), options(machine.options_), stopped(machine.stopped_), dispatches(machine.dispatches_) {}
  Riu& riu() { return m.riu_; }
  Copro& copro() { return m.copro_; }
  uint32_t irqLines() const { return m.irqLinesApplied_; }
  bool remoteWork() const { return !m.options_.irScript.empty() || m.options_.remoteQueue || !m.irWaiting_.empty(); }
  uint32_t cpi() const { return m.virtualTime_.cpi(); }
  void restore() { m.restoreSnapshot(m.options_.loadState); }
  bool resumed() const { return m.resumed_; }
  uint32_t resumePc() const { return m.resumePc_; }
  void event(const char* kind, uint32_t a, unsigned n, uint32_t v, const char* detail) { m.event(EvText(kind), a, n, v, EvText(detail)); }
  void stop(const std::string& why, uint32_t a) { m.stop(why, a); }
  uint64_t mmioRead(uint32_t off, unsigned n) { return Msd309Machine::mmioRead(nullptr, off, n, &m); }
  void mmioWrite(uint32_t off, unsigned n, uint32_t v) { Msd309Machine::mmioWrite(nullptr, off, n, v, &m); }
  void serviceRemote() { m.serviceVirtualRemote(); }
  void preview() { m.publishPreviewLive(); }
  bool previewDue() { return m.previewDue(); }
  void finish(uint32_t pc, bool fatal);
};

struct MachineBus : Bus {
  NativeAccess& a;
  explicit MachineBus(NativeAccess& access) : a(access) {}
  void sync(CPU& c, uint32_t pc) {
    if (a.cpu.gpr) {
      std::memcpy(a.cpu.gpr, c.r, sizeof c.r);
      a.cpu.gpr[32] = pc;
    } else {
      for (int i = 0; i < 32; ++i) a.cpu.reg(UC_MIPS_REG_0 + i, c.r[i]);
      a.cpu.reg(UC_MIPS_REG_PC, pc);
    }
  }
  uint32_t fault(CPU& c, uint32_t addr, uint32_t pc) {
    sync(c, pc);
    a.event("memory_fault", addr, 4, 0, "native CPU: access outside every mapped region");
    a.stop("unmapped/protected guest access", addr);
    c.halt = true;
    return 0;
  }
  uint32_t read(CPU& c, uint32_t addr, unsigned n, uint32_t pc) override {
    uint32_t p = addr & 0x1fffffffu;
    if (addr >= 0xC0000000u) {   // kseg2: MsOS_PA2Uncached maps MIU1 at D0000000..D7FFFFFF (bus 40000000..47FFFFFF) = DRAM backing + 128 MB
      if (addr >= 0xD0000000u && addr < 0xE8000000u) {
        uint64_t off = 0x08000000ull + (addr - 0xD0000000u);
        if (off + n <= a.dram.size()) { uint32_t v = 0; std::memcpy(&v, a.dram.data() + off, n); return v; }
      }
      return fault(c, addr, pc);
    }
    if (p >= 0x1f000000u && p < 0x1f400000u) {
      sync(c, pc);
      uint32_t v = uint32_t(a.mmioRead(p - 0x1f000000u, n));
      if (a.stopped) c.halt = true;
      return v;
    }
    if (p >= 0x1fc00000u || (p >= 0x14000000u && p < 0x14800000u && a.options.candidateSpi)) {   // flash aperture / XIU window (read-only memory in the machine)
      uint32_t v = 0;
      a.cpu.read(p, &v, n);
      return v;
    }
    return fault(c, addr, pc);
  }
  void write(CPU& c, uint32_t addr, unsigned n, uint32_t v, uint32_t pc) override {
    uint32_t p = addr & 0x1fffffffu;
    if (addr >= 0xC0000000u) {
      if (addr >= 0xD0000000u && addr < 0xE8000000u) {
        uint64_t off = 0x08000000ull + (addr - 0xD0000000u);
        if (off + n <= a.dram.size()) { std::memcpy(a.dram.data() + off, &v, n); return; }
      }
      fault(c, addr, pc);
      return;
    }
    if (p >= 0x1f000000u && p < 0x1f400000u) {
      sync(c, pc);
      a.mmioWrite(p - 0x1f000000u, n, v);
      if (a.stopped) c.halt = true;
      return;
    }
    fault(c, addr, pc);
  }
};

struct Runner {
  NativeAccess& a;
  uint64_t lastIcount = 0;
  pages::Host pages;
};

static void after_run(CPU& c, uint32_t pc, void* ctx) {
  auto& r = *static_cast<Runner*>(ctx);
  auto& a = r.a;
  uint64_t delta = c.icount - r.lastIcount;
  r.lastIcount = c.icount;
  uint64_t prev = a.dispatches, cur = prev + delta;
  a.dispatches = cur;
  c.last_pc = pc;
  if (a.stopped) { c.halt = true; return; }
  r.pages.service(c, cur);
  // the interpreter's hook runs these on exact multiples of 0x4000 dispatches; here on the first run boundary after crossing
  const uint64_t blocks = (cur >> 14) - (prev >> 14);
  if (blocks) {
    if (a.options.exploratoryCopro) for (uint64_t i = 0; i < blocks; ++i) a.copro().run(0x4000);
    if (a.remoteWork()) a.serviceRemote();
    c.ext_irq = a.irqLines();
  }
  if (a.options.previewStore && ((cur / (a.options.previewMask + 1)) != (prev / (a.options.previewMask + 1))) && a.previewDue()) a.preview();
  else if (!a.options.gopFrameHistory.empty() && ((cur >> a.options.gopHistoryLog2) != (prev >> a.options.gopHistoryLog2))) a.preview();
  if (a.stopped) c.halt = true;
}

void NativeAccess::finish(uint32_t pc, bool fatal) {
  if (!m.stopped_) {
    m.blockingPc_ = pc;
    m.reason_ = fatal ? "native CPU stopped (unsupported instruction or dispatch into unrecompiled code)" : "instruction budget exhausted";
  }
  m.publishPreview();
  auto s = m.summary();
  if (!m.options_.summary.empty()) {
    std::ofstream f(m.options_.summary);
    f << s << '\n';
  }
  if (!m.options_.exportGopFrame.empty()) m.exportGopFrames();
  if (!m.options_.gopFrameHistory.empty()) {
    m.publishPreview();
    std::ofstream idx(m.options_.gopFrameHistory / "index.json");
    idx << "{\n  \"note\": \"diagnostic GOP preview timeline; format 6 assumed ARGB1555; not panel output\",\n  \"frames\": [\n    " << m.historyIndex_ << "\n  ]\n}\n";
  }
  std::cout << s << "\nUART: " << m.riu_.uart << '\n';
}

// Takeover from the snapshot: restore it, load the CPU state into the native CPU, wire bus, hooks and host pages. Shared by the blocking run (nativeRun)
// and the browser build's step-wise session (MANTA_WEB), which resumes run_guest in slices from the page's frame loop.
static uint32_t nativeTakeover(NativeAccess& a, CPU& c, MachineBus& bus, Runner& runner, RunHooks& hooks) {
  if (a.options.loadState.empty()) throw std::runtime_error("--native-cpu needs --load-state (the native CPU takes over from a saved machine state)");
  if (a.cpi() < 2) throw std::runtime_error("--native-cpu needs --exploratory-cpi >= 2");
  a.restore();
  c.ram = a.dram.data();
  c.ram_size = uint32_t(a.dram.size());
  for (int i = 0; i < 32; ++i) c.r[i] = a.cpu.reg(UC_MIPS_REG_0 + i);
  std::fprintf(stderr, "native: takeover pc=%08x sp=%08x ra=%08x resumed=%d\n", a.resumed() ? a.resumePc() : a.cpu.reg(UC_MIPS_REG_PC), c.r[29], c.r[31], int(a.resumed()));
  c.hi = a.cpu.reg(UC_MIPS_REG_HI);
  c.lo = a.cpu.reg(UC_MIPS_REG_LO);
  uint32_t pc = a.resumed() ? a.resumePc() : a.cpu.reg(UC_MIPS_REG_PC);
  c.cp0[12][0] = a.cpu.reg(UC_MIPS_REG_CP0_STATUS);
  c.cp0[13][0] = a.cpu.reg(UC_MIPS_REG_CP0_CAUSE);
  c.cp0[14][0] = a.cpu.reg(UC_MIPS_REG_CP0_EPC);
  c.cp0[11][0] = a.cpu.reg(UC_MIPS_REG_CP0_COMPARE);
  c.cp0[15][1] = 0x803b0000u;   // EBase: the firmware's vector is observed at 0x803B0180 (ASSUMPTION for the takeover state; later MTC0 writes are tracked)
  c.count_per_insn = a.cpi() / 2;
  c.count_off = a.cpu.reg(UC_MIPS_REG_CP0_COUNT);
  c.prev_count = c.count_off;
  c.ip7 = (a.cpu.reg(UC_MIPS_REG_CP0_CAUSE) >> 15) & 1;
  c.ext_irq = a.irqLines();
  c.bus = &bus;
  hooks.after_run = after_run;
  hooks.ctx = &runner;
  runner.pages.testOskAt = a.options.exploratoryOskTestAt;
  runner.pages.attach(hooks);
  if (a.options.previewStore) runner.pages.activeFlag = &a.options.previewStore->remote->oskActive;
  a.options.remoteFilter = [&runner](const std::string& f) { return runner.pages.onFunction(f); };
  if (a.options.exploratoryMenuNoAutoexit) {
    hooks.redirect_pc = 0x804F0724u; hooks.redirect_ra = 0x804CA098u; hooks.redirect_a0 = 0x17; hooks.redirect_a1 = 5; hooks.redirect_to = 0x804C8624u;
    // MENU (app key 0x29) and EXIT (app key 0x09) must still close the menu: the original path runs when the key handler 8046A754 last received (a1) one of them
    hooks.key_pc = 0x8046A754u; hooks.redirect_unless_keys[0] = 0x29; hooks.redirect_unless_keys[1] = 0x09; hooks.redirect_unless_n = 2;
    std::fprintf(stderr, "EXPLORATORY: --exploratory-menu-no-autoexit: the menu page handler's exit request (804F0724(0x17,5) from 804CA098) is ignored (not original behaviour)%c", 10);
  }
  if (a.options.exploratorySkipZuiTimers) {
    hooks.skip_pc = 0x804A0770u;
    std::fprintf(stderr, "EXPLORATORY: --exploratory-skip-zui-timers: the original ZUI software-timer service 804A0770 is skipped (no ZUI timer expires; not original behaviour)\n");
  }
  if (const char* spec = std::getenv("MANTA_TRACE_ENTRY")) {   // diagnostic: "PC[:REG:VALUE],..." (hex)
    int k = 0;
    for (const char* p = spec; *p && k < 4; ++k) {
      char* end = nullptr;
      hooks.trace_pc[k] = uint32_t(std::strtoul(p, &end, 16));
      if (*end == ':') { hooks.trace_reg[k] = uint32_t(std::strtoul(end + 1, &end, 16)); if (*end == ':') hooks.trace_val[k] = uint32_t(std::strtoul(end + 1, &end, 16)); }
      p = *end == ',' ? end + 1 : end;
    }
    hooks.trace_base = a.dispatches;
  }
  return pc;
}

int nativeRun(Msd309Machine& machine) {
  NativeAccess a(machine);
  CPU c;
  MachineBus bus(a);
  Runner runner{a, 0};
  RunHooks hooks;
  uint32_t pc = nativeTakeover(a, c, bus, runner, hooks);
  uint64_t limit = a.options.limit ? a.options.limit : UINT64_MAX;
  uint64_t budget = limit > a.dispatches ? limit - a.dispatches : 1;
  run_guest(c, pc, budget, &hooks);
  pc = c.last_pc;
  bus.sync(c, pc);   // hand the final state back to the machine object so the summary shows it
  a.cpu.reg(UC_MIPS_REG_HI, c.hi);
  a.cpu.reg(UC_MIPS_REG_LO, c.lo);
  if (hooks.skip_pc) std::fprintf(stderr, "EXPLORATORY: skipped calls to %08x: %llu\n", hooks.skip_pc, (unsigned long long)hooks.skip_count);
  if (hooks.redirect_pc) std::fprintf(stderr, "EXPLORATORY: ignored menu exit requests: %llu, passed through after MENU/EXIT: %llu%c", (unsigned long long)hooks.redirect_count, (unsigned long long)hooks.redirect_passed, 10);
  if (std::getenv("MANTA_RING")) { std::fprintf(stderr, "RING (most recent run entries, oldest first):"); for (unsigned i = 0; i < 64; ++i) std::fprintf(stderr, " %08x", c.ring[(c.ring_i + i) & 63]); std::fprintf(stderr, "%c", 10); }
  a.finish(pc, c.fatal);
  std::fprintf(stderr, "native CPU: %llu guest instructions executed natively, %llu timer interrupts, %llu external, %llu exceptions\n", (unsigned long long)c.icount,
               (unsigned long long)c.timer_irqs, (unsigned long long)c.ext_irqs, (unsigned long long)c.exceptions);
  return 2;
}

#ifdef MANTA_WEB
// Browser build (Emscripten, no threads): the page's frame loop calls manta_step() for a slice of guest instructions, pushes remote functions with
// manta_key() and reads the latest published GOP preview frame (composed like the Linux presenter: guest pixels, else the video-plane colour, else
// black, padded to the whole panel) with manta_frame_*. Same machine, same recompiled firmware, same options as the desktop build.
#include <emscripten/emscripten.h>
#include <memory>
namespace {
struct WebSession {
  NativeAccess a; CPU c; MachineBus bus; Runner runner; RunHooks hooks; uint32_t pc = 0; bool done = false;
  explicit WebSession(Msd309Machine& m) : a(m), bus(a), runner{a, 0} {}
};
WebSession* web = nullptr;
std::vector<uint8_t> webRgba;
uint32_t webW = 0, webH = 0;
uint64_t webSeq = 0;
}
void webStart(Msd309Machine& machine) {
  web = new WebSession(machine);
  web->pc = nativeTakeover(web->a, web->c, web->bus, web->runner, web->hooks);
}
extern "C" {
// Runs about n more guest instructions. 0 = running, 1 = the guest stopped (budget, fatal or a machine stop), -1 = no session.
EMSCRIPTEN_KEEPALIVE int manta_step(double n) {
  if (!web) return -1;
  if (web->done) return 1;
  run_guest(web->c, web->pc, web->c.icount + uint64_t(n), &web->hooks);
  web->pc = web->c.last_pc;
  if (web->c.halt || web->a.stopped || web->c.fatal) {
    web->done = true;
    web->bus.sync(web->c, web->pc);
    web->a.finish(web->pc, web->c.fatal);
  }
  return web->done ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE double manta_dispatches() { return web ? double(web->a.dispatches) : 0.0; }
// Remote function name as used by the desktop build ("MENU", "UP", "DIGIT_8", "raw=0x15", ...).
EMSCRIPTEN_KEEPALIVE void manta_key(const char* function) {
  if (web && web->a.options.previewStore) web->a.options.previewStore->remote->push(function);
}
// Composes the latest published frame into RGBA if it is new; returns its sequence number (0 = none yet).
EMSCRIPTEN_KEEPALIVE double manta_frame_update() {
  if (!web || !web->a.options.previewStore) return 0;
  auto f = web->a.options.previewStore->latest();
  if (!f || f->empty()) return double(webSeq);
  if (f->sequence == webSeq) return double(webSeq);
  // The published frame only spans the enabled GOP windows' extent (from the panel origin). The page always shows the whole 1360x768 panel:
  // outside that extent the video plane shows (its colour, or black when no plane is active), as on the TV, so a small OSD such as the source
  // list is never stretched over the screen.
  constexpr uint32_t kPanelW = 1360, kPanelH = 768;
  webSeq = f->sequence; webW = std::max<uint32_t>(f->width, kPanelW); webH = std::max<uint32_t>(f->height, kPanelH);
  webRgba.resize(size_t(webW) * webH * 4);
  const uint32_t outside = f->planeActive ? (f->planeRgb & 0xffffffu) : 0u;
  for (uint32_t y = 0; y < webH; ++y) for (uint32_t x = 0; x < webW; ++x) {
    uint32_t rgb = outside;
    if (x < f->width && y < f->height) {
      const size_t i = size_t(y) * f->width + x;
      rgb = f->covered[i] ? (f->bgra[i] & 0xffffffu) : !f->planeBgra.empty() ? (f->planeBgra[i] & 0xffffffu)
          : outside;
    }
    uint8_t* o = &webRgba[(size_t(y) * webW + x) * 4];
    o[0] = uint8_t(rgb >> 16); o[1] = uint8_t(rgb >> 8); o[2] = uint8_t(rgb); o[3] = 255;
  }
  return double(webSeq);
}
EMSCRIPTEN_KEEPALIVE uint8_t* manta_frame_rgba() { return webRgba.data(); }
EMSCRIPTEN_KEEPALIVE int manta_frame_width() { return int(webW); }
EMSCRIPTEN_KEEPALIVE int manta_frame_height() { return int(webH); }
}
#endif
