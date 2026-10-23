#include "rt.hpp"
#include <cstdlib>

#ifdef RT_TRACE_STORES
uint32_t g_store_addr[65536];
unsigned g_store_len[65536];
unsigned g_store_n = 0;
bool g_bus_touched = false;
#endif
uint32_t bus_read(CPU* c, uint32_t a, unsigned n, uint32_t pc) {
#ifdef RT_TRACE_STORES
  g_bus_touched = true;
#endif
  return c->bus->read(*c, a, n, pc);
}
void bus_write(CPU* c, uint32_t a, unsigned n, uint32_t v, uint32_t pc) {
#ifdef RT_TRACE_STORES
  g_bus_touched = true;
#endif
  c->bus->write(*c, a, n, v, pc);
}

static inline uint32_t count_now(const CPU* c) { return uint32_t(c->icount * c->count_per_insn) + c->count_off; }

// CP0 register numbers used here
enum { C0_COUNT = 9, C0_COMPARE = 11, C0_STATUS = 12, C0_CAUSE = 13, C0_EPC = 14, C0_PRID = 15, C0_CONFIG = 16 };
static const uint32_t ST_IE = 1u, ST_EXL = 2u, ST_ERL = 4u, ST_BEV = 1u << 22;

uint32_t CP0R(CPU* c, unsigned reg, unsigned sel) {
  if (reg == C0_COUNT) return count_now(c);
  if (reg == C0_CAUSE) {
    uint32_t v = c->cp0[C0_CAUSE][0] & ~(0xffu << 8);
    v |= (c->ip7 ? 1u << 15 : 0u) | ((c->ext_irq & 31u) << 10) | (c->cp0[C0_CAUSE][0] & (3u << 8));
    return v;
  }
  if (reg == C0_PRID && sel == 0) return c->cp0[C0_PRID][0] ? c->cp0[C0_PRID][0] : 0x00019300u;   // MIPS 24Kc-like PRId (ASSUMPTION; the firmware only needs a plausible value)
  return c->cp0[reg][sel & 7];
}
void CP0W(CPU* c, unsigned reg, unsigned sel, uint32_t v) {
  if (reg == C0_COUNT) { c->count_off = v - uint32_t(c->icount * c->count_per_insn); c->prev_count = v; return; }
  if (reg == C0_COMPARE) { c->cp0[C0_COMPARE][0] = v; c->ip7 = false; return; }
  if (reg == C0_CAUSE) { c->cp0[C0_CAUSE][0] = (c->cp0[C0_CAUSE][0] & ~(3u << 8)) | (v & (3u << 8)) | (v & (1u << 23)) | (v & (1u << 27)); return; }
  c->cp0[reg][sel & 7] = v;
}
void CP0EI(CPU* c) { c->cp0[C0_STATUS][0] |= ST_IE; }
void CP0DI(CPU* c) { c->cp0[C0_STATUS][0] &= ~ST_IE; }
uint32_t ERET(CPU* c) {
  uint32_t& st = c->cp0[C0_STATUS][0];
  uint32_t target;
  if (st & ST_ERL) { target = c->cp0[30][0]; st &= ~ST_ERL; } else { target = c->cp0[C0_EPC][0]; st &= ~ST_EXL; }
  return target;
}
static uint32_t vector_base(CPU* c) {
  if (c->cp0[C0_STATUS][0] & ST_BEV) return 0xbfc00200u;
  uint32_t eb = c->cp0[C0_PRID][1];
  return eb ? (eb & 0xfffff000u) : 0x80000000u;
}
static uint32_t take_exception(CPU* c, uint32_t epc, unsigned code, uint32_t offset) {
  ++c->exceptions;
  uint32_t& st = c->cp0[C0_STATUS][0];
  if (!(st & ST_EXL)) c->cp0[C0_EPC][0] = epc;
  c->cp0[C0_CAUSE][0] = (c->cp0[C0_CAUSE][0] & ~(31u << 2)) | (code << 2);
  st |= ST_EXL;
  return vector_base(c) + offset;
}
uint32_t RAISE_SYSCALL(CPU* c, uint32_t pc) { return take_exception(c, pc, 8, 0x180); }
uint32_t RAISE_BREAK(CPU* c, uint32_t pc) { return take_exception(c, pc, 9, 0x180); }
uint32_t RAISE_TRAP(CPU* c, uint32_t pc) { return take_exception(c, pc, 13, 0x180); }
void TRAP_IMPL(CPU* c, uint32_t pc, uint32_t word) {
  std::fprintf(stderr, "NATIVE STOP: unsupported/undecoded instruction %08x at %08x (icount %llu)\n", word, pc, (unsigned long long)c->icount);
  c->fatal = true;
  throw 1;
}
uint32_t BADPC(CPU* c, uint32_t pc) {
  std::fprintf(stderr, "NATIVE STOP: dispatch into an address with no recompiled code: %08x (icount %llu)\n", pc, (unsigned long long)c->icount);
  c->fatal = true;
  throw 2;
}

// Delivers a pending timer/external interrupt between runs; returns true if one was taken (pc updated).
static bool check_interrupts(CPU* c, uint32_t& pc) {
  uint32_t cnt = count_now(c);
  const uint32_t cmp = c->cp0[C0_COMPARE][0];
  uint32_t d1 = cmp - c->prev_count, d2 = cnt - c->prev_count;
  if (!c->ip7 && d1 != 0 && d1 <= d2) c->ip7 = true;
  c->prev_count = cnt;
  uint32_t st = c->cp0[C0_STATUS][0];
  if (!(st & ST_IE) || (st & (ST_EXL | ST_ERL))) return false;
  uint32_t ip = (c->ip7 ? 0x80u : 0u) | ((c->ext_irq & 31u) << 2) | ((c->cp0[C0_CAUSE][0] >> 8) & 3u);
  if (!((ip << 8) & st & 0xff00u)) return false;
  if (c->ip7 && ((0x80u << 8) & st)) ++c->timer_irqs; else ++c->ext_irqs;
  pc = take_exception(c, pc, 0, (c->cp0[C0_CAUSE][0] & (1u << 23)) ? 0x200 : 0x180);
  return true;
}

// Idle fast-forward for `wait`: advance guest time to the next timer match.
static void idle(CPU* c) {
  const uint32_t cnt = count_now(c), cmp = c->cp0[C0_COMPARE][0];
  uint32_t d = cmp - cnt;
  if (!c->ip7 && d && d < 0x80000000u) c->icount += d / c->count_per_insn + 1;
}

uint64_t run_guest(CPU& c, uint32_t pc, uint64_t limit, RunHooks* hooks) {
  try {
    while (c.icount < limit && !c.halt) {
      uint32_t idx = (pc - GEN_BASE) >> 2;
      if (pc < GEN_BASE || (pc & 3) || idx >= GEN_WORDS || !GEN_INDEX[idx]) BADPC(&c, pc);
      c.ring[c.ring_i++ & 63] = pc;
      pc = GEN_RUNS[GEN_INDEX[idx]](&c, pc);
      if (hooks && hooks->after_run) hooks->after_run(c, pc, hooks->ctx);
      if (c.wait) { c.wait = 0; idle(&c); }
      check_interrupts(&c, pc);
    }
  } catch (int) {
  }
  c.last_pc = pc;
  return c.icount;
}
