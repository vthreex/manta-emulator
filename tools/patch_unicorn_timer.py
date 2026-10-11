"""Apply the small, auditable MIPS timer extension to pristine Unicorn 2.1.4.

Guest instructions remain unchanged; QEMU owns exception entry, EPC/BD and ERET.
The board advances Count using an explicit virtual CPU clock through reg_write.
"""
import argparse
from pathlib import Path

def replace(path, old, new):
    text=path.read_text(encoding='utf-8')
    if text.count(old)!=1: raise ValueError(f'Unexpected Unicorn 2.1.4 source: {path}: {old[:40]}')
    path.write_text(text.replace(old,new),encoding='utf-8')

def patch(root):
    replace(root/'include/unicorn/mips.h','    UC_MIPS_REG_ENDING,',
'''    // Manta deterministic Count/Compare extension; existing IDs unchanged.
    UC_MIPS_REG_CP0_COUNT,
    UC_MIPS_REG_CP0_COMPARE,
    UC_MIPS_REG_CP0_CAUSE,
    UC_MIPS_REG_CP0_EPC,
    UC_MIPS_REG_CP0_TICK,
    UC_MIPS_REG_ENDING,''')
    replace(root/'include/unicorn/mips.h','#define UNICORN_MIPS_H',
            '#define UNICORN_MIPS_H\n#define UC_MIPS_MANTA_TIMER_API 1')
    # Upstream Windows x64 VEH trampoline lacks shadow space/alignment for GCC.
    win=root/'qemu/accel/tcg/translate-all.c'
    replace(win,r'\x48\x83\xec\x10',r'\x48\x83\xec\x28')
    replace(win,r'\x48\x83\xc4\x10',r'\x48\x83\xc4\x28')
    helper=root/'qemu/target/mips/cp0_helper.c'
    replace(helper,'    // return (int32_t)cpu_mips_get_count(env);\n    return 0;',
            '    return (int32_t)env->CP0_Count;')
    replace(helper,'    //cpu_mips_store_count(env, arg1);','    env->CP0_Count = (uint32_t)arg1;')
    replace(helper,'    // cpu_mips_store_compare(env, arg1);',
'''    env->CP0_Compare = (uint32_t)arg1;
    env->CP0_Cause &= ~((1U << CP0Ca_TI) | (1U << (8 + ((env->CP0_IntCtl >> CP0IntCtl_IPTI) & 7))));
    if (!(env->CP0_Cause & CP0Ca_IP_mask))
        env_cpu(env)->interrupt_request &= ~CPU_INTERRUPT_HARD;''')
    replace(root/'qemu/target/mips/cpu.c','qemu_memalign(8, sizeof(*cpu))','qemu_memalign(_Alignof(MIPSCPU), sizeof(*cpu))')
    core=root/'qemu/target/mips/unicorn.c'
    replace(core,'MIPSCPU *cpu_mips_init(struct uc_struct *uc);',
            'MIPSCPU *cpu_mips_init(struct uc_struct *uc);\nvoid helper_mtc0_compare(CPUMIPSState *env, target_ulong arg1);')
    replace(core,'        case UC_MIPS_REG_CP0_STATUS:\n            CHECK_REG_TYPE(mipsreg_t);',
'''        case UC_MIPS_REG_CP0_COUNT:
            CHECK_REG_TYPE(mipsreg_t); *(mipsreg_t *)value = env->CP0_Count; break;
        case UC_MIPS_REG_CP0_COMPARE:
            CHECK_REG_TYPE(mipsreg_t); *(mipsreg_t *)value = env->CP0_Compare; break;
        case UC_MIPS_REG_CP0_CAUSE:
            CHECK_REG_TYPE(mipsreg_t); *(mipsreg_t *)value = env->CP0_Cause; break;
        case UC_MIPS_REG_CP0_EPC:
            CHECK_REG_TYPE(mipsreg_t); *(mipsreg_t *)value = env->CP0_EPC; break;
        case UC_MIPS_REG_CP0_STATUS:
            CHECK_REG_TYPE(mipsreg_t);''')
    replace(core,'        case UC_MIPS_REG_CP0_STATUS:\n            // TODO: ALL CP0 REGS',
'''        case UC_MIPS_REG_CP0_TICK: {
            CHECK_REG_TYPE(mipsreg_t);
            uint32_t delta = *(mipsreg_t *)value;
            if (!(env->CP0_Cause & (1U << CP0Ca_DC))) {
                uint32_t distance = (uint32_t)env->CP0_Compare - (uint32_t)env->CP0_Count;
                env->CP0_Count += delta;
                if (distance && distance <= delta) {
                    env->CP0_Cause |= (1U << CP0Ca_TI) | (1U << (8 + ((env->CP0_IntCtl >> CP0IntCtl_IPTI) & 7)));
                    env_cpu(env)->interrupt_request |= CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB;
                    env_cpu(env)->tcg_exit_req = 1;
                    env_cpu(env)->icount_decr_ptr->u16.high = -1;
                }
            }
            break;
        }
        case UC_MIPS_REG_CP0_COUNT:
            CHECK_REG_TYPE(mipsreg_t); env->CP0_Count = *(mipsreg_t *)value; break;
        case UC_MIPS_REG_CP0_COMPARE:
            CHECK_REG_TYPE(mipsreg_t); helper_mtc0_compare(env, *(mipsreg_t *)value); break;
        case UC_MIPS_REG_CP0_STATUS:
            // TODO: ALL CP0 REGS''')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);a=p.parse_args();patch(a.source)
    print('Patched Unicorn 2.1.4 MIPS Count/Compare; no guest patches')
