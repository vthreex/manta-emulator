// Finds where the MIPS registers live inside Unicorn's saved CPU context blob (differential: write two values, save the context, diff).
// Output is the table native/linux/uclite.cpp uses to restore a snapshot's "mips" section without Unicorn.
#include "cpu/engine.hpp"
#include <cstdio>
#include <vector>
using namespace manta;
static std::vector<uint8_t> save(Engine& e) {
  uc_context* ctx = nullptr;
  check(uc_context_alloc(e.handle, &ctx));
  check(uc_context_save(e.handle, ctx));
  size_t total = uc_context_size(e.handle), payload = *reinterpret_cast<size_t*>(ctx);
  std::vector<uint8_t> blob(payload);
  std::memcpy(blob.data(), reinterpret_cast<uint8_t*>(ctx) + (total - payload), payload);
  uc_context_free(ctx);
  return blob;
}
int main() {
  Engine e;
  std::printf("blob size %zu\n", save(e).size());
  struct R { const char* name; int id; };
  std::vector<R> regs;
  for (int i = 1; i < 32; ++i) { static char names[32][8]; std::snprintf(names[i], 8, "r%d", i); regs.push_back({names[i], UC_MIPS_REG_0 + i}); }
  regs.push_back({"pc", UC_MIPS_REG_PC}); regs.push_back({"hi", UC_MIPS_REG_HI}); regs.push_back({"lo", UC_MIPS_REG_LO});
  regs.push_back({"status", UC_MIPS_REG_CP0_STATUS}); regs.push_back({"cause", UC_MIPS_REG_CP0_CAUSE}); regs.push_back({"epc", UC_MIPS_REG_CP0_EPC});
  regs.push_back({"compare", UC_MIPS_REG_CP0_COMPARE}); regs.push_back({"count", UC_MIPS_REG_CP0_COUNT});
  // CP0 Cause/EPC cannot be written through uc_reg_write: run MTC0 code instead and diff
  {
    e.map(0x1000, 0x1000);
    auto A = save(e);
    uint32_t code[] = {0x3c080000u, 0x35085a5au, 0x40886000u + 0u, 0x3c080000u, 0x35085a5au, 0x40887000u + 0u};   // lui/ori $t0 ; mtc0 $t0,$12.. placeholder
    uint32_t codeEpc[] = {0x3c081234u, 0x35085678u, 0x40887000u};      // mtc0 $t0, $14 (EPC)
    uint32_t codeCause[] = {0x24080300u, 0x40886800u + 0x0u};          // addiu $t0,$zero,0x300 ; mtc0 $t0,$13 (Cause)
    (void)code;
    e.write(0x1000, codeEpc, sizeof codeEpc);
    e.reg(UC_MIPS_REG_CP0_STATUS, 0x10400000u);
    auto s0 = save(e);
    e.run(0x80001000u, 3);
    auto s1 = save(e);
    std::printf("EPC (wrote 12345678) offsets:");
    for (size_t i = 0; i + 4 <= s0.size(); i += 4) if (std::memcmp(&s0[i], &s1[i], 4)) std::printf(" %zu(%02x%02x%02x%02x)", i, s1[i+3], s1[i+2], s1[i+1], s1[i]);
    uint32_t epcRead = e.reg(UC_MIPS_REG_CP0_EPC);
    std::printf("  uc_reg_read EPC=%08x\n", epcRead);
    e.write(0x1100, codeCause, sizeof codeCause);
    auto s2 = save(e);
    e.run(0x80001100u, 2);
    auto s3 = save(e);
    std::printf("Cause (wrote 300) offsets:");
    for (size_t i = 0; i + 4 <= s2.size(); i += 4) if (std::memcmp(&s2[i], &s3[i], 4)) std::printf(" %zu(%02x%02x%02x%02x)", i, s3[i+3], s3[i+2], s3[i+1], s3[i]);
    std::printf("  uc_reg_read Cause=%08x\n", e.reg(UC_MIPS_REG_CP0_CAUSE));
    (void)A;
  }
  for (auto& r : regs) {
    uint32_t a = 0x00000000u, b = 0x00005a5au;
    e.reg(r.id, a); auto A = save(e);
    e.reg(r.id, b); auto B = save(e);
    uint32_t got = e.reg(r.id);
    std::printf("%-8s wrote %08x reads %08x offsets:", r.name, b, got);
    for (size_t i = 0; i + 4 <= A.size(); ++i) if (std::memcmp(&A[i], &B[i], 4) != 0 && (i % 4 == 0)) std::printf(" %zu(%02x%02x%02x%02x->%02x%02x%02x%02x)", i, A[i+3], A[i+2], A[i+1], A[i], B[i+3], B[i+2], B[i+1], B[i]);
    std::printf("\n");
    e.reg(r.id, 0);
  }
}
