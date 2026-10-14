"""Execute the unmodified, guest-uploaded AVD image; unknown hardware must stop."""
import argparse, hashlib, json, subprocess, tempfile, zlib
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args(); stock=Path(a.firmware).read_bytes()
assert len(stock)==0x800010 and hashlib.sha256(stock).hexdigest()=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
main=zlib.decompress(stock[0x23880:0x16124e],-15)
assert hashlib.sha256(main).hexdigest()=='149c6d713a91017eff5352fe2227e9dc99603869683ac8a57b66c83bc63ef236'
blob=main[0x80627a00-0x803b3980:0x8062ae00-0x803b3980]
sha=hashlib.sha256(blob).hexdigest()
assert len(blob)==13312 and sha=='48ab58a5689d3135b2cdf78fa77da92fa912776f741c33c7afa97fdcfc4ee4fd'
with tempfile.TemporaryDirectory(prefix='manta-original-avd-') as directory:
    image=Path(directory)/'original-avd.bin';image.write_bytes(blob)
    r=subprocess.run([a.probe,str(image)],text=True,capture_output=True)
    assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
    lines=r.stdout.splitlines()
    assert 'AVD_XDATA write 0xc008 value 0x55' in lines
    assert 'AVD_ORIGINAL_INSTRUCTIONS 6 PC 0x802 SP 0xae' in lines
    assert any(x.startswith('AVD_BOUNDARY ') and 'address 0xc008' in x for x in lines)
    assert '0000 02 LJMP  #0003h' in lines and '0219 f1 ACALL 07FDh' in lines
    pm=stock[0x10000:0x20000]
    assert hashlib.sha256(pm).hexdigest()=='5f9d541120948f5067d162048c17f30fc704f871f9fdfbcc161181567f48fd3c'
    candidate=Path(directory)/'original-pm-candidate.bin';candidate.write_bytes(pm)
    dis=subprocess.run([a.probe,'--disassemble-only',str(candidate),'0','3'],text=True,capture_output=True)
    assert dis.returncode==0 and 'LJMP  #60D8h' in dis.stdout and 'no auxiliary instructions executed' in dis.stdout
    truncated=subprocess.run([a.probe,'--disassemble-only',str(candidate),'0','2'],text=True,capture_output=True)
    assert truncated.returncode==1 and 'truncated candidate instruction' in truncated.stderr
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==hashlib.sha256(stock).hexdigest()
report={'result':'PASS','scope':'original AVD auxiliary diagnostic, not complete startup or TV boot',
        'program_sha256':sha,'upload_bytes':len(blob),'instructions':6,'pc':'0x0802',
        'unknown_xdata':'0xC008','attempted_write':'0x55','hardware_side_effects':False,
        'stdout':r.stdout,'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),
        'PM51_candidate_static_disassembly':'PASS; reset LJMP 60D8 and truncated-range rejection; no PM execution',
        'PM51_candidate_sha256':hashlib.sha256(pm).hexdigest()}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original AVD reset/startup instructions; first unknown transaction stopped without acknowledgment')
