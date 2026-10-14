"""Check the unexecuted original activity producer; never invoke it on a CPU."""
import argparse, hashlib, json, struct, subprocess, tempfile, zlib
from pathlib import Path

p=argparse.ArgumentParser()
for name in ('firmware','disassembler','report'):p.add_argument('--'+name,required=True)
a=p.parse_args()
stock=Path(a.firmware).read_bytes()
assert hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
pm=stock[0x10000:0x20000]
assert hashlib.sha256(pm).hexdigest()=='5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c'
regions={
 'event_sample':(0x9d48,0x9d60,['#14h','#1Ch','#0Eh']),
 'event_dispatch':(0x5ea3,0x5ebb,['#040A3h','SWAP','ANL   A, #01h','LCALL #9B96h']),
 'eight_producer':(0x9b96,0x9ba8,['LCALL #9E7Ah','MOV   R7, #08h','LCALL #956Bh','LCALL #9438h']),
 'activity_gate':(0x99e4,0x99f8,['#1Eh','#0Eh','JB   ACC.4','MOV   R7, #01h']),
 'wake_transition':(0x956b,0x9586,['LCALL #8AAFh','LCALL #9E31h','LCALL #9EC7h']),
 'masked_writer':(0x8aaf,0x8ae0,['MOV   A, #0Fh','#69h','#0Eh','LCALL #7380h','LJMP  #724Eh']),
 'RTC_bank_access':(0x9438,0x9455,['#00h','#12h','ORL   A, #80h','LJMP  #724Eh']),
 'cold_clear':(0x934d,0x936c,['CLR   A','#69h','#6Fh','MOV   A, #FFh','LCALL #724Eh']),
 'cold_clear_caller':(0x9e49,0x9e4f,['LCALL #934Dh']),
 'low_status_wait':(0x9b17,0x9b2a,['#C2h','#0Eh','LCALL #6D84h','ANL   A, #07h','XRL   A, #07h','JNZ']),
}
disassembly={}
with tempfile.TemporaryDirectory(prefix='manta-pm-forensics-') as directory:
    image=Path(directory)/'original-PM.bin';image.write_bytes(pm)
    for name,(start,end,expected) in regions.items():
        r=subprocess.run([a.disassembler,'--disassemble-only',str(image),hex(start),hex(end)],capture_output=True,text=True)
        assert r.returncode==0,(name,r.stderr)
        assert 'no auxiliary instructions executed' in r.stdout
        for line in expected:assert line in r.stdout,(name,line)
        disassembly[name]=r.stdout
main=zlib.decompress(stock[0x23880:0x16124e],-15)
offset=0x80404bec-0x803b3980
words=struct.unpack_from('<7I',main,offset)
assert words[2:]==(0x90621cd1,0x3042000f,0x38420008,0x03e00008,0x2c420001)
assert struct.unpack_from('<I',main,0x8046a190-0x803b3980)[0]==0x0c000000|((0x80404bec>>2)&0x03ffffff)
assert pm[0x60fb:0x60fe]==bytes.fromhex('02 70 38')
assert pm[0x7061:0x7064]==bytes.fromhex('12 9e 49')
report={'result':'PASS','scope':'static original-code evidence only; no writer invocation, PM reply or reset inference',
 'firmware_sha256':hashlib.sha256(stock).hexdigest(),'PM_sha256':hashlib.sha256(pm).hexdigest(),
 'MIPS_predicate':'(BF001CD1 & 0x0F) == 8; optional restoration, both paths join at 80469CB8',
 'eight_producer':'5EB8 -> 9B96 -> 956B -> 8AAF -> 724E/MOVX729A',
 'producer_conditions':['sampled 0E1C bit4 is set','0E1E bit4 is clear'],
 'producer_effect':'replace 0E69 low nibble with 8, preserving upper nibble; RTC bank1200 bit7 command follows',
 'interpretation':'RTC0-related wake-event reason, supported by original bank1200 access; exact interrupt wiring unverified',
 'cold_clear_path':'reset pre-C initializer5717 must return; C initialization6136 -> 60FB -> 7038 -> 7061 -> 9E49 -> 934D',
 'cold_clear_effect':'original writer prepares 0E69=0, 0E6F=FF; not executed and not a hardware reset specification',
 'status_0EC2_bit_names':None,'status_0EC2_producer':None,'writer_executed':False,
 'disassembler_sha256':hashlib.sha256(Path(a.disassembler).read_bytes()).hexdigest(),
 'disassembly':disassembly}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original activity-eight and cold-clear producer paths; static evidence, no invented PM state')
