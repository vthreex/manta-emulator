"""Candidate decoding of actual recovered uploads, not auxiliary execution."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser()
for n in ('disassembler','addressed-image','report'):p.add_argument('--'+n,required=True)
a=p.parse_args();b=Path(a.addressed_image).read_bytes()
assert hashlib.sha256(b).hexdigest()=='f25cba069289823a8165f3d22dbbd90f58c83bbdc49aeff90f6476faad7da2df'
def run(path):return subprocess.run([a.disassembler,'--candidate-adsp21xx',str(path),'0','0x80'],text=True,capture_output=True)
r=run(a.addressed_image);assert r.returncode==0,r.stderr
assert 'ARCHITECTURE_HYPOTHESIS' in r.stdout and 'instructions executed=0' in r.stdout
assert '0000 18064f JUMP $0064' in r.stdout and '0001 0a001f RTI' in r.stdout
assert '0064 040040 ??? (040040)' in r.stdout and '0065 1ffdef CALL $3FDE' in r.stdout
with tempfile.TemporaryDirectory(prefix='manta-dsp-decoder-') as directory:
    f=Path(directory)/'invalid.bin'
    for bad in (b[:-1],b[:5]+b[:5],b''):
        f.write_bytes(bad);q=run(f);assert q.returncode==1 and q.stderr
report={'result':'PASS','scope':'static candidate ISA decoding only',
        'original_addressed_word_sha256':hashlib.sha256(b).hexdigest(),
        'unknown_entry_word_remains_unknown':True,'DSP_instructions_executed':0,
        'malformed_inputs_rejected':3,'disassembler_sha256':hashlib.sha256(Path(a.disassembler).read_bytes()).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: candidate original DSP vector decoding, unknown entry retained, malformed input rejection; no DSP execution claim')
