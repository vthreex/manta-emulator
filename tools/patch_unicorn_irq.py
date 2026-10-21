"""Add an external hardware-interrupt line register to the (already timer-patched) Unicorn 2.1.4 MIPS core.

UC_MIPS_REG_CP0_IRQ: a write sets Cause.IP2..IP6 (bits 10..14) to the given 5-bit level mask; if an unmasked line is pending the core is asked to
take the interrupt at the next instruction boundary, if none is pending the request is withdrawn. Guest instructions are never touched; exception entry,
EPC/BD and ERET stay with QEMU. The timer interrupt (IP7) and its Cause bits are not modified.
"""
import argparse
from pathlib import Path

def replace(path, old, new):
    text = path.read_text(encoding='utf-8')
    if text.count(old) != 1: raise ValueError(f'Unexpected source: {path}: {old[:40]}')
    path.write_text(text.replace(old, new), encoding='utf-8')

def patch(root):
    replace(root/'include/unicorn/mips.h', '    UC_MIPS_REG_ENDING,',
            '    // Manta external hardware interrupt lines IP2..IP6 (level mask); existing IDs unchanged.\n    UC_MIPS_REG_CP0_IRQ,\n    UC_MIPS_REG_ENDING,')
    replace(root/'include/unicorn/mips.h', '#define UC_MIPS_MANTA_TIMER_API 1', '#define UC_MIPS_MANTA_TIMER_API 1\n#define UC_MIPS_MANTA_IRQ_API 1')
    core = root/'qemu/target/mips/unicorn.c'
    replace(core, '        case UC_MIPS_REG_CP0_COUNT:\n            CHECK_REG_TYPE(mipsreg_t); env->CP0_Count = *(mipsreg_t *)value; break;',
'''        case UC_MIPS_REG_CP0_IRQ: {
            CHECK_REG_TYPE(mipsreg_t);
            uint32_t lines = (uint32_t)(*(mipsreg_t *)value) & 0x1f;
            env->CP0_Cause = (env->CP0_Cause & ~(0x1fU << 10)) | (lines << 10);
            if (env->CP0_Cause & env->CP0_Status & CP0Ca_IP_mask) {
                env_cpu(env)->interrupt_request |= CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB;
                env_cpu(env)->tcg_exit_req = 1;
                env_cpu(env)->icount_decr_ptr->u16.high = -1;
            } else if (!(env->CP0_Cause & CP0Ca_IP_mask)) {
                env_cpu(env)->interrupt_request &= ~CPU_INTERRUPT_HARD;
            }
            break;
        }
        case UC_MIPS_REG_CP0_COUNT:
            CHECK_REG_TYPE(mipsreg_t); env->CP0_Count = *(mipsreg_t *)value; break;''')

if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('source', type=Path); a = p.parse_args(); patch(a.source)
    print('Patched Unicorn MIPS with CP0_IRQ external lines; no guest patches')
