"""Original surface-constructor clear packets; isolated, no framebuffer/raster effects."""
import argparse,hashlib,json,os,subprocess,tempfile,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('firmware','probe','report'):p.add_argument('--'+name,required=True)
a=p.parse_args();stock=Path(a.firmware).read_bytes();sha=hashlib.sha256(stock).hexdigest()
assert sha=='d60645b549a456d4193e7b8bf16fc892118cba9489dcabf74848336f6f1030e9'
image=zlib.decompress(stock[0x23880:0x16124e],-15)
with tempfile.TemporaryDirectory(prefix='manta-original-clear-') as directory:
    target=Path(directory)/'original-main.bin';target.write_bytes(image)
    first=subprocess.check_output([a.probe,str(target)],text=True,timeout=30)
    assert subprocess.check_output([a.probe,str(target)],text=True,timeout=30)==first
    result=json.loads(first)
    assert result['result']=='PASS' and result['cases']==5
    assert result['raster_requests_rejected']==3 and result['zero_size_returns']==2
    assert result['raster_command']==0x30 and result['original_instructions']==4352
    assert result['original_format_capability_cases']==4 and result['original_destination_binding_cases']==8
    assert result['original_destination_binding_instructions']>4000
    assert not result['pixels_generated'] and not result['framebuffer_allocated']
    damaged=bytearray(image);damaged[0x8049e158-0x803b3980]^=1;target.write_bytes(damaged)
    rejected=subprocess.run([a.probe,str(target)],capture_output=True,text=True,timeout=30)
    assert rejected.returncode==1 and 'original main image mismatch' in rejected.stderr
    target.write_bytes(image)
    env=os.environ.copy();env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')
    assert subprocess.check_output([str(Path(a.probe).resolve()),str(target)],text=True,env=env,timeout=30)==first
assert hashlib.sha256(Path(a.firmware).read_bytes()).hexdigest()==sha
report={'result':'PASS','scope':'isolated original clear-constructor fragment with synthetic context; no TV frame or raster effects',
        'original_functions':result,'corrupted_main_rejected':True,'clean_PATH_probe_pass':True,
        'probe_sha256':hashlib.sha256(Path(a.probe).read_bytes()).hexdigest(),'firmware_sha256':sha}
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original clear command30, rectangle/color writes, zero-size rejection and unresolved raster boundary')
