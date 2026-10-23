// "Unicorn-lite": the part of the Unicorn API that Msd309Machine uses to hold guest registers and memory, WITHOUT any instruction execution.
// The native build executes the firmware as recompiled machine code (native/gen), so no CPU emulator is linked. Memory regions (plain, shared,
// MMIO) and the register file live here; uc_emu_start refuses to run. Snapshot "mips" blobs written by the Unicorn-based emulator are decoded with
// offsets measured by tools/uc_context_layout_probe.cpp (blob size 11196: gpr[i] at 4*i, PC 128, HI 132, LO 148, Count 1300, Compare 1336,
// Status 1340, Cause 1356, EPC 1360).
#include <unicorn/unicorn.h>
#include <unicorn/mips.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

struct Region {
  uint64_t begin = 0, size = 0;
  uint8_t* ptr = nullptr;
  std::vector<uint8_t> own;
  uc_cb_mmio_read_t rd = nullptr; void* rdUser = nullptr;
  uc_cb_mmio_write_t wr = nullptr; void* wrUser = nullptr;
};
struct uc_struct {
  std::vector<Region> regions;
  std::map<int, uint32_t> regs;
};
static constexpr size_t kBlob = 11196;
struct __attribute__((packed)) uc_context { size_t payload; uint8_t blob[kBlob]; };   // packed: uc_context_size() - payload must be the blob offset (snapshot.cpp)
static const std::pair<int, size_t> kCtxRegs[] = {{UC_MIPS_REG_PC, 128}, {UC_MIPS_REG_HI, 132}, {UC_MIPS_REG_LO, 148}, {UC_MIPS_REG_CP0_COUNT, 1300},
  {UC_MIPS_REG_CP0_COMPARE, 1336}, {UC_MIPS_REG_CP0_STATUS, 1340}, {UC_MIPS_REG_CP0_CAUSE, 1356}, {UC_MIPS_REG_CP0_EPC, 1360}};

static Region* find(uc_engine* uc, uint64_t a, uint64_t n) {
  for (auto& r : uc->regions) if (a >= r.begin && a + n <= r.begin + r.size) return &r;
  return nullptr;
}
extern "C" {
uc_err uc_open(uc_arch, uc_mode, uc_engine** out) { *out = new uc_struct; return UC_ERR_OK; }
uc_err uc_close(uc_engine* uc) { delete uc; return UC_ERR_OK; }
const char* uc_strerror(uc_err e) { return e == UC_ERR_OK ? "OK" : e == UC_ERR_ARG ? "Invalid argument (UC_ERR_ARG)" : e == UC_ERR_READ_UNMAPPED ? "Invalid memory read (UC_ERR_READ_UNMAPPED)" : "uclite: CPU emulation is not available in this build"; }
uc_err uc_mem_map(uc_engine* uc, uint64_t a, size_t n, uint32_t) { Region r; r.begin = a; r.size = n; r.own.assign(n, 0); r.ptr = r.own.data(); uc->regions.push_back(std::move(r)); return UC_ERR_OK; }
uc_err uc_mem_map_ptr(uc_engine* uc, uint64_t a, size_t n, uint32_t, void* p) { Region r; r.begin = a; r.size = n; r.ptr = static_cast<uint8_t*>(p); uc->regions.push_back(std::move(r)); return UC_ERR_OK; }
uc_err uc_mmio_map(uc_engine* uc, uint64_t a, size_t n, uc_cb_mmio_read_t rd, void* ru, uc_cb_mmio_write_t wr, void* wu) {
  Region r; r.begin = a; r.size = n; r.rd = rd; r.rdUser = ru; r.wr = wr; r.wrUser = wu; uc->regions.push_back(std::move(r)); return UC_ERR_OK;
}
uc_err uc_mem_read(uc_engine* uc, uint64_t a, void* out, size_t n) {
  auto* r = find(uc, a, n);
  if (!r) return UC_ERR_READ_UNMAPPED;
  if (r->ptr) { std::memcpy(out, r->ptr + (a - r->begin), n); return UC_ERR_OK; }
  auto* o = static_cast<uint8_t*>(out);
  for (size_t i = 0; i < n; ++i) o[i] = uint8_t(r->rd(uc, a + i - r->begin, 1, r->rdUser));
  return UC_ERR_OK;
}
uc_err uc_mem_write(uc_engine* uc, uint64_t a, const void* in, size_t n) {
  auto* r = find(uc, a, n);
  if (!r) return UC_ERR_WRITE_UNMAPPED;
  if (r->ptr) { std::memcpy(r->ptr + (a - r->begin), in, n); return UC_ERR_OK; }
  auto* p = static_cast<const uint8_t*>(in);
  for (size_t i = 0; i < n; ++i) r->wr(uc, a + i - r->begin, 1, p[i], r->wrUser);
  return UC_ERR_OK;
}
uc_err uc_reg_read(uc_engine* uc, int id, void* v) { auto it = uc->regs.find(id); uint32_t x = it == uc->regs.end() ? 0 : it->second; std::memcpy(v, &x, 4); return UC_ERR_OK; }
uc_err uc_reg_write(uc_engine* uc, int id, const void* v) { uint32_t x; std::memcpy(&x, v, 4); uc->regs[id] = x; return UC_ERR_OK; }
uc_err uc_hook_add(uc_engine*, uc_hook* h, int, void*, void*, uint64_t, uint64_t, ...) { *h = 0; return UC_ERR_OK; }
// Snapshot restore replays the guest TLB installs through a short MIPS routine; the native runtime has no TLB (it hard-wires the one observed kseg2 mapping, see
// native_runner.cpp MachineBus), so nothing needs to execute: report success. Never used to run guest code in this build.
uc_err uc_emu_start(uc_engine*, uint64_t, uint64_t, uint64_t, size_t) { return UC_ERR_OK; }
uc_err uc_emu_stop(uc_engine*) { return UC_ERR_OK; }
uc_err uc_ctl(uc_engine*, uc_control_type, ...) { return UC_ERR_OK; }
uc_err uc_context_alloc(uc_engine*, uc_context** c) { *c = static_cast<uc_context*>(std::calloc(1, sizeof(uc_context))); (*c)->payload = kBlob; return UC_ERR_OK; }
size_t uc_context_size(uc_engine*) { return sizeof(uc_context); }
uc_err uc_context_free(uc_context* c) { std::free(c); return UC_ERR_OK; }
uc_err uc_context_save(uc_engine* uc, uc_context* c) {
  std::memset(c->blob, 0, kBlob);
  for (int i = 1; i < 32; ++i) { uint32_t v = uc->regs[UC_MIPS_REG_0 + i]; std::memcpy(c->blob + 4 * i, &v, 4); }
  for (auto& [id, off] : kCtxRegs) { uint32_t v = uc->regs[id]; std::memcpy(c->blob + off, &v, 4); }
  return UC_ERR_OK;
}
uc_err uc_context_restore(uc_engine* uc, uc_context* c) {
  { uint32_t v[3]; std::memcpy(&v[0], c->blob + 4 * 29, 4); std::memcpy(&v[1], c->blob + 4 * 31, 4); std::memcpy(&v[2], c->blob + 128, 4);
    std::fprintf(stderr, "uclite: context_restore sp=%08x ra=%08x pc=%08x\n", v[0], v[1], v[2]); }
  for (int i = 1; i < 32; ++i) { uint32_t v; std::memcpy(&v, c->blob + 4 * i, 4); uc->regs[UC_MIPS_REG_0 + i] = v; }
  for (auto& [id, off] : kCtxRegs) { uint32_t v; std::memcpy(&v, c->blob + off, 4); uc->regs[id] = v; }
  return UC_ERR_OK;
}
}
