"""Cross-reference original PM instructions against local primary sources.

References are read-only research inputs, never substituted firmware. Provide
a local neuschaefer/mstar-mboot checkout (or the selected downloaded files).
"""
import argparse, hashlib, json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--firmware',required=True)
p.add_argument('--reference-root',required=True)
p.add_argument('--report',required=True)
a=p.parse_args()
stock=Path(a.firmware).read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(stock)=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
pm=stock[0x10000:0x20000]
# The original getter returns the low byte in R7; no high-byte decision occurs.
assert pm[0x9b22:0x9b2a]==bytes.fromhex('ef5407640770ee22')
assert pm[0x571c:0x571f]==bytes.fromhex('129e9f')
assert pm[0x5722:0x5730]==bytes.fromhex('900e137410f0f5a0901e037404f0')
assert pm[0x936c:0x938a]==bytes.fromhex('43d88075aaf675ba03759850438780ef6002f599759dd975bb03759b9022')
assert pm[0x5749:0x5750]==bytes.fromhex('900e6ae0547ff0')
assert pm[0x5757:0x575e]==bytes.fromhex('900ec2e030e3f9')
root=Path(a.reference_root)
references=[]
for chip in ('amber5','kronus'):
    path=Path('sboot/src')/chip/'boot.S'
    raw=(root/path).read_bytes();text=raw.decode('latin1')
    assert 'CheckPowerGood' in text
    assert '0x90,0x0E,0xC2' in text and '0x54,0x07' in text and '0xB4,0x07' in text
    references.append({'path':path.as_posix(),'sha256':sha(raw),
        'url':'https://github.com/neuschaefer/mstar-mboot/blob/master/'+path.as_posix(),
        'supports':'same XDATA address, mask and all-set predicate named CheckPowerGood',
        'does_not_establish':'individual MSD309 detectors, thresholds, reset values, fourth status bit'})
for chip in ('amber5','kaiser'):
    path=Path('sboot/src')/chip/'drvPadConf.c'
    raw=(root/path).read_bytes();text=raw.decode('latin1')
    assert '_RVM1(0x0E13, BIT3, BIT3)' in text and 'RX0_ENABLE' in text
    assert '_RVM1(0x0E13, 0 , BIT4)' in text
    references.append({'path':path.as_posix(),'sha256':sha(raw),
        'url':'https://github.com/neuschaefer/mstar-mboot/blob/master/'+path.as_posix(),
        'supports':'related UART receive/select configuration',
        'does_not_establish':'exact Titania4 pad routing or reset configuration'})
report={'result':'PASS','stock_sha256':sha(stock),'PM_sha256':sha(pm),
 'original_status_reader':'0x9B17..0x9B29, low byte of XDATA 0x0EC2',
 'equivalent_predicate_for_all_low_bit_combinations':all(((v&7)^7)==0 if v==7 else ((v&7)^7)!=0 for v in range(8)),
 'best_supported_interpretation':'aggregate power-good poll; individual bit producers unverified',
 'original_UART_initializer':'0x936C..0x9389; R8051-like ADCON, S0/S1 reload and control writes',
 'next_unknown_write':'0x574F XDATA 0x0E6A clears bit7, lower bits preserved invalid',
 'subsequent_unknown_predicate':'0x575B waits for XDATA 0x0EC2 bit3; not covered by named CheckPowerGood source',
 'references':references,'strict_mode_status_producer_verified':False,
 'guest_execution_claim':'none; this is static cross-reference evidence only'}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: exact original PM predicates and UART setup cross-referenced; MSD309 detector/reset uncertainty retained')
