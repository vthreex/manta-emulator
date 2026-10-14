"""Diagnostic exports may not truncate the stock input through path aliases."""
import argparse, hashlib, json, os, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--emulator',required=True)
p.add_argument('--report',required=True)
a=p.parse_args()
exe=str(Path(a.emulator).resolve())
cases=[]
with tempfile.TemporaryDirectory(prefix='manta-immutable-input-') as directory:
    root=Path(directory)
    image=root/'original.bin'
    original=b'immutable input policy fixture, not a TV firmware image'
    image.write_bytes(original)
    aliases=[image, root/'subdir'/ '..'/'original.bin', root/'hard-link.bin']
    (root/'subdir').mkdir()
    os.link(image,aliases[-1])
    for output in ('--pm51-history','--summary','--coverage','--record-trace','--tsp-firmware-dump','--nvram'):
        for alias in aliases:
            r=subprocess.run([exe,'--headless','--firmware',str(image),output,str(alias)],capture_output=True,text=True)
            assert r.returncode==1 and 'aliases immutable firmware input' in r.stderr, (output,alias,r.stderr)
            assert image.read_bytes()==original and aliases[-1].read_bytes()==original
            cases.append({'option':output,'alias':alias.name,'rejected':True})
    r=subprocess.run([exe,'--headless','--firmware',str(image),'--summary',str(root/'different.json')],capture_output=True,text=True)
    assert r.returncode==1 and 'aliases immutable' not in r.stderr
    assert image.read_bytes()==original and not (root/'different.json').exists()
Path(a.report).write_text(json.dumps({'result':'PASS','cases':cases,
 'scope':'file identity protection before guest execution; policy fixture is not original firmware',
 'emulator_sha256':hashlib.sha256(Path(exe).read_bytes()).hexdigest()},indent=2)+'\n')
print('PASS: 18 immutable-input path/alias checks; distinct output is not rejected as alias')
