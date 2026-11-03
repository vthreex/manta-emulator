"""End translation blocks before the TCG temp array overflows.

Unicorn 2.1.4 (a QEMU 5 fork) allocates TCG temporaries with `tcg_temp_alloc`, which has only a debug-build bound check.
Its instrumentation (block/code hooks) uses extra temporaries, so a very long straight-line translation block can exceed
TCG_MAX_TEMPS (512 on 64-bit hosts) and the `memset` of the next temp then zeroes the fields that follow `temps[]` in
TCGContext (the op list head/tail).  The result is a host segfault in `tcg_emit_op` while translating guest code.

The Sunplus firmware's unrolled SHA-256 routine (guest 0x80d18000..0x80d19xxx, ~1,100 straight-line instructions with
many `rotr`) triggers it with a block hook installed.  This patch makes `tcg_op_buf_full` also stop the block when the
temp count is within 96 of the limit, which is a conservative superset of the existing 4000-op stop.

usage: python patch_unicorn_temps.py UNICORN_SRC_DIR
"""
import sys

path = sys.argv[1].rstrip('/\\') + '/qemu/include/tcg/tcg.h'
src = open(path, 'rb').read().decode()
old = 'return tcg_ctx->nb_ops >= 4000;'
new = 'return tcg_ctx->nb_ops >= 4000 || tcg_ctx->nb_temps >= TCG_MAX_TEMPS - 96;'
if new in src:
    print('already patched')
elif old in src:
    open(path, 'wb').write(src.replace(old, new, 1).encode())
    print('patched', path)
else:
    sys.exit('tcg_op_buf_full not found')
