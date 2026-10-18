"""Static analysis anchors: AVD register ownership scanner and HTotal join proof.

The scanner must find all five previously known original-MIPS host accessors
and the AVD publisher/mailbox writes; the join proof must keep showing that the
HTotal comparison outcomes differ only in registers dead at the join.
"""
import argparse, json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
for n in ('firmware', 'avd', 'probe', 'amethyst', 'marlon', 'report'):
    p.add_argument('--' + n, required=True)
a = p.parse_args()

with tempfile.TemporaryDirectory(prefix='manta-ownership-') as temp:
    own = Path(temp) / 'own.json'
    subprocess.run([sys.executable, str(ROOT / 'tools/avd_register_ownership.py'), '--firmware', a.firmware, '--avd', a.avd,
                    '--probe', a.probe, '--driver', a.amethyst, '--driver', a.marlon, '--report', str(own)],
                   check=True, capture_output=True)
    regs = json.loads(own.read_text())['registers']
    # Five known host accessors in the original MIPS image.
    expected = {'0x11c7': '0x805030fc', '0x11c8': '0x805030f8', '0x11ca': '0x80503098', '0x11dc': '0x805032a0'}
    for reg, pc in expected.items():
        assert any(x['pc'] == pc for x in regs[reg]['original_mips_register_accesses']), (reg, pc)
    assert {x['pc'] for x in regs['0x119c']['original_mips_register_accesses']} == {'0x80503644', '0x80503650'}
    # AVD-side facts the analysis depends on.
    assert regs['0x11c7']['avd_mcu_accesses'].get('helper_write16') and regs['0x11c8']['avd_mcu_accesses'].get('helper_write16')
    assert regs['0x11da']['avd_mcu_accesses'].get('helper_write8'), '11DA is written by the AVD'
    assert regs['0x11fc']['avd_mcu_accesses'].get('read') and regs['0x11fc']['avd_mcu_accesses'].get('helper_write8')
    assert list(regs['0x11db']['avd_mcu_accesses']) == ['read'], 'no direct AVD writer of 11DB in the searched forms'
    assert not regs['0x11db']['original_mips_register_accesses'] and not regs['0x11db']['original_mips_init_table_entries']
    assert all(not hits for hits in regs['0x11db']['reference_driver_function_hits'].values())

    join = Path(temp) / 'join.json'
    subprocess.run([sys.executable, str(ROOT / 'tools/mips_join_proof.py'), '--firmware', a.firmware, '--report', str(join)],
                   check=True, capture_output=True)
    proof = json.loads(join.read_text())
    assert proof['registers_differing_between_paths'] == ['a0', 'v0', 'v1']
    assert proof['differing_and_live'] == [] and proof['join_is_register_compatible']
    assert proof['join_has_no_mmio_or_control_effects'] and not proof['non_ram_stores']
    stores = {n: sorted(e[1] for e in es if e[0].startswith('store')) for n, es in proof['path_effects'].items()}
    assert stores == {'mismatch': ['0x806d83d4'], 'match_counter_lt4': ['0x806d83d4'], 'match_counter_ge4': ['0x806ad960']}, stores
    Path(a.report).write_text(json.dumps({'result': 'PASS', 'join_live_registers': proof['registers_live_at_join'],
                                          'analysis_nodes': proof['analysis_nodes']}, indent=2) + '\n')
print('PASS: ownership scanner finds all known accessors; HTotal join differs only in dead registers')
