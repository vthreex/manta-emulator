#pragma once
// Runtime for the statically recompiled MIPS32 firmware (see native/tools/mips_recomp.py).
#include <cstdint>
#include <cstdio>
#include <cstring>

struct CPU;
struct Bus {
  virtual ~Bus() {}
  virtual uint32_t read(CPU& c, uint32_t addr, unsigned size, uint32_t pc) = 0;
  virtual void write(CPU& c, uint32_t addr, unsigned size, uint32_t value, uint32_t pc) = 0;
};

struct CPU {
  uint32_t r[32] = {};
  uint32_t hi = 0, lo = 0;
  uint64_t icount = 0;            // guest instructions executed (counted per run, at run entry)
  uint32_t cp0[32][8] = {};
  uint32_t hwr[32] = {};
  uint8_t* ram = nullptr;
  uint32_t ram_size = 0;
  Bus* bus = nullptr;
  int wait = 0;
  uint32_t count_off = 0;         // Count = (uint32)(icount * count_per_insn) + count_off
  uint32_t count_per_insn = 8;    // ASSUMPTION: same virtual-time ratio as the interpreter-based emulator (--exploratory-cpi 16 => 8 Count ticks per instruction)
  uint32_t prev_count = 0;
  bool ip7 = false;               // timer interrupt pending (cleared by writing Compare)
  uint32_t ext_irq = 0;           // external interrupt lines IP2..IP6 (bit0 = IP2)
  uint32_t last_pc = 0;
  bool fatal = false;
  bool halt = false;              // set by the bus/hooks to end the run loop
  uint64_t timer_irqs = 0, ext_irqs = 0, exceptions = 0;
  uint32_t ring[64] = {};         // recently dispatched run starts, for diagnostics
  unsigned ring_i = 0;
  void* hooks = nullptr;          // the RunHooks of the active run_guest (for nested guest calls)
  // Asynchronous call injection (EXPLORATORY features): the host asks for a guest function to be called at the next run boundary. The registers are saved in a
  // frame on the guest stack; when the function returns to the trampoline address the registers are restored and the interrupted code resumes. The function
  // runs in the normal loop (interrupts, host services and time all keep working).
  struct { bool pending = false; uint32_t target = 0, a0 = 0, a1 = 0; } inject;
  bool injecting = false;
};

typedef uint32_t (*RunFn)(CPU*, uint32_t);
// Host extension point (EXPLORATORY features): when a dispatch reaches `pc`, fn(c, ctx) runs first. If it returns true the original guest function is NOT
// executed: the host has set v0 and the call returns to ra at once. If it returns false the original runs unchanged.
struct EntryHook { uint32_t pc = 0; bool (*fn)(CPU&, void*) = nullptr; void* ctx = nullptr; };
struct RunHooks {
  void (*after_run)(CPU&, uint32_t pc, void* ctx) = nullptr;   // called after every run, before interrupt delivery (host services, time)
  void* ctx = nullptr;
  // EXPLORATORY (opt-in) redirect: a dispatch to redirect_pc made with ra == redirect_ra, a0 == redirect_a0 and a1 == redirect_a1 continues at redirect_to with v0 = 1
  // instead (0 = off). Used to ignore one specific original call site, never a whole function.
  uint32_t redirect_pc = 0, redirect_ra = 0, redirect_a0 = 0, redirect_a1 = 0, redirect_to = 0;
  uint64_t redirect_count = 0;
  // ...except right after the guest key handler (key_pc, application key code in a1) received one of redirect_unless_keys (e.g. MENU/EXIT)
  uint32_t key_pc = 0, last_key = 0; uint64_t last_key_icount = 0; uint32_t redirect_unless_keys[4] = {}; int redirect_unless_n = 0; uint64_t redirect_passed = 0;
  EntryHook entry[16];
  unsigned entry_n = 0;
  uint64_t skip_count = 0;
  uint32_t trace_pc[4] = {0, 0, 0, 0}, trace_reg[4] = {0, 0, 0, 0}, trace_val[4] = {0, 0, 0, 0};   // diagnostic: log entries of trace_pc[i] where r[trace_reg[i]] == trace_val[i] (trace_reg 0 = any)
  uint64_t trace_base = 0;
  uint32_t skip_pc = 0;   // EXPLORATORY (opt-in): a call entering this guest function returns at once (v0 = 0) without running it; 0 = off
};
uint64_t run_guest(CPU& c, uint32_t pc, uint64_t limit, RunHooks* hooks = nullptr);
constexpr uint32_t kInjectReturn = 0xbfffff10u;   // trampoline address a function called through CPU::inject returns to
// Calls a guest function from host code at a run boundary (registers are saved and restored; no interrupts are delivered meanwhile). Returns v0.
uint32_t call_guest(CPU& c, uint32_t pc, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint64_t budget = 200000000);
extern const uint32_t GEN_BASE, GEN_WORDS;
extern const uint32_t GEN_INDEX[];
extern const RunFn GEN_RUNS[];
extern const unsigned GEN_RUN_COUNT;

#ifdef RT_TRACE_STORES
extern uint32_t g_store_addr[65536];
extern unsigned g_store_len[65536];
extern unsigned g_store_n;
extern bool g_bus_touched;
#define LOOPCHK(c) do { if ((c)->icount > 5000) throw 3; } while (0)   // differential test only: random inputs can make in-run delay loops (nearly) endless
#define NOTE_STORE(p, n) do { if (g_store_n < 65536) { g_store_addr[g_store_n] = (p); g_store_len[g_store_n] = (n); ++g_store_n; } } while (0)
#else
#define NOTE_STORE(p, n) do {} while (0)
#define LOOPCHK(c) do {} while (0)
#endif
uint32_t bus_read(CPU* c, uint32_t a, unsigned n, uint32_t pc);
void bus_write(CPU* c, uint32_t a, unsigned n, uint32_t v, uint32_t pc);

#define ROR32(x, n) ((uint32_t)(((x) >> ((n) & 31)) | ((x) << ((32 - ((n) & 31)) & 31))))

static inline uint32_t LW(CPU* c, uint32_t a, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p + 4 <= c->ram_size) { uint32_t v; memcpy(&v, c->ram + p, 4); return v; }
  return bus_read(c, a, 4, pc);
}
static inline uint32_t LH(CPU* c, uint32_t a, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p + 2 <= c->ram_size) { uint16_t v; memcpy(&v, c->ram + p, 2); return v; }
  return bus_read(c, a, 2, pc);
}
static inline uint32_t LB(CPU* c, uint32_t a, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p < c->ram_size) return c->ram[p];
  return bus_read(c, a, 1, pc);
}
static inline void SW(CPU* c, uint32_t a, uint32_t v, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p + 4 <= c->ram_size) { NOTE_STORE(p, 4); memcpy(c->ram + p, &v, 4); return; }
  bus_write(c, a, 4, v, pc);
}
static inline void SH(CPU* c, uint32_t a, uint32_t v, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p + 2 <= c->ram_size) { NOTE_STORE(p, 2); uint16_t h = uint16_t(v); memcpy(c->ram + p, &h, 2); return; }
  bus_write(c, a, 2, v & 0xffffu, pc);
}
static inline void SB(CPU* c, uint32_t a, uint32_t v, uint32_t pc) {
  uint32_t p = a & 0x1fffffffu;
  if (p < c->ram_size) { NOTE_STORE(p, 1); c->ram[p] = uint8_t(v); return; }
  bus_write(c, a, 1, v & 0xffu, pc);
}
// unaligned partial accesses (little-endian MIPS)
static inline uint32_t LWL(CPU* c, uint32_t a, uint32_t rt, uint32_t pc) {
  uint32_t w = LW(c, a & ~3u, pc), sh = (3 - (a & 3)) * 8;
  return (w << sh) | (rt & ((1u << sh) - 1));
}
static inline uint32_t LWR(CPU* c, uint32_t a, uint32_t rt, uint32_t pc) {
  uint32_t w = LW(c, a & ~3u, pc), sh = (a & 3) * 8;
  return (w >> sh) | (sh ? (rt & ~(0xffffffffu >> sh)) : 0u);
}
static inline void SWL(CPU* c, uint32_t a, uint32_t rt, uint32_t pc) {
  uint32_t w = LW(c, a & ~3u, pc), sh = (3 - (a & 3)) * 8;
  SW(c, a & ~3u, (w & ~(0xffffffffu >> sh)) | (rt >> sh), pc);
}
static inline void SWR(CPU* c, uint32_t a, uint32_t rt, uint32_t pc) {
  uint32_t w = LW(c, a & ~3u, pc), sh = (a & 3) * 8;
  SW(c, a & ~3u, (w & ~(0xffffffffu << sh)) | (rt << sh), pc);
}

uint32_t CP0R(CPU* c, unsigned reg, unsigned sel);
void CP0W(CPU* c, unsigned reg, unsigned sel, uint32_t v);
void CP0EI(CPU* c);
void CP0DI(CPU* c);
uint32_t ERET(CPU* c);
uint32_t RAISE_SYSCALL(CPU* c, uint32_t pc);
uint32_t RAISE_BREAK(CPU* c, uint32_t pc);
uint32_t RAISE_TRAP(CPU* c, uint32_t pc);
[[noreturn]] void TRAP_IMPL(CPU* c, uint32_t pc, uint32_t word);
uint32_t BADPC(CPU* c, uint32_t pc);
#define TRAP(c, pc, w) TRAP_IMPL(c, pc, w)
